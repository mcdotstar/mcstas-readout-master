#include "CollectorClass.h"

#include <filesystem>
#include <ranges>
#include <set>

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

class CollectorShape {
  using readouts_t = std::map<std::string, std::vector<uint32_t>>;
  public:
  std::optional<size_t> points; // number of points, as defined in the parameters group entries
  std::set<std::string> parameters = {}; // dataset names in the parameters group
  readouts_t readouts = {}; // collector group names and their number of readouts per point

  size_t total_readouts() const {
    size_t total{0};
    for (const auto &counts: readouts | std::views::values) {
      total += std::ranges::fold_left(counts, 0u, std::plus<size_t>());
    }
    return total;
  }
  uint32_t total_readouts(const std::string & name) const {
    if (readouts.contains(name)) {
      return std::ranges::fold_left(readouts.at(name), 0u, std::plus<uint32_t>());
    }
    return 0;
  }

  std::string insert_readouts_from_cues(const std::string & name, const HighFive::DataSet & cues) {
    if (!points.has_value()) points = cues.getDimensions().back();
    else if (cues.getDimensions().back() != points.value()) return "wrong number of readout points";
    if (readouts.contains(name)) return "readouts already inserted";
    readouts[name] = std::vector<uint32_t>();
    readouts.at(name).resize(points.value());
    // read the end-cues for each segment of the array: [N0, N0+N1, N0+N1+N2, ..., len(readouts)]
    cues.select({0u}, {points.value()}).read(readouts.at(name));
    // convert to the number of readouts corresponding to each point
    uint32_t offset{0};
    for (auto & value: readouts.at(name)) {
      const auto current = value;
      value -= offset;
      offset = current;
    }
    return {};
  }

  bool insert_readouts_from_offsets(const std::string & name, const std::vector<uint32_t> & offsets) {
    if (readouts.contains(name)) return false;
    readouts[name] = std::vector<uint32_t>();
    readouts.at(name).resize(offsets.size());
    uint32_t offset{0};
    for (const auto & current: offsets) {
      readouts.at(name).push_back(current - offset);
      offset = current;
    }
    return true;
  }

  CollectorShape combine(const CollectorShape & other) const {
    if (points != other.points || parameters != other.parameters) {
      return CollectorShape(0);
    }
    CollectorShape out(points, parameters);
    for (const auto & [name, rpp]: readouts) {
      out.readouts[name] = std::vector(rpp.begin(), rpp.end());
    }
    for (const auto & [name, rpp]: other.readouts) {
      if (out.readouts.contains(name)) {
        for (size_t i=0; i<points.value(); ++i) {
          out.readouts.at(name).at(i) += rpp.at(i);
        }
      } else {
        out.readouts[name] = std::vector(rpp.begin(), rpp.end());
      }
    }
    return out;
  }

  CollectorShape null_readouts() const {
    CollectorShape out(points, parameters);
    for (const auto & [name, rpp]: readouts) {
      out.readouts[name] = std::vector(rpp.size(), 0u);
      if (!rpp.empty()) {
        // cumulative sum, e.g., partial_sum prepending a 0 and skipping the last value
        for (size_t i=0; i<rpp.size()-1; ++i) {
          out.readouts.at(name).at(i+1) = out.readouts.at(name).at(i) + rpp.at(i);
        }
      }
    }
    return out;
  }

  bool has_all_readouts(const std::set<std::string> & names) const {
    // names should be the set of all readout keys from multiple CollectorShape objects
    // here we *only* check if there are any named readout groups in names that are not in readouts
    for (const auto & name: names) {
      if (!readouts.contains(name)) return false;
    }
    return true;
  }

  CollectorShape concatenate(const CollectorShape & other) const {
    if (parameters != other.parameters) {
      return CollectorShape(0);
    }
    CollectorShape out(points.value() + other.points.value(), parameters);
    // readouts get concatenated along the point dimension, which means concatenate(std::vector, std::vector)
    // we should have already verified that all concatenated objects have identical readout keys
    for (const auto & name: readouts | std::views::keys) {
      out.readouts[name] = std::vector<uint32_t>();
      out.readouts.at(name).reserve(readouts.at(name).size() + other.readouts.at(name).size());
      out.readouts.at(name).append_range(readouts.at(name));
      out.readouts.at(name).append_range(other.readouts.at(name));
    }
    return out;
  }
};
CollectorShape concatenate_shapes(const std::vector<const CollectorShape *> & shapes) {
  const auto points = std::transform_reduce(shapes.begin(), shapes.end(), 0u, std::plus<size_t>(), [](const auto & a){return a->points.value();});
  CollectorShape out(points, shapes.front()->parameters);
  for (const auto & name: shapes.front()->readouts | std::views::keys) {
    out.readouts[name] = std::vector<uint32_t>();
    const auto length = std::transform_reduce(shapes.begin(), shapes.end(), 0u, std::plus<size_t>(), [&name](const auto & a){return a->readouts.at(name).size();});
    out.readouts.at(name).reserve(length);
    for (const auto & shape: shapes) {
      out.readouts.at(name).append_range(shape->readouts.at(name));
    }
  }
  return out;
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

static void concat_dataset(const HighFive::DataSet & out, const size_t out_offset, const HighFive::DataSet & in, const std::optional<size_t> in_offset = std::nullopt, const std::optional<size_t> in_count = std::nullopt) {
  const size_t offset = in_offset.value_or(0u);
  const size_t count = in_count.value_or(in.getDimensions().front() - offset);
  const auto type = out.getDataType();
  std::vector<uint8_t> buffer(count * type.getSize());
  in.select({offset}, {count}).read_raw(buffer.data(), type);
  out.select({out_offset}, {count}).write_raw(buffer.data(), type);
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

void ensure_file_attributes(HighFive::File & file) {
  using C=CollectorSink;
  ensure_file_attribute<std::string>(file, C::program_attribute_name(), C::program_attribute_value());
  ensure_file_attribute<std::string>(file, C::version_attribute_name(), C::version_attribute_value());
  ensure_file_attribute<std::string>(file, C::revision_attribute_name(), C::revision_attribute_value());
}

template<class T>
std::string validate_file_attribute(HighFive::File & file, const std::string & name, const T & expected_value) {
  T value;
  file.getAttribute(name).read(value);
  if (value != expected_value) {
    std::stringstream s;
    s << "File has unexpected value for attribute " << name << ": expected " << expected_value << " but got " << value;
    std::cerr << std::endl << s.str() << std::endl << std::endl;
    std::cerr << " Are you appending to a mismatched file?" << std::endl;
    std::cerr << " If so, you should create a new file instead." << std::endl << std::endl;
   return s.str();
  }
  return {};
}

std::string validate_file_attributes(HighFive::File & file) {
  using C=CollectorSink;
  std::vector<std::pair<const std::string &, const std::string &>> pairs = {
    {C::program_attribute_name(), C::program_attribute_value()},
    {C::version_attribute_name(), C::version_attribute_value()},
    {C::revision_attribute_name(), C::revision_attribute_value()}
  };

  for (const auto & [name, value]: pairs) {
    if (const auto & ret = validate_file_attribute(file, name, value); ret.size() > 0) {
      return ret;
    }
  }

  return {};
}

std::string verify_collector_files(const std::vector<std::string> & filenames) {
  for (const auto & filename: filenames) {
    try {
      auto file = HighFive::File(filename, HighFive::File::ReadOnly);
      if (auto res = validate_file_attributes(file); res.size() > 0) return res;
    } catch (std::exception ex) {
      std::stringstream res;
      res << " Error validating " << filename << ". Exception raised: " << ex.what();
      return res.str();
    }
  }
  return {};
}

/** \brief Check whether a set of files contain consistent parameter sets
 *
 * A parameter is a dataset under /parameters with a dataset name given by the name of the parameter.
 * The parameter dataset must be 1-D, and can have any N>0 number of elements corresponding to N scan points.
 * Consistent parameters share the same name, datatype, units attribute and comment attribute.
 * All parameters in a single file *must* have the same N. Additionally all parameters across all files
 * can be required to have the singular N if `equal_length_required` is `true`.
 *
 * @param filenames *list of collector output files which should have consistent parameter sets*
 * @param equal_length_required a flag to control whether N must be the same for all parameters in all files
 * @return The reason why verification failed (or an empty string) and the per-file point/parameter shape object
 */
std::pair<std::string, std::map<std::string, CollectorShape>> verify_parameters_consistent(const std::vector<std::string> & filenames, const bool equal_length_required=false) {
  using CS = CollectorSink;
  std::map<std::string, CollectorShape> shapes;
  std::set<std::string> parameters;
  std::map<std::string, HighFive::DataType> type;
  std::map<std::string, std::string> unit, comment;
  size_t points{0};
  bool first{true};
  std::stringstream res;
  std::optional<HighFive::Group> group;
  const auto & uname = CS::parameter_unit_attribute_name();
  const auto & cname = CS::parameter_description_attribute_name();
  for (const auto & filename: filenames) {
    shapes[filename] = CollectorShape();
    try {
      auto file = HighFive::File(filename, HighFive::File::ReadOnly);
      group = file.getGroup(CS::parameter_group_name());
    } catch (std::exception ex) {
      res << "Unable to retrieve parameter group from " << filename << " due to exception " << ex.what();
      return {res.str(), {}};
    }
    for (size_t i=0; i<group->getNumberObjects(); ++i) {
      if (const auto name = group->getObjectName(i); group->getObjectType(name) == HighFive::ObjectType::Dataset) {
        auto ds = group->getDataSet(name);
        // A parameter dataset must be 1-D
        auto dims = ds.getDimensions();
        if (dims.size() != 1u) {
          res << "Non-vector dataset " << name << " in " << filename;
          return {res.str(), {}};
        }
        // All parameter datasets in one file must have the same number of entries
        // And all files might be required to have the same number of entries
        if (!shapes.at(filename).points.has_value()) {
          shapes.at(filename).points = dims.back();
          if (first) {
            points = dims.back();
          } else if (equal_length_required && shapes.at(filename).points != points) {
            res << "Inconsistent points across files (current file is " << filename << ")";
            return {res.str(), {}};
          }
        } else if (dims.back() != shapes.at(filename).points.value()) {
          res << "Inconsistent points dimension for " << name << " in " << filename;
          return {res.str(), {}};
        }
        // Parameter datasets across datasets must all have the same names, datatypes, units, and descriptions
        if (first) {
          parameters.insert(name);
          type.insert({name, ds.getDataType()});
          if (ds.hasAttribute(uname)) {
            unit.insert({name, ds.getAttribute(uname).read<std::string>()});
          }
          if (ds.hasAttribute(cname)) {
            comment.insert({name, ds.getAttribute(cname).read<std::string>()});
          }
        } else {
          if (!parameters.contains(name)) {
            res << "Extra parameter " << name << " in file " << filename << ".";
            return {res.str(), {}};
          }
          shapes.at(filename).parameters.insert(name);
          if (type.at(name) != ds.getDataType()) {
            res << "Data type mismatch for " << name << " in file " << filename << ".";
            return {res.str(), {}};
          }
          if (unit.contains(name) && (!ds.hasAttribute(uname) || unit.at(name) != ds.getAttribute(uname).read<std::string>())) {
            res << "Expected unit for " << name << ", " << unit.at(name) << (ds.hasAttribute(uname) ? " is wrong" : " is missing") << " in file " << filename;
            return {res.str(), {}};
          }
          if (ds.hasAttribute(uname) && !unit.contains(name)) {
            res << "Unexpected unit for " << name << " in file " << filename;
            return {res.str(), {}};
          }
          if (comment.contains(name) && (!ds.hasAttribute(cname) || comment.at(name) != ds.getAttribute(cname).read<std::string>())) {
            res << "Expected description for " << name << ", " << comment.at(name) << (ds.hasAttribute(uname) ? " is wrong" : " is missing") << " in file " << filename;
            return {res.str(), {}};
          }
          if (ds.hasAttribute(cname) && !comment.contains(name)) {
            res << "Unexpected description for " << name << " in file " << filename;
            return {res.str(), {}};
          }
        }
      }
    }
    // the set difference between the current file and the first file's parameters should be empty
    if (!first) {
      for (const auto & name: parameters) {
        if (!shapes.at(filename).parameters.contains(name)) {
          if (!parameters.contains(name)) {
            res << "File " << filename << "is missing parameter " << name << ".";
            return {res.str(), {}};
          }
        }
      }
    }
    first = false;
    shapes.at(filename).points = points;
  }
  return {{}, shapes};
}


std::pair<std::string, std::map<std::string, CollectorShape>> verify_parameters_identical(const std::vector<std::string> & filenames) {
  auto [vres, shapes] = verify_parameters_consistent(filenames, true);
  if (vres.size() > 0) {
    return {vres, {}};
  }
  // Everything except for the dataset values has been shown to be identical. Now go through again and verify
  // that the values are the same:
  std::stringstream res;
  std::map<std::string, std::vector<uint8_t>> buffers;
  std::map<std::string, std::string> first;
  for (const auto & filename: filenames) {
    const auto group = HighFive::File(filename, HighFive::File::ReadOnly).getGroup(CollectorSink::parameter_group_name());
    for (const auto & name: shapes.at(filenames.front()).parameters) {
      const auto dataset = group.getDataSet(name);
      const auto type = dataset.getDataType();
      const auto count = dataset.getDimensions().back() * type.getSize();
      std::vector<uint8_t> buffer(count);
      dataset.read_raw(buffer.data(), type);
      if (!buffers.contains(name)) {
        buffers.insert({name, buffer});
        first.insert({name, filename});
      } else if (!std::ranges::equal(buffers.at(name), buffer)) {
        res << "Parameter value mismatch for " << name << " in file " << filename << " compared to " << first.at(name) << ".";
        return {res.str(), {}};
      }
    }
  }
  return {{}, shapes};
}

enum class Consistency {inconsistent, consistent, identical};

std::pair<Consistency, std::map<std::string, CollectorShape>> classify_file_parameters(const std::vector<std::string> & filenames) {
  auto [res, shapes] = verify_parameters_consistent(filenames, true);
  if (!res.empty()) return {Consistency::inconsistent, {}};

  std::map<std::string, std::vector<uint8_t>> buffers;
  for (const auto & filename: filenames) {
    const auto group = HighFive::File(filename, HighFive::File::ReadOnly).getGroup(CollectorSink::parameter_group_name());
    for (const auto & name: shapes.at(filenames.front()).parameters) {
      const auto dataset = group.getDataSet(name);
      const auto type = dataset.getDataType();
      const auto count = dataset.getDimensions().back() * type.getSize();
      std::vector<uint8_t> buffer(count);
      dataset.read_raw(buffer.data(), type);
      if (!buffers.contains(name)) {
        buffers.insert({name, buffer});
      } else if (!std::ranges::equal(buffers.at(name), buffer)) {
        return {Consistency::consistent, shapes};
      }
    }
  }
  return {Consistency::identical, shapes};
}


/*** Collector files should only contain a parameters group and any number of collector instance output groups
 * No other datasets or groups are expected
 *
 * @param shapes A collection of HDF5 filenames containing collector output and their parameter size information
 * @return Any error string plus the set of collector groups present across all files
 *
 * The **shapes** objects are modified to include their per-collector-group number of readouts per point
 */
std::pair<std::string, std::set<std::string>> identify_collector_datasets(std::map<std::string, CollectorShape> & shapes) {
  std::set<std::string> datasets;
  const auto & parameters = CollectorSink::parameter_group_name();
  const auto & cues = CollectorSink::cue_dataset_name();
  for (auto & [filename, shape]: shapes) {
    const auto file = HighFive::File(filename, HighFive::File::ReadOnly);
    for (size_t i=0; i<file.getNumberObjects(); ++i) {
      if (const auto name = file.getObjectName(i); name != parameters && file.getObjectType(name) == HighFive::ObjectType::Group) {
        if (const auto & res = validate_collector_group(file.getGroup(name)); res.empty()) {
          datasets.insert(name);
          if (const auto res2 = shape.insert_readouts_from_cues(name, file.getGroup(name).getDataSet(cues)); !res2.empty()) {
            return {res2, {}};
          }
        } else {
          std::cerr << "Unexpected non-collector group " << name << " in " << filename << " due to" << std::endl << res << std::endl;
        }
      }
    }
  }
  return {{}, datasets};
}

void ensure_collector_group_attributes(HighFive::Group & group) {
  using C=CollectorSink;
  if (!group.hasAttribute(C::type_attribute())) {
    group.createAttribute<std::string>(C::type_attribute(), C::collector_group_type());
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
}

std::string validate_collector_group(const HighFive::Group & group) {
  using C=CollectorSink;
  const auto & tan = C::type_attribute();
  const auto & tav = C::collector_group_type();
  if (!group.hasAttribute(tan) || group.getAttribute(tan).read<std::string>() != tav) {
    return "Missing required collector attributes";
  }
  for (const auto & name: {C::readout_dataset_name(), C::cue_dataset_name(), C::weight_dataset_name()}) {
    if (!group.exist(name)) {
      return std::format("Missing {} entry from collector group", name);
    }
    if (group.getObjectType(name) != HighFive::ObjectType::Dataset) {
      return std::format("Entry {} should be a dataset", name);
    }
    if (auto dims = group.getDataSet(name).getDimensions(); dims.size() != 1) {
      return std::format("Dataset {} should be 1-D but is {}-D instead", name, dims.size());
    }
  }
  auto readouts = group.getDataSet(C::readout_dataset_name());
  if (!readouts.hasAttribute(C::detector_attribute_name())) {
    return "readout dataset should have a detector name attribute";
  }
  if (!readouts.hasAttribute(C::readout_attribute_name())) {
    return "readout dataset should have a readout attribute name";
  }

  return {};
}

std::string validate_collector_root(const HighFive::Group & group) {
  using namespace HighFive;
  using C=CollectorSink;
  const auto & ta = C::type_attribute();

  // verify that any datasets in the collector group have the expected attributes and dimensions:
  const auto object_count = group.getNumberObjects();
  std::stringstream message;
  for (size_t i=0; i<object_count; ++i) {
    const auto name = group.getObjectName(i);
    if (group.getObjectType(name) != ObjectType::Group) {
      message << "Node " << name << " in parameters is not a group." << std::endl;
    } else if (!group.getGroup(name).hasAttribute(ta)) {
      message << "Node " << name << " in parameters is not a supported Collector group." << std::endl;
    } else {
      const auto subgroup = group.getGroup(name);
      if (const auto & group_type = subgroup.getAttribute(ta).read<std::string>(); group_type == C::collector_group_type()) {
        if (const auto & res = validate_collector_group(subgroup); res.size() > 0) {
          message << res << std::endl;
        }
      } else if (group_type == C::parameter_group_type()) {
        for (size_t j=0; j< subgroup.getNumberObjects(); ++j) {
          const auto param_name = subgroup.getObjectName(j);
          if (subgroup.getObjectType(param_name) != ObjectType::Dataset) {
            message << "Node " << param_name << " in parameters is not a dataset." << std::endl;
          } else if (auto dims = subgroup.getDataSet(param_name).getDimensions(); dims.size() != 1) {
            message << "Parameter " << param_name << " is not one-dimensional." << std::endl;
          }
        }
      } else {
        message << "Unexpected Collector group " << name << " with type " << group_type << std::endl;
      }
    }
  }
  return message.str();
}


int validate_collector_file_impl(const HighFive::File & file, const std::string & filename) {
  using C=CollectorSink;
  using namespace HighFive;
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
  if (const auto result = validate_collector_group(file.getGroup("/")); !result.empty()) {
    std::cerr << "Warning: file " << filename << " has a root group with unexpected structure: " << std::endl << result;
    return -1;
  }

  return 0;
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
  const std::vector<std::string> & in_filenames,
  std::optional<std::string> expected_dataset,
  std::set<std::string> & datasets,
  std::map<std::string, size_t> & sizes,
  std::set<std::string> & valid_files
  ) {
  using C=CollectorSink;
  using namespace HighFive;
  bool first{true};
  for (const auto & in_file : in_filenames) {
    try {
      File file(in_file, File::ReadOnly);
      if (const auto n = validate_collector_file_impl(file, in_file); n < 0) {
        continue;
      }
      auto collector = file.getGroup("/");
      bool all_datasets_valid{true};
      const auto& dan = C::detector_attribute_name();
      const auto& ran = C::readout_attribute_name();
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
  const std::vector<std::string> & in_filenames,
  std::set<std::string> & valid_files,
  std::set<std::string> & parameters
  ) {
  using C=CollectorSink;
  using namespace HighFive;
  bool first{true};
  for (const auto & in_file : in_filenames) {
    try {
      File file(in_file, File::ReadOnly);
      if (const auto n = validate_collector_file_impl(file, in_file); n < 0) {
        continue;
      }
      auto collector = file.getGroup("/");
      bool all_parameters_valid{true};
      const auto& pgn = C::parameter_group_name();
      for (size_t i=0; i<collector.getNumberObjects(); ++i) {
        if (const auto name = collector.getObjectName(i); collector.getObjectType(name) == ObjectType::Group && name == pgn) {
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
  const std::vector<std::string> & in_filenames,
  std::set<std::string> & datasets,
  std::map<std::string, size_t> & sizes,
  std::set<std::string> & valid_files,
  std::string & collector_name,
  std::set<std::string> & parameters
  ) {
  using namespace HighFive;

  if (!validate_collector_files_datasets(in_filenames, std::nullopt, datasets, sizes, valid_files)) {
    std::cerr << "No valid input files found, cannot merge!" << std::endl;
    return false;
  }

  std::set<std::string> files_with_valid_parameters;
  std::string parameter_collector_name{"/"};
  if (!validate_collector_files_parameters(in_filenames, files_with_valid_parameters, parameters)) {
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


template<class GroupOrDataSet>
void copy_attribute(const GroupOrDataSet & source, const std::string & name, GroupOrDataSet & destination) {
  if (!source.hasAttribute(name)) {
    return;
  }
  if (destination.hasAttribute(name)) {
    return;
  }
  auto attr = source.getAttribute(name);
  const auto size = attr.getStorageSize();
  const auto type = attr.getDataType();
  const auto space = attr.getSpace();
  std::vector<uint8_t> buffer(size);
  attr.read_raw(buffer.data(), type);

  destination.createAttribute(name, space, type);
  destination.getAttribute(name).write_raw(buffer.data(), type);
}

void copy_dataset(const HighFive::Group & source, const std::string & name, HighFive::Group & destination) {
  const auto ds = source.getDataSet(name);
  const auto type = ds.getDataType();
  const auto space = ds.getSpace();
  const auto create_prop = ds.getCreatePropertyList();
  const auto access_prop = ds.getAccessPropertyList();
  std::vector<uint8_t> buffer(ds.getStorageSize());
  ds.read_raw(buffer.data(), type);

  destination.createDataSet(name, space, type, create_prop, access_prop);
  destination.getDataSet(name).write_raw(buffer.data(), type);

  for (const auto & attr: source.listAttributeNames()) {
    copy_attribute(source, attr, destination);
  }
}

void copy_group(const HighFive::Group & source, const std::string & name, HighFive::Group & destination) {
  const auto from_group = source.getGroup(name);
  auto to_group = destination.createGroup(name);
  for (const auto & obj_name: from_group.listObjectNames()) {
    if (const auto obj_type = from_group.getObjectType(obj_name); obj_type == HighFive::ObjectType::Group) {
      copy_group(from_group, obj_name, to_group);
    } else if (obj_type == HighFive::ObjectType::Dataset) {
      copy_dataset(from_group, obj_name, to_group);
    } else {
      std::cerr << "Unknown object type for " << name << std::endl;
    }
  }
  for (const auto & attr: from_group.listAttributeNames()) {
    copy_attribute(from_group, attr, to_group);
  }
}


void empty_like_dataset(const HighFive::Group & source, const std::string & name, HighFive::Group & destination, const std::optional<size_t> size = std::nullopt) {
  const auto ds = source.getDataSet(name);
  auto dimensions = ds.getSpace().getDimensions();
  if (size.has_value()) {
    dimensions.back() = size.value();
  }
  const auto space = HighFive::DataSpace(dimensions);
  destination.createDataSet(name, space, ds.getDataType());

  for (const auto & attr: source.listAttributeNames()) {
    copy_attribute(source, attr, destination);
  }
}


void empty_collector_group_like(const HighFive::Group & source, const std::string & name, HighFive::Group & destination, const uint32_t readouts, const uint32_t points) {
  /* A collector group should only ever contain *datasets*:
   *    readouts<compound-typed>[n_readouts,]
   *    weights<double>[n_points,]
   *    cues<uint32_t>[n_points,]
   *    normalizations<uint64_t>[n_points,]
   *
   * Here we keep copying of subgroups and generic empty-dataset creation for easier modification in the future
   */
  const auto from_group = source.getGroup(name); // should only contain {readouts, cues, weights, normalizations}
  auto to_group = destination.createGroup(name);
  for (const auto & obj_name: from_group.listObjectNames()) {
    if (const auto obj_type = from_group.getObjectType(obj_name); obj_type == HighFive::ObjectType::Group) {
      copy_group(from_group, obj_name, to_group);
    } else if (obj_type == HighFive::ObjectType::Dataset) {
      if (obj_name == CollectorSink::readout_dataset_name()) {
        empty_like_dataset(from_group, obj_name, to_group, readouts);
      } else {
        // TODO these values need to be initialized
        empty_like_dataset(from_group, obj_name, to_group, points);
      }

    } else {
      std::cerr << "Unknown object type for " << name << std::endl;
    }
  }
  for (const auto & attr: from_group.listAttributeNames()) {
    copy_attribute(from_group, attr, to_group);
  }
}

void combine_collector_group(const HighFive::Group & source, const std::string & name, const HighFive::Group & destination, const std::vector<uint32_t> & totals, const std::vector<uint32_t> & shape, std::vector<uint32_t> & offsets) {
  using CS = CollectorSink;
  const auto from_group = source.getGroup(name);
  const auto to_group = destination.getGroup(name);
  const auto points = totals.size();

  for (size_t point=0; point<points; ++point) {
    // bounds checking to ensure we don't overrun the total number of readouts per point?
    if (offsets[point] + shape[point] > totals[point]) {
      std::cerr << "Out of bounds readout copying in point " << point << " of " << points << std::endl;
    }
  }

  const auto & rdn = CS::readout_dataset_name();
  const auto type = from_group.getDataSet(rdn).getDataType();
  uint32_t offset{0};
  for (size_t point=0; point<points; ++point) if (shape[point] > 0) {
    std::vector<uint8_t> buffer(shape[point] * type.getSize());
    // step through the input list of readouts according to the number of readouts per point
    from_group.getDataSet(rdn).select({offset}, {shape[point]}).read_raw(buffer.data(), type);
    offset += shape[point];
    // and the output according to where the current point has been written up-to thus far
    to_group.getDataSet(rdn).select({offsets[point]}, {shape[point]}).write_raw(buffer.data(), type);
    offsets[point] += shape[point];
  }

  // accumulate the remaining required datasets
  const auto & cdn = CS::cue_dataset_name();
  const auto & wdn = CS::weight_dataset_name();
  const auto & ndn = CS::normalization_dataset_name();

  std::vector<uint32_t> cues(points), add_cues(points);
  from_group.getDataSet(cdn).read(add_cues);
  to_group.getDataSet(cdn).read(cues);

  std::vector<double> weights(points), add_weights(points);
  from_group.getDataSet(wdn).read(add_weights);
  to_group.getDataSet(wdn).read(weights);

  std::vector<uint64_t> normalizations(points), add_normalizations(points);
  from_group.getDataSet(ndn).read(add_normalizations);
  to_group.getDataSet(ndn).read(normalizations);

  for (size_t point=0; point<points; ++point) {
    cues[point] += add_cues[point];
    weights[point] += add_weights[point];
    normalizations[point] += add_normalizations[point];
  }
  to_group.getDataSet(cdn).write(cues);
  to_group.getDataSet(cdn).write(weights);
  to_group.getDataSet(cdn).write(normalizations);
}

void concatenate_collector_group (const HighFive::Group & source, const std::string & name, const HighFive::Group & destination, const std::vector<uint32_t> & totals, const std::vector<uint32_t> & shape, std::vector<uint32_t> & offsets) {
  using CS = CollectorSink;
  const auto from_group = source.getGroup(name);
  const auto to_group = destination.getGroup(name);
  const auto total_points = totals.size();
  const auto points = shape.size();
  size_t pre{0u};
  while (pre < total_points && offsets[pre] > 0) ++pre;
  if (pre + points >= total_points) {
    std::cerr << "Out of bounds readout concatenation in " << name << " after " << pre << " points" << std::endl;
  }
  for (size_t point=0; point < points; ++point) {
    if (shape[point] != totals[pre + point]) {
      std::cerr << "Unexpected readouts for point " << point << " since " << shape[point] << " != " << totals[pre + point] << std::endl;
    }
  }
  const auto & rdn = CS::readout_dataset_name();
  const auto type = from_group.getDataSet(rdn).getDataType();
  uint32_t offset{0};
  for (size_t point=0; point<points; ++point) if (shape[point] > 0) {
    std::vector<uint8_t> buffer(shape[point] * type.getSize());
    // step through the input list of readouts according to the number of readouts per point
    from_group.getDataSet(rdn).select({offset}, {shape[point]}).read_raw(buffer.data(), type);
    offset += shape[point];
    // and the output following the prior point's last entry (or zero if no point yet)
    auto this_offset = pre + point > 0 ? offsets[pre + point - 1] : 0u;
    to_group.getDataSet(rdn).select({this_offset}, {shape[point]}).write_raw(buffer.data(), type);
    offsets[point] = this_offset + shape[point];
  }

  // concatenate the remaining required datasets
  const auto & cdn = CS::cue_dataset_name();
  const auto & wdn = CS::weight_dataset_name();
  const auto & ndn = CS::normalization_dataset_name();

  std::vector<uint32_t> cues(points);
  std::vector<double> weights(points);
  std::vector<uint64_t> normalizations(points);

  from_group.getDataSet(cdn).read(cues);
  from_group.getDataSet(wdn).read(weights);
  from_group.getDataSet(ndn).read(normalizations);

  to_group.getDataSet(cdn).select({pre}, {points}).write(cues);
  to_group.getDataSet(cdn).select({pre}, {points}).write(weights);
  to_group.getDataSet(cdn).select({pre}, {points}).write(normalizations);
}


/*
 * New merge algorithm approach:
 * At least two ways to merge, either combine equivalent points or concatenate inequivalent points. The algorithm could be
 * smart enough to do both or to guess which is needed, but for sanity an explicit choice should be provided.
 *
 * A: combine equivalent points
 *   1. verify parameter information is identical
 *   2. verify same collector groups (or get the set of all unique group names?)
 *   3. for each (unique) group across all files
 *     i.   concatenate readouts in per-point order (tricky?)
 *     ii.  add cue vectors elementwise
 *     iii. add weight vectors elementwise
 *     iv.  add normalization vectors elementwise
 *
 * B: concatenate inequivalent points
 *   1. verify same parameter datasets exist in all files (names, datatypes, units, comments, ..., *not* values)
 *   2. verify same collector groups exist in all files
 *   3. for each unique group
 *     i.   concatenate readout vectors
 *     ii.  concatenate cue vectors, offset by cumulative readout vector length
 *     iii. concatenate weight vectors
 *     iv.  concatenate normalization vectors
 *   4. for each unique parameter
 *     i.   concatenate datasets
 *
 *  Path A should cover the case where collector groups come from different files
 *  and the case where point(s) have been repeated or subdivided across nodes
 *
 *  Path B should cover merging scan point files post-scan.
 */
void combine_collector_files_equivalent_points(const HighFive::File & outfile, const std::map<std::string, CollectorShape>& shapes, const std::set<std::string>& collectors, const std::vector<std::string> & in_filenames) {
  // copy parameters group to the output file
  auto out_parameters = outfile.getGroup("/");
  copy_group(HighFive::File(in_filenames.front(), HighFive::File::ReadOnly).getGroup("/"), CollectorSink::parameter_group_name(), out_parameters);

  // decide the per-collector total number of readouts needed
  const auto combined = std::ranges::fold_left(shapes | std::views::values | std::views::drop(1), shapes.begin()->second, [](const auto a, const auto b){return a.combine(b);});
  // setup counters for per-point offsets during piecewise copying
  auto counters = combined.null_readouts();

  // for each unique dataset, copy from the first file containing it then concatenate remaining **in file order**
  std::set<std::string> initialized{};
  auto sink = outfile.getGroup("/");
  for (const auto & name: collectors) {
    for (const auto & filename: in_filenames) {
      if (shapes.at(filename).readouts.contains(name)) {
        auto source = HighFive::File(filename, HighFive::File::ReadOnly).getGroup("/");
        if (!initialized.contains(name)) {
          // create the output dataset before trying to write into it:
          empty_collector_group_like(source, name, sink, combined.total_readouts(name), combined.points.value());
          // ensure we don't try and create this dataset again
          initialized.insert(name);
        }
        combine_collector_group(source, name, sink, combined.readouts.at(name), shapes.at(filename).readouts.at(name), counters.readouts.at(name));
      }
    }
  }
}
void combine_collector_files_concatenate(const HighFive::File & outfile, const std::map<std::string, CollectorShape>& shapes, const std::set<std::string>& collectors, const std::vector<std::string> & in_filenames) {
  // collect the collector shapes _in file order_
  std::vector<const CollectorShape*> shape_ptrs;
  shape_ptrs.reserve(in_filenames.size());
  for (const auto & name: in_filenames) {
    shape_ptrs.push_back(&shapes.at(name));
  }
  // and get the total size(s)
  const auto concatenated = concatenate_shapes(shape_ptrs);
  auto counters = concatenated.null_readouts();

  //
  auto sink = outfile.getGroup("/");
  {
    const auto source = HighFive::File(in_filenames.front(), HighFive::File::ReadOnly).getGroup("/");
    for (const auto & name: collectors) {
      empty_collector_group_like(source, name, sink, concatenated.total_readouts(name), concatenated.points.value());
    }
  }
  for (const auto & filename: in_filenames) {
    auto source = HighFive::File(filename, HighFive::File::ReadOnly).getGroup("/");
    for (const auto & name: collectors) {
      concatenate_collector_group(source, name, sink, concatenated.readouts.at(name), shapes.at(filename).readouts.at(name), counters.readouts.at(name));
    }
  }

  // TODO continue path B by concatenating the parameters from all files

}

void combine_collector_files_path_a(const std::string & out_filename, const std::vector<std::string> & in_filenames) {
  if (const auto & res = verify_collector_files(in_filenames); res.size() > 0) {
    std::cerr << res << std::endl;
    return;
  }
  auto [res, shapes] = verify_parameters_identical(in_filenames);
  if ( res.size() > 0) {
    std::cerr << res << std::endl;
    return;
  }
  // figure out the unique datasets across all files
  const auto & [problems, collectors] = identify_collector_datasets(shapes);
  if ( !problems.empty() ) {
    std::cerr << problems << std::endl;
    return;
  }
  // create the output file
  const auto outfile = HighFive::File(out_filename, HighFive::File::Create);
  // do the actual combining
  combine_collector_files_equivalent_points(outfile, shapes, collectors, in_filenames);
}

void combine_collector_files(const std::string & out_filename, const std::vector<std::string> & in_filenames) {
  if (const auto & res = verify_collector_files(in_filenames); res.size() > 0) {
    std::cerr << res << std::endl;
    return;
  }
  auto [consistency, shapes] = classify_file_parameters(in_filenames);
  if (Consistency::inconsistent == consistency) {
    return;
  }
  // figure out the unique datasets across all files
  if (const auto & [problems, collectors] = identify_collector_datasets(shapes); problems.empty() ) {
    const auto outfile = HighFive::File(out_filename, HighFive::File::Create);
    if (Consistency::identical == consistency) {
      combine_collector_files_equivalent_points(outfile, shapes, collectors, in_filenames);
    }
    else if (Consistency::consistent == consistency) {
      // FIXME a check for identical collector group names is needed -- each should be identical to the set 'collectors'
      for (const auto & [filename, shape]: shapes) {
        if (!shape.has_all_readouts(collectors)) {
          std::cerr << "File " << filename << " is missing one or more collector group(s)" << std::endl;
          return;
        }
      }
      combine_collector_files_concatenate(outfile, shapes, collectors, in_filenames);
    }
  } else {
    std::cerr << problems << std::endl;
  }
}


void merge_collector_files(const std::string & out_filename, const std::vector<std::string> & in_filenames, const bool remove_after_merge) {
  using namespace HighFive;

  std::set<std::string> datasets;
  std::map<std::string, size_t> sizes;
  std::set<std::string> valid_files;
  std::string collector_name{"/"};
  std::set<std::string> parameters;
  if (!validate_collector_files(in_filenames, datasets, sizes, valid_files, collector_name, parameters)) {
    std::cerr << "No valid input files found, cannot merge!" << std::endl;
    return;
  }

  try {
    bool first{true};
    File out_file(out_filename, File::OpenOrCreate);
    ensure_file_attributes(out_file);

    auto out_collector = out_file.getGroup("/");
    // This can be simplified -- each group merely appends itself to the end of the pre-existing group of the same name
    // FIXME the current implementation is certainly wrong!

    first = true;
    for (const auto & in_file : valid_files) {
      File file(in_file, File::ReadOnly);
      auto in_collector = file.getGroup(collector_name);

      if (first && !parameters.empty()) {
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
          if (auto expected_type = hdf_compound_type(readout_type); datatype != expected_type) {
            std::cerr << "Warning: dataset " << dataset_name << " in file " << in_file << " has data type which does not match the expected type for its readout. This may cause problems when merging." << std::endl;
          }
          // Extensible datasets require chunked storage
          DataSetCreateProps props;
          props.add(Chunking(std::vector<hsize_t>{100}));
          auto out_ds = out_collector.createDataSet(dataset_name, dataspace, datatype, props);
          auto detector_type_name = in_data.getAttribute("detector").read<std::string>();
          auto detector_type = detectorType_from_name(detector_type_name);
          ensure_dataset_attributes(out_ds, detector_type, readout_type);
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
        concat_dataset(out_data,  out_size, in_data);
        // accumulate the dataset weight dataset, ensure any post-merge datasets are reset
      }
      first = false;
    }
    // done merging, flush the output file to ensure all data is written to disk
    out_file.flush();
  } catch (Exception & ex) {
    std::cerr << "Error writing output file " << out_filename << ": " << ex.what() << std::endl;
  }
}



void copy_collector_parameters(const std::string & out_filename, const std::vector<std::string> & in_filenames) {
  using namespace HighFive;

  std::set<std::string> valid_files;
  std::string collector_name{"/"};
  std::set<std::string> parameters;
  if (!validate_collector_files_parameters(in_filenames, valid_files, parameters)) {
    std::cerr << "No valid input files found, cannot merge!" << std::endl;
    return;
  }
  if (parameters.empty()) {
    return;
  }

  try {
    File out_file(out_filename, File::OpenOrCreate);
    ensure_file_attributes(out_file);

    auto out_collector = out_file.getGroup("/");

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
  if (!validate_collector_files_parameters({out_filename}, post_copy_valid_files, post_copy_parameters)) {
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