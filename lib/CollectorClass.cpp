#include "CollectorClass.h"

CollectorSink * CollectorSink::instance_ = nullptr;

CollectorSink * CollectorSink::instance() {
  if (instance_ == nullptr) {
    instance_ = new CollectorSink();
  }
  return instance_;
}

void CollectorSink::destroy() {
  if (instance_ != nullptr) {
    instance_->teardown();
    delete instance_;
    instance_ = nullptr;
  }
}


RL_API void ensure_file_data_type(HighFive::File file, const std::string & type_name, const HighFive::CompoundType & hc_type){
  try {
    auto existing_type = file.getDataType(type_name);
  } catch (HighFive::Exception & ex) {
    std::cout << "No existing type for " << type_name << " found in file, committing new type.\n";
    hc_type.commit(file, type_name);
    return;
  }
  if (file.getDataType(type_name) != hc_type) {
    std::cerr << "Warning: file already has a type for " << type_name << " which does not match the expected structure. This may cause problems when writing events.\n";
  }
}

HighFive::CompoundType hdf_compound_type(const ReadoutType readout){
  using namespace HighFive;
  switch (readout){
    case ReadoutType::CAEN: return create_datatype<CAEN_event>();
    case ReadoutType::TTLMonitor: return create_datatype<TTLMonitor_event>();
    case ReadoutType::CDT: return create_datatype<CDT_event>();
    case ReadoutType::VMM3: return create_datatype<VMM3_event>();
    case ReadoutType::BM0: return create_datatype<BM0_event>();
    case ReadoutType::BM2: return create_datatype<BM2_event>();
    case ReadoutType::BMI: return create_datatype<BMI_event>();
    default: throw std::runtime_error("Saving this readout type is not implemented yet!");
  }
}

template<class T>
static void concat_dataset_impl(HighFive::DataSet & out, const HighFive::DataSet & in, const size_t out_offset) {
  const auto out_datatype = out.getDataType();
  std::vector<T> buffer(in.getDimensions().front());
  in.read_raw(buffer.data(), out_datatype);
  out.select({out_offset}, {buffer.size()}).write_raw(buffer.data(), out_datatype);
}

static void concat_dataset(HighFive::DataSet & out, const HighFive::DataSet & in, const size_t out_offset) {
  // this is a bit hacky but it allows us to avoid having to write separate code for each readout type
  if (auto type = out.getDataType(); type == hdf_compound_type(ReadoutType::CAEN)) {
    concat_dataset_impl<CAEN_event>(out, in, out_offset);
  } else if (type == hdf_compound_type(ReadoutType::TTLMonitor)) {
    concat_dataset_impl<TTLMonitor_event>(out, in, out_offset);
  } else if (type == hdf_compound_type(ReadoutType::CDT)) {
    concat_dataset_impl<CDT_event>(out, in, out_offset);
  } else if (type == hdf_compound_type(ReadoutType::VMM3)) {
    concat_dataset_impl<VMM3_event>(out, in, out_offset);
  } else if (type == hdf_compound_type(ReadoutType::BM0)) {
    concat_dataset_impl<BM0_event>(out, in, out_offset);
  } else if (type == hdf_compound_type(ReadoutType::BM2)) {
    concat_dataset_impl<BM2_event>(out, in, out_offset);
  } else if (type == hdf_compound_type(ReadoutType::BMI)) {
    concat_dataset_impl<BMI_event>(out, in, out_offset);
  } else {
    throw std::runtime_error("Unsupported dataset type for concatenation");
  }
}

template<class T>
static void ensure_file_attribute(HighFive::File & file, const std::string & name, const T & expected_value) {
  if (!file.hasAttribute(name)) {
    file.createAttribute<T>(name, expected_value);
    return;
  }
  T value;
  file.getAttribute(name).read(value);
  if (value != expected_value) {
    throw std::runtime_error("File has unexpected value for attribute " + name);
  }
}


int validate_collector_file_impl(const HighFive::File & file, const std::string & filename) {
  using namespace HighFive;
  int points{0};
  if (!file.hasAttribute("program") || file.getAttribute("program").read<std::string>() != "libreadout") {
      std::cerr << "Warning: file " << filename << " does not appear to be a libreadout file." << std::endl;
      return -1;
    }
    if (!file.hasAttribute("version") || !file.hasAttribute("revision")) {
      std::cerr << "Warning: file " << filename << " does not have version information." << std::endl;
      return -1;
    }
    if (!file.hasAttribute("points")) {
      std::cerr << "Warning: file " << filename << " does not have the expected number of points." << std::endl;
      return -1;
    }
    points = file.getAttribute("points").read<int>();
    if (points < 0) {
      std::cerr << "Warning: file " << filename << " has an invalid number of points." << std::endl;
      return -1;
    }
    auto collector = file.getGroup("/");
    if (points > 0) {
      bool found{false};
      const auto member_count = file.getNumberObjects();
      for (size_t i = 0; i < member_count; ++i) {
        if (const auto name = file.getObjectName(i); file.getObjectType(name) == ObjectType::Group) {
          if (collector = file.getGroup(name);
            collector.hasAttribute("collector_type")
            && collector.getAttribute("collector_type").read<std::string>() == "point"
            && collector.hasAttribute("scan_point")) {
              if (!collector.hasAttribute("scan_point") || collector.getAttribute("scan_point").read<int>() < 0 || collector.getAttribute("scan_point").read<int>() >= points) {
                std::cerr << "Warning: file " << filename << " has a collector group with an invalid scan point." << std::endl;
                return -1;
              }
              found = true;
            }
        }
        if (!found) {
          std::cerr << "Warning: file " << filename << " does not have a collector group for any scan points." << std::endl;
          return -1;
        }
      }
    }
    // verify that any datasets in the collector group have the expected attributes and dimensions:
    for (size_t i=0; i<collector.getNumberObjects(); ++i) {
      const auto name = collector.getObjectName(i);
      if (collector.getObjectType(name) == ObjectType::Dataset && collector.getDataSet(name).hasAttribute("detector") && collector.getDataSet(name).hasAttribute("readout")) {
        auto dims = collector.getDataSet(name).getDimensions();
        if (dims.size() != 1) {
          std::cerr << "Warning: dataset " << name << " in file " << filename << " is not 1-dimensional." << std::endl;
          return -1;
        }
      } else if (collector.getObjectType(name) == ObjectType::Group && name == "parameters") {
        // ensure that all parameters are scalars:
        auto param_group = collector.getGroup("parameters");
        for (size_t j=0; j< param_group.getNumberObjects(); ++j) {
          const auto param_name = param_group.getObjectName(j);
          if (param_group.getObjectType(param_name) != ObjectType::Dataset) {
            std::cerr << "Warning: parameter " << param_name << " in collector group of file " << filename << " is not a dataset." << std::endl;
            return -1;
          }
          auto dims = param_group.getDataSet(param_name).getDimensions();
          if (dims.size() != 0 || (dims.size() == 1 && dims.front() != 1)) {
            std::cerr << "Warning: parameter " << param_name << " in collector group of file " << filename << " is not scalar." << std::endl;
            return -1;
          }
        }
      } else {
        std::cerr << "Warning: node " << name << " in collector group of file " << filename << " is not expected." << std::endl;
        return -1;
      }
    }
  return points;
}

int validate_collector_file(const std::string & filename) {
  using namespace HighFive;
  int points{0};
  try {
    const File file(filename, File::ReadOnly);
    points = validate_collector_file_impl(file, filename);
  } catch (Exception & ex) {
    std::cerr << "Error reading file " << filename << ": " << ex.what() << std::endl;
    return -1;
  }
  return points;
}

bool validate_collector_files(
  const std::vector<std::string> & in_filenames, const int point, const int points,
  std::set<std::string> & datasets,
  std::map<std::string, size_t> & sizes,
  std::set<std::string> & valid_files,
  std::string & collector_name,
  std::set<std::string> & parameters
  ) {
  using namespace HighFive;
  bool first{true};
  for (const auto & in_file : in_filenames) {
    try {
      File file(in_file, File::ReadOnly);
      auto file_points = validate_collector_file_impl(file, in_file);
      if (file_points < 0 || (points > 0 && file_points != points)) {
        continue;
      }
      auto collector = file.getGroup("/");
      if (points > 0) {
        bool found{false};
        const auto member_count = file.getNumberObjects();
        for (size_t i = 0; i < member_count; ++i) {
          if (const auto name = file.getObjectName(i); file.getObjectType(name) == ObjectType::Group) {
            if (collector = file.getGroup(name);
              collector.hasAttribute("collector_type")
              && collector.getAttribute("collector_type").read<std::string>() == "point"
              && collector.hasAttribute("scan_point")
              && collector.getAttribute("scan_point").read<int>() == point
              ) {
              // this is the collector group for the point we're merging, so we can stop looking for groups
              found = true;
              if (first) collector_name = name;
              break;
            }
          }
        }
        if (!found) {
          std::cerr << "Warning: file " << in_file << " does not have a collector group for the expected scan point, skipping." << std::endl;
          continue;
        }
      }
      bool all_datasets_valid{true};
      bool all_parameters_valid{true};
      for (size_t i=0; i<collector.getNumberObjects(); ++i) {
        const auto name = collector.getObjectName(i);
        if (collector.getObjectType(name) == ObjectType::Dataset && collector.getDataSet(name).hasAttribute("detector") && collector.getDataSet(name).hasAttribute("readout")) {
          if (first) {
            datasets.insert(name);
            sizes[name] = 0;
          } else if (!datasets.contains(name)) {
            std::cerr << "Warning: file " << in_file << " has a dataset " << name << " which is not present in previous files, skipping." << std::endl;
            all_datasets_valid = false;
            break;
          }
          auto dims = collector.getDataSet(name).getDimensions();
          if (dims.size() != 1) {
            std::cerr << "Warning: dataset " << name << " in file " << in_file << " is not 1-dimensional, skipping." << std::endl;
            all_datasets_valid = false;
            break;
          }
          sizes[name] += dims.front();
        } else if (collector.getObjectType(name) == ObjectType::Group && name == "parameters") {
          auto param_group = collector.getGroup("parameters");
          if (first) {
            for (size_t j=0; j< param_group.getNumberObjects(); ++j) {
              parameters.insert(param_group.getObjectName(j));
            }
          } else {
            for (size_t j=0; j< param_group.getNumberObjects(); ++j) {
              const auto param_name = param_group.getObjectName(j);
              if (!parameters.contains(param_name)) {
                std::cerr << "Warning: file " << in_file << " has a parameter " << param_name << " which is not present in previous files, skipping." << std::endl;
                all_parameters_valid = false;
                break;
              }
            }
          }
        }
      }
      if (all_datasets_valid and all_parameters_valid) {
        valid_files.insert(in_file);
        first = false;
      }
    } catch (Exception & ex) {
      std::cerr << "Error reading file " << in_file << ": " << ex.what() << ", skipping." << std::endl;
    }
  }

  if (datasets.empty()) {
    std::cerr << "No valid input files found, cannot merge!" << std::endl;
    return false;
  }
  if (valid_files.empty()) {
    std::cerr << "No valid input files found, cannot merge!" << std::endl;
    return false;
  }
  return true;
}



void merge_collector_files(const std::string & out_filename, const std::vector<std::string> & in_filenames, const int point, const int points) {
  using namespace HighFive;

  std::set<std::string> datasets;
  std::map<std::string, size_t> sizes;
  std::set<std::string> valid_files;
  std::string collector_name{"/"};
  std::set<std::string> parameters;
  if (!validate_collector_files(in_filenames, point, points, datasets, sizes, valid_files, collector_name, parameters)) {
    std::cerr << "No valid input files found, cannot merge!" << std::endl;
    return;
  }

  try {
    bool first{true};
    File out_file(out_filename, File::OpenOrCreate);
    auto u8str = [](const auto * p){return std::string(reinterpret_cast<const char *>(p));};
    ensure_file_attribute<std::string>(out_file, "program", u8str("libreadout"));
    ensure_file_attribute<std::string>(out_file, "version", u8str(libreadout::version::version_number));
    ensure_file_attribute<std::string>(out_file, "revision", u8str(libreadout::version::git_revision));
    ensure_file_attribute<int>(out_file, "points", points);

    auto out_collector = out_file.getGroup("/");
    if (points > 0) {
      out_collector = out_file.createGroup(collector_name);
      out_collector.createAttribute("collector_type", "point");
      out_collector.createAttribute<int>("scan_point", point);
    }
    first = true;
    for (const auto & in_file : valid_files) {
      File file(in_file, File::ReadOnly);
      auto in_collector = file.getGroup(collector_name);

      if (first && parameters.size() > 0) {
        auto out_param_group = out_collector.createGroup("parameters");
        for (const auto & param_name : parameters) {
          auto in_param_ds = in_collector.getGroup("parameters").getDataSet(param_name);
          auto datatype = in_param_ds.getDataType();
          if (datatype.isVariableStr()) {
            // convert to fixed-length string type for output file, since variable-length strings can cause issues when reading with some libraries
            datatype = create_datatype<std::string>();
          }
          auto dataclass = datatype.getClass();
          if (DataTypeClass::Float == dataclass) {
            // for floating-point types, we want to make sure to use the standard 32-bit or 64-bit IEEE format in the output file for maximum compatibility, since some libraries have trouble with non-standard formats
            if (datatype.getSize() == 4) {
              datatype = create_datatype<float>();
            } else if (datatype.getSize() == 8) {
              datatype = create_datatype<double>();
            } else {
              std::cerr << "Warning: dataset " << param_name << " in file " << in_file << " has a floating-point type with non-standard size, which may cause problems when merging." << std::endl;
            }
          } else if (DataTypeClass::Integer == dataclass) {
            // for integer types, we want to make sure to use a standard size in the output file for maximum compatibility, since some libraries have trouble with non-standard sizes. We'll convert to 64-bit integers to be safe, since it's unlikely that we'll have parameters with values that don't fit in 64 bits.
            if (datatype.getSize() <= 8) {
              datatype = create_datatype<int64_t>();
            } else {
              std::cerr << "Warning: dataset " << param_name << " in file " << in_file << " has an integer type with non-standard size, which may cause problems when merging." << std::endl;
            }
          } else if (DataTypeClass::Compound == dataclass) {
            std::cerr << "Warning: parameter dataset " << param_name << " in file " << in_file << " has a compound type, which may cause problems when merging." << std::endl;
          }
          auto out_param_ds = out_param_group.createDataSet(param_name, in_param_ds.getSpace(), datatype);
          if (dataclass == DataTypeClass::Float && datatype.getSize() == 4) {
            out_param_ds.write(in_param_ds.read<float>());
          } else if (dataclass == DataTypeClass::Float && datatype.getSize() == 8) {
            out_param_ds.write(in_param_ds.read<double>());
          } else if (dataclass == DataTypeClass::Integer && datatype.getSize() == 8) {
            out_param_ds.write(in_param_ds.read<int64_t>());
          } else if (dataclass == DataTypeClass::String) {
            out_param_ds.write(in_param_ds.read<std::string>());
          } else {
            std::cerr << "Warning: parameter dataset " << param_name << " in file " << in_file << " has a data type that we can not copy (yet)." << std::endl;
          }

          if (in_param_ds.hasAttribute("unit")) {
            out_param_ds.createAttribute("unit", in_param_ds.getAttribute("unit").read<std::string>());
          }
          if (in_param_ds.hasAttribute("description")) {
            out_param_ds.createAttribute("description", in_param_ds.getAttribute("description").read<std::string>());
          }
        }
      }

      for (const auto & dataset_name : datasets) {
        auto in_data = in_collector.getDataSet(dataset_name);
        if (first) {
          // create the output datasets with the known total size
          DataSpace dataspace({0}, {sizes[dataset_name]});
          auto datatype = in_data.getDataType();
          // Do we need to worry about copying the compound datatype's committed name and/or attributes here?
          // Maybe not, since the name is only used for referencing the type within the file, and the attributes are not required for reading/writing data.
          // But if we do need to copy them, we can get them from the input file's type and commit a new type with the same properties to the output file before creating the dataset.

          auto readout_type_name = in_data.getAttribute("readout").read<std::string>();
          auto readout_type = readoutType_from_name(readout_type_name);
          auto expected_type = hdf_compound_type(readout_type);
          if (datatype != expected_type) {
            std::cerr << "Warning: dataset " << dataset_name << " in file " << in_file << " has data type which does not match the expected type for its readout. This may cause problems when merging." << std::endl;
          }
          ensure_file_data_type(out_file, readout_type_name, expected_type);
          out_collector.createDataSet(dataset_name, dataspace, datatype);
        }
        auto out_data = out_collector.getDataSet(dataset_name);
        auto in_dims = in_data.getDimensions();
        auto out_dims = out_data.getDimensions();
        if (in_dims.size() != 1 || out_dims.size() != 1) {
          std::cerr << "Warning: dataset " << dataset_name << " in file " << in_file << " or output dataset is not 1-dimensional, skipping." << std::endl;
          continue;
        }
        auto in_size = in_dims.front();
        auto out_size = out_dims.front();
        if (out_data.getSpace().getMaxDimensions().front() < out_size + in_size) {
          std::cerr << "Warning: dataset " << dataset_name << " in output file does not have enough capacity to hold all data from file " << in_file << ", skipping." << std::endl;
          continue;
        }
        out_data.resize({out_size + in_size});
        concat_dataset(out_data, in_data, out_size);
      }
      first = false;
    }
    // done merging, flush the output file to ensure all data is written to disk
    out_file.flush();
  } catch (Exception & ex) {
    std::cerr << "Error writing output file " << out_filename << ": " << ex.what() << std::endl;
  }
}