#include "CollectorClass.h"

#include <filesystem>

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
    std::stringstream s;
    s << "File has unexpected value for attribute " << name << ": expected " << expected_value << " but got " << value;
    std::cerr << std::endl << s.str() << std::endl << std::endl;
    std::cerr << " Are you appending to a mismatched file?" << std::endl;
    std::cerr << " If so, you should create a new file instead." << std::endl << std::endl;
    throw std::runtime_error(s.str());
  }
}

void ensure_file_attributes(HighFive::File & file, const int points) {
  using C=CollectorSink;
  ensure_file_attribute<std::string>(file, C::program_attribute_name(), C::program_attribute_value());
  ensure_file_attribute<std::string>(file, C::version_attribute_name(), C::version_attribute_value());
  ensure_file_attribute<std::string>(file, C::revision_attribute_name(), C::revision_attribute_value());
  ensure_file_attribute<int>(file, C::total_points_attribute_name(), points);
}

void ensure_collector_group_attributes(HighFive::Group & group, const int point) {
  using C=CollectorSink;
  if (!group.hasAttribute(C::type_attribute_name())) {
    group.createAttribute<std::string>(C::type_attribute_name(), C::type_attribute_value());
  }
  if (!group.hasAttribute(C::point_attribute_name())) {
    group.createAttribute<int>(C::point_attribute_name(), point);
  }
}

void ensure_dataset_attributes(HighFive::DataSet & dataset, const DetectorType detector, const ReadoutType readout) {
  using C=CollectorSink;
  if (!dataset.hasAttribute(C::detector_attribute_name())) {
    dataset.createAttribute<std::string>(C::detector_attribute_name(), detectorType_name(detector));
  }
  if (!dataset.hasAttribute(C::readout_attribute_name())) {
    dataset.createAttribute<std::string>(C::readout_attribute_name(), readoutType_name(readout));
  }
  if (!dataset.hasAttribute(C::weight_attribute_name())) {
    dataset.createAttribute<double>(C::weight_attribute_name(), 0.0);
  }
}

std::string validate_is_collector_group(const HighFive::Group & group, const std::optional<int> expected_point, const std::optional<int> total_points) {
  using C=CollectorSink;
  const auto & tan = C::type_attribute_name();
  const auto & tav = C::type_attribute_value();
  const auto & pan = C::point_attribute_name();
  // without total points specified, the group is _probably_ the root group of a non-point-based collector,
  // so we don't require the type and point attributes, but if total points is specified, then we require
  // the group to have the correct type and point attributes, and if expected_point is also specified,
  // we require the point attribute to match it
  if (total_points.has_value()) {
    if (!group.hasAttribute(tan) || group.getAttribute(tan).read<std::string>() != tav || !group.hasAttribute(pan)) {
      return "Missing required collector attributes";
    }
    if (const auto n = group.getAttribute(pan).read<int>(); n < 0 || n >= total_points.value()) {
      return "Out of bounds scan point in collector group";
    }
    if (expected_point.has_value() && group.getAttribute(pan).read<int>() != expected_point.value()) {
      return "Unexpected scan point in collector group";
    }
  }
  return {};
}

std::string validate_collector_group(const HighFive::Group & group, const std::optional<int> expected_point, const std::optional<int> total_points) {
  using namespace HighFive;
  using C=CollectorSink;
  if (auto result = validate_is_collector_group(group, expected_point, total_points); !result.empty()) {
    return result;
  }
  // validate the nodes in the group
  // verify that any datasets in the collector group have the expected attributes and dimensions:
  const auto object_count = group.getNumberObjects();
  std::stringstream message;
  const auto dan = C::detector_attribute_name();
  const auto ran = C::readout_attribute_name();
  const auto pgn = C::parameter_group_name();
  for (size_t i=0; i<object_count; ++i) {
    const auto name = group.getObjectName(i);
    if (group.getObjectType(name) == ObjectType::Dataset && group.getDataSet(name).hasAttribute(dan) && group.getDataSet(name).hasAttribute(ran)) {
      if (auto dims = group.getDataSet(name).getDimensions(); dims.size() != 1) {
        message << "Dataset " << name << " in collector group is not 1-dimensional." << std::endl;
      }
    } else if (group.getObjectType(name) == ObjectType::Group && name == pgn) {
      // ensure that all parameters are scalars:
      auto param_group = group.getGroup(pgn);
      for (size_t j=0; j< param_group.getNumberObjects(); ++j) {
        const auto param_name = param_group.getObjectName(j);
        if (param_group.getObjectType(param_name) != ObjectType::Dataset) {
          message << "Node " << param_name << " in parameters is not a dataset." << std::endl;
        } else if (auto dims = param_group.getDataSet(param_name).getDimensions(); dims.size() != 0 || (dims.size() == 1 && dims.front() != 1)) {
          message << "Parameter " << param_name << " is not scalar." << std::endl;
        }
      }
    } else {
      message << "Unexpected node " << name << " in collector group" << std::endl;
    }
  }
  return message.str();
}


int validate_collector_file_impl(const HighFive::File & file, const std::string & filename) {
  using C=CollectorSink;
  using namespace HighFive;
  int points{0};
  if (const auto n = C::program_attribute_name();
    !file.hasAttribute(n) || file.getAttribute(n).read<std::string>() != C::program_attribute_value()
    ) {
    std::cerr << "Warning: file " << filename << " does not appear to be a libreadout file." << std::endl;
    return -1;
  }
  if (!file.hasAttribute(C::version_attribute_name()) || !file.hasAttribute(C::revision_attribute_name())) {
    std::cerr << "Warning: file " << filename << " does not have version information." << std::endl;
    return -1;
  }
  if (!file.hasAttribute(C::total_points_attribute_name())) {
    std::cerr << "Warning: file " << filename << " does not have the expected number of points." << std::endl;
    return -1;
  }
  points = file.getAttribute(C::total_points_attribute_name()).read<int>();
  if (points < 0) {
    std::cerr << "Warning: file " << filename << " has an invalid number of points." << std::endl;
    return -1;
  }
  if (points > 0) {
    bool found{false};
    const auto member_count = file.getNumberObjects();
    for (size_t i = 0; i < member_count; ++i) {
      if (const auto name = file.getObjectName(i); file.getObjectType(name) == ObjectType::Group) {
        if (auto result = validate_collector_group(file.getGroup(name), std::nullopt, points); !result.empty()) {
          std::cerr << "Warning: file " << filename << " has a collector group with unexpected structure: " << std::endl << result;
          return -1;
        }
      }
      found = true;
    }
    if (!found) {
      std::cerr << "Warning: file " << filename << " does not have a collector group for any scan points." << std::endl;
      return -1;
    }
  } else {
    if (const auto result = validate_collector_group(file.getGroup("/")); !result.empty()) {
      std::cerr << "Warning: file " << filename << " has a root group with unexpected structure: " << std::endl << result;
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

bool validate_collector_files_datasets(
  const std::vector<std::string> & in_filenames, const int point, const int points,
  std::optional<std::string> expected_dataset,
  std::set<std::string> & datasets,
  std::map<std::string, size_t> & sizes,
  std::set<std::string> & valid_files,
  std::string & collector_name
  ) {
  using C=CollectorSink;
  using namespace HighFive;
  bool first{true};
  for (const auto & in_file : in_filenames) {
    try {
      File file(in_file, File::ReadOnly);
      if (const auto n = validate_collector_file_impl(file, in_file); n < 0 || (points > 0 && n != points)) {
        continue;
      }
      auto collector = file.getGroup("/");
      if (points > 0) {
        bool found{false};
        const auto member_count = file.getNumberObjects();
        for (size_t i = 0; i < member_count; ++i) {
          if (const auto name = file.getObjectName(i); file.getObjectType(name) == ObjectType::Group) {
            // check if the group is a collector group and the point number matches the point we're merging
            if (validate_collector_group(file.getGroup(name), point, points).empty()) {
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
        collector = file.getGroup(collector_name);
      }
      bool all_datasets_valid{true};
      const auto dan = C::detector_attribute_name();
      const auto ran = C::readout_attribute_name();
      for (size_t i=0; i<collector.getNumberObjects(); ++i) {
        if (const auto name = collector.getObjectName(i);
          collector.getObjectType(name) == ObjectType::Dataset
          && collector.getDataSet(name).hasAttribute(dan) && collector.getDataSet(name).hasAttribute(ran)
          && (!expected_dataset.has_value() || name == expected_dataset.value())
          ) {
          if (first) {
            datasets.insert(name);
            sizes[name] = 0;
          } else if (!datasets.contains(name)) {
            std::cerr << "Warning: file " << in_file << " has a dataset " << name << " which is not present in previous files, skipping." << std::endl;
            all_datasets_valid = false;
            break;
          }
          const auto dims = collector.getDataSet(name).getDimensions();
          if (dims.size() != 1) {
            std::cerr << "Warning: dataset " << name << " in file " << in_file << " is not 1-dimensional, skipping." << std::endl;
            all_datasets_valid = false;
            break;
          }
          sizes[name] += dims.front();
        }
      }
      if (all_datasets_valid) {
        valid_files.insert(in_file);
        first = false;
      } else if (first) {
        // ensure we don't leak invalid dataset information from pre-first file(s)
        datasets.clear();
        sizes.clear();
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

bool validate_collector_files_parameters(
  const std::vector<std::string> & in_filenames, const int point, const int points,
  std::set<std::string> & valid_files,
  std::string & collector_name,
  std::set<std::string> & parameters
  ) {
  using C=CollectorSink;
  using namespace HighFive;
  bool first{true};
  for (const auto & in_file : in_filenames) {
    try {
      File file(in_file, File::ReadOnly);
      if (const auto n = validate_collector_file_impl(file, in_file); n < 0 || (points > 0 && n != points)) {
        continue;
      }
      auto collector = file.getGroup("/");
      if (points > 0) {
        bool found{false};
        const auto member_count = file.getNumberObjects();
        for (size_t i = 0; i < member_count; ++i) {
          if (const auto name = file.getObjectName(i); file.getObjectType(name) == ObjectType::Group) {
            if (validate_is_collector_group(file.getGroup(name), point, points).empty()) {
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
      bool all_parameters_valid{true};
      const auto pgn = C::parameter_group_name();
      for (size_t i=0; i<collector.getNumberObjects(); ++i) {
        const auto name = collector.getObjectName(i);
        if (collector.getObjectType(name) == ObjectType::Group && name == pgn) {
          auto param_group = collector.getGroup(pgn);
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
      if (all_parameters_valid) {
        valid_files.insert(in_file);
        first = false;
      }
    } catch (Exception & ex) {
      std::cerr << "Error reading file " << in_file << ": " << ex.what() << ", skipping." << std::endl;
    }
  }
  if (valid_files.empty()) {
    std::cerr << "No valid input files found, cannot merge!" << std::endl;
    return false;
  }
  return true;
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

  if (!validate_collector_files_datasets(in_filenames, point, points, std::nullopt, datasets, sizes, valid_files, collector_name)) {
    std::cerr << "No valid input files found, cannot merge!" << std::endl;
    return false;
  }

  std::set<std::string> files_with_valid_parameters;
  std::string parameter_collector_name{"/"};
  if (!validate_collector_files_parameters(in_filenames, point, points, files_with_valid_parameters, parameter_collector_name, parameters)) {
    std::cerr << "No valid input files found, cannot merge!" << std::endl;
    return false;
  }
  if (collector_name != parameter_collector_name) {
    std::cerr << "The collector group containing datasets, "<< collector_name
              << ", does not match the collector group containing parameters, " << parameter_collector_name
              << std::endl;
    return false;
  }
  // verify that the valid file sets are identical
  if (valid_files != files_with_valid_parameters) {
    std::cerr << "The set of valid files for datasets does not match the set of valid files for parameters, cannot merge!" << std::endl;
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
    ensure_file_attributes(out_file, points);

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

static HighFive::Group open_or_create_collector_group(HighFive::File & file, const std::string & collector_name, const int point, const int points) {
  using C=CollectorSink;
  if (points <= 0) {
    return file.getGroup("/");
  }
  if (file.exist(collector_name) && file.getObjectType(collector_name) == HighFive::ObjectType::Group) {
    const auto result = validate_is_collector_group(file.getGroup(collector_name), point, points);
    if (result.empty()) {
      return file.getGroup(collector_name);
    }
    std::stringstream ss;
    ss << "Existing group " << collector_name << " in output file does not have the expected attributes for a collector group." << std::endl;
    ss << result;
    throw std::runtime_error(ss.str());
  }
  auto collector = file.createGroup(collector_name);
  collector.createAttribute(C::type_attribute_name(), C::type_attribute_value());
  collector.createAttribute<int>(C::point_attribute_name(), point);
  return collector;
}


void merge_collector_datasets(const std::string & out_filename, const std::vector<std::string> & in_filenames, int point, int points, const std::string & which_dataset, const bool remove_after_merge) {
  using C=CollectorSink;
  using namespace HighFive;

  std::set<std::string> datasets;
  std::map<std::string, size_t> sizes;
  std::set<std::string> valid_files;
  std::string collector_name{"/"};
  std::optional expected_dataset{which_dataset.empty() ? std::nullopt : std::make_optional(which_dataset)};
  if (!validate_collector_files_datasets(in_filenames, point, points, expected_dataset, datasets, sizes, valid_files, collector_name)) {
    std::cerr << "No valid input files found, cannot merge!" << std::endl;
    return;
  }
  if (expected_dataset.has_value() && std::ranges::find(datasets, expected_dataset.value()) == datasets.end()) {
    std::cerr << "Dataset " << expected_dataset.value() << " not found in valid input files, cannot merge!" << std::endl;
    return;
  }

  try {
    bool first{true};
    File out_file(out_filename, File::OpenOrCreate);
    ensure_file_attributes(out_file, points);

    auto out_collector = open_or_create_collector_group(out_file, collector_name, point, points);
    for (const auto & in_file : valid_files) {
      File file(in_file, remove_after_merge ? File::ReadWrite : File::ReadOnly);
      auto in_collector = file.getGroup(collector_name);

      for (const auto & dataset_name : datasets) {
        auto in_data = in_collector.getDataSet(dataset_name);
        if (first && !out_collector.exist(dataset_name)) {
          // create the output datasets with the known total size
          DataSpace dataspace({0}, {sizes[dataset_name]});
          auto datatype = in_data.getDataType();

          auto readout_type_name = in_data.getAttribute(C::readout_attribute_name()).read<std::string>();
          auto readout_type = readoutType_from_name(readout_type_name);
          auto expected_type = hdf_compound_type(readout_type);
          if (datatype != expected_type) {
            std::cerr << "Warning: dataset " << dataset_name << " in file " << in_file << " has data type which does not match the expected type for its readout. This may cause problems when merging." << std::endl;
          }
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
        // accumulate the dataset's weight attribute
        if (const auto wan = C::weight_attribute_name(); in_data.hasAttribute(wan)) {
          auto weight = in_data.getAttribute(wan).read<double>();
          if (out_data.hasAttribute(wan)) {
            weight += out_data.getAttribute(wan).read<double>();
            out_data.deleteAttribute(wan); // unnecessary?
          }
          out_data.createAttribute(wan, weight);
        }
        if (remove_after_merge) {
          // unlinking may or may not actually free any disk space, and likely needs to be followed by h5repack
          // in_collector.unlink(dataset_name);
          // instead, let's just reset the size of the dataset to 0 to avoid remerging the same data again
          // if there is no further writing to the file, h5repack would also shrink its size on disk
          in_data.resize({0});
        }
      }
      first = false;
      if (remove_after_merge) {
        file.flush();
      }
    }
    // done merging, flush the output file to ensure all data is written to disk
    out_file.flush();
  } catch (Exception & ex) {
    std::cerr << "Error writing output file " << out_filename << ": " << ex.what() << std::endl;
  }

}

void copy_collector_parameters(const std::string & out_filename, const std::vector<std::string> & in_filenames, const int point, const int points) {
  using namespace HighFive;

  std::set<std::string> valid_files;
  std::string collector_name{"/"};
  std::set<std::string> parameters;
  if (!validate_collector_files_parameters(in_filenames, point, points, valid_files, collector_name, parameters)) {
    std::cerr << "No valid input files found, cannot merge!" << std::endl;
    return;
  }
  if (parameters.empty()) {
    return;
  }

  try {
    File out_file(out_filename, File::OpenOrCreate);
    ensure_file_attributes(out_file, points);

    auto out_collector = open_or_create_collector_group(out_file, collector_name, point, points);

    // Pick any of the valid files to copy from, since we've already verified that they all have the same parameters with compatible types and attributes.
    const auto & in_file = *valid_files.begin();
    File file(in_file, File::ReadOnly);
    auto in_parameters = file.getGroup(collector_name).getGroup("parameters");

    if (!out_collector.exist("parameters")) {
      auto out_param_group = out_collector.createGroup("parameters");
      for (const auto & param_name : parameters) {
        auto in_param_ds = in_parameters.getDataSet(param_name);
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
      // done copying parameters, flush the output file to ensure all data is written to disk
      out_file.flush();
    }
  } catch (Exception & ex) {
    std::cerr << "Error writing output file " << out_filename << ": " << ex.what() << std::endl;
  }

  // validate that the parameters were copied (or pre-exising parameters matched)
  std::set<std::string> post_copy_valid_files;
  std::string post_copy_collector_name{"/"};
  std::set<std::string> post_copy_parameters;
  if (!validate_collector_files_parameters({out_filename}, point, points, post_copy_valid_files, post_copy_collector_name, post_copy_parameters)) {
    std::cerr << "Error validating output file " << out_filename << " after copying parameters!" << std::endl;
    return;
  }
  if (post_copy_collector_name != collector_name) {
    std::cerr << "Error validating output file " << out_filename << " after copying parameters: collector group name does not match expected name!" << std::endl;
    return;
  }
  if (post_copy_parameters != parameters) {
    std::cerr << "Error validating output file " << out_filename << " after copying parameters: parameter set does not match expected set!" << std::endl;
    return;
  }

}

/// \brief Generate a filename suitable for use with the CollectorSink class
///
/// \param basepath the base path to the directory where the file will be created
/// \param basename the base name to use for the file
/// \param extension the file extension to use (with or without the dot, e.g. "h5" or ".h5")
///
/// If the basepath is empty the resulting filename will be defined relative to the current working directory.
/// The basename can contain directory components, which will alter the resulting filename.
/// The extension can include extra 'sub-extensions' (e.g. "tar.gz") if desired, but the final extension of the resulting filename will be exactly what is provided in this argument, with no additional extensions added.
/// If the basename is an absolute path, the resulting filename will be <basename>.h5 and the basepath will be ignored.
/// If the basename is a relative path, the resulting filename will be <basepath>/<basename>.h5 (if basepath is not empty) or <basename>.h5 (if basepath is empty).
std::string filename_for_collector(const std::string & basepath, const std::string & basename, const std::string & extension ) {
  std::filesystem::path path(basename);
  if (path.is_absolute()) {
    return path.replace_extension(extension).string();
  }
  std::filesystem::path base(basepath);
  return (base / path).replace_extension(extension).string();
}
std::string filename_for_collector_node(const std::string & basepath, const std::string & basename, const int node, const int nodes) {
  std::stringstream ss;
  ss << "n" << std::setfill('0') << std::setw(static_cast<int>(std::to_string(nodes-1).length())) << node << ".h5";
  return filename_for_collector(basepath, basename, ss.str());
}