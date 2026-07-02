#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ReadoutClass.h"
#include "CollectorClass.h"


#ifdef WIN32
// Export symbols if compile flags "READOUT_SHARED" and "READOUT_EXPORT" are set on Windows.
    #ifdef READOUT_SHARED
        #ifdef READOUT_EXPORT
            #define RL_API __declspec(dllexport)
        #else
            #define RL_API __declspec(dllimport)
        #endif
    #else
        // Disable definition if linking statically.
        #define RL_API
    #endif
#else
// Disable definition for non-Win32 systems.
#define RL_API
#endif

struct ParameterDatasetView {
  std::string name;
  HighFive::DataType datatype;
  std::optional<std::string> unit;
  std::optional<std::string> description;
};

class Reader {
  std::shared_ptr<HighFive::File> file_;
  std::string group_name_;
  std::optional<HighFive::Group> group_;
  std::optional<HighFive::DataSet> readouts_, cues_, weights_, normalizations_;
  std::vector<uint32_t> cue_values_;
  std::optional<DetectorType> detector_{std::nullopt};
  std::optional<ReadoutType> readout_{std::nullopt};
  std::optional<ReadoutType> sendable_{std::nullopt};
  std::optional<std::string> description_{std::nullopt};
  std::optional<std::string> efu_address_{std::nullopt};
  std::optional<uint16_t> efu_port_{std::nullopt};

  [[nodiscard]] std::pair<size_t, size_t> point_bounds(const size_t point) const {
    if (point >= cue_values_.size()) {
      throw std::runtime_error("Out of bounds point requested");
    }
    const auto end = static_cast<size_t>(cue_values_.at(point));
    const auto start = point > 0 ? static_cast<size_t>(cue_values_.at(point - 1)) : 0u;
    if (end < start || end > size()) {
      throw std::runtime_error("Invalid cue values in collector group");
    }
    return {start, end};
  }

public:
  RL_API explicit Reader(std::shared_ptr<HighFive::File> file, const std::string & collector_name): file_{std::move(file)}, group_name_{collector_name} {
    if (!file_) {
      throw std::runtime_error("Reader requires an open file");
    }
    if (!file_->exist(group_name_)) {
      std::stringstream s;
      s << "Collector group \"" << group_name_ << "\" not found in file";
      throw std::runtime_error(s.str());
    }
    group_ = file_->getGroup(group_name_);
    if (const auto result = validate_collector_group(group_.value()); !result.empty()) {
      std::stringstream s;
      s << "Collector group \"" << group_name_ << "\" is invalid: " << result;
      throw std::runtime_error(s.str());
    }

    readouts_ = group_->getDataSet(CollectorSink::readout_dataset_name());
    cues_ = group_->getDataSet(CollectorSink::cue_dataset_name());
    weights_ = group_->getDataSet(CollectorSink::weight_dataset_name());
    normalizations_ = group_->getDataSet(CollectorSink::normalization_dataset_name());

    // detector/readout attributes are informative and only present on registry-typed groups
    if (readouts_->hasAttribute(CollectorSink::detector_attribute_name())) {
      detector_ = detectorType_from_name(readouts_->getAttribute(CollectorSink::detector_attribute_name()).read<std::string>());
    }
    if (readouts_->hasAttribute(CollectorSink::readout_attribute_name())) {
      readout_ = readoutType_from_name(readouts_->getAttribute(CollectorSink::readout_attribute_name()).read<std::string>());
    }
    if (readouts_->hasAttribute(CollectorSink::parameter_description_attribute_name())) {
      description_ = readouts_->getAttribute(CollectorSink::parameter_description_attribute_name()).read<std::string>();
    }
    // EFU-sendability is decided by the stored datatype matching a registry compound type,
    // not by the attributes: an attribute cannot lie about the record layout
    const auto stored = readouts_->getDataType();
    for (const auto rt : {ReadoutType::CAEN, ReadoutType::TTLMonitor, ReadoutType::CDT, ReadoutType::VMM3,
                          ReadoutType::BM0, ReadoutType::BM2, ReadoutType::BMI}) {
      if (stored == hdf_compound_type(rt)) {
        sendable_ = rt;
        break;
      }
    }

    if (group_->hasAttribute(CollectorSink::efu_address_attribute_name())) {
      efu_address_ = group_->getAttribute(CollectorSink::efu_address_attribute_name()).read<std::string>();
    }
    if (group_->hasAttribute(CollectorSink::efu_port_attribute_name())) {
      const auto port = group_->getAttribute(CollectorSink::efu_port_attribute_name()).read<int>();
      if (port > 0 && port <= 65535) {
        efu_port_ = static_cast<uint16_t>(port);
      }
    }

    cues_->read(cue_values_);
    if (cue_values_.empty()) {
      throw std::runtime_error("Collector cues dataset is empty");
    }
    if (!std::ranges::is_sorted(cue_values_)) {
      throw std::runtime_error("Collector cues dataset must be monotonically non-decreasing");
    }
    if (cue_values_.back() != size()) {
      throw std::runtime_error("Collector cues end value does not match readout dataset size");
    }
    if (weights_->getDimensions().back() != cue_values_.size() || normalizations_->getDimensions().back() != cue_values_.size()) {
      throw std::runtime_error("Collector point datasets (cues, weights, normalizations) have mismatched dimensions");
    }
  }

  RL_API ~Reader() = default;

  RL_API [[nodiscard]] const std::string & collector_name() const { return group_name_; }
  /// The detector named by the group's attribute, or Reserved for user-described groups
  RL_API [[nodiscard]] DetectorType detector_type() const { return detector_.value_or(DetectorType::Reserved); }
  /// The readout type named by the group's attribute, or implied by the stored datatype
  RL_API [[nodiscard]] ReadoutType readout_type() const {
    if (readout_.has_value()) { return readout_.value(); }
    if (sendable_.has_value()) { return sendable_.value(); }
    throw std::runtime_error("Collector group has no readout type: it stores user-described records");
  }
  /// The registry readout type whose compound datatype exactly matches the stored records,
  /// or nullopt when the group stores user-described records that no EFU accepts
  RL_API [[nodiscard]] std::optional<ReadoutType> sendable_readout_type() const { return sendable_; }
  /// The original type-description string for user-described groups, if recorded
  RL_API [[nodiscard]] const std::optional<std::string> & type_description() const { return description_; }
  RL_API [[nodiscard]] size_t size() const { return readouts_->getDimensions().back(); }
  RL_API [[nodiscard]] size_t points() const { return cue_values_.size(); }
  RL_API [[nodiscard]] HighFive::DataType datatype() const { return readouts_->getDataType(); }
  RL_API [[nodiscard]] size_t record_size() const { return readouts_->getDataType().getSize(); }

  /// The accumulated rate-weight of one point
  RL_API [[nodiscard]] double point_weight(const size_t point) const {
    if (point >= cue_values_.size()) {
      throw std::runtime_error("Out of bounds point requested");
    }
    return weights_->select({point}, {1}).read<double>();
  }

  /// Raw access to stored records for user-described groups: count records starting at index,
  /// each record_size() bytes in the stored layout
  RL_API [[nodiscard]] std::vector<uint8_t> get_raw(const size_t index, const size_t count) const {
    if (index >= size() || index + count > size()) { throw std::runtime_error("Out of bounds record requested"); }
    const auto datatype = readouts_->getDataType();
    std::vector<uint8_t> buffer(count * datatype.getSize());
    readouts_->select({index}, {count}).read_raw(buffer.data(), datatype);
    return buffer;
  }

  /// Returns the EFU IP address embedded in the file, if any.
  RL_API [[nodiscard]] std::optional<std::string> efu_address() const { return efu_address_; }
  /// Returns the EFU UDP port embedded in the file, if any.
  RL_API [[nodiscard]] std::optional<uint16_t> efu_port() const { return efu_port_; }

  RL_API [[nodiscard]] size_t point_offset(const size_t point) const {
    return point_bounds(point).first;
  }
  RL_API [[nodiscard]] size_t point_size(const size_t point) const {
    const auto [start, end] = point_bounds(point);
    return end - start;
  }

  RL_API auto get_CAEN(const size_t index, const size_t count) const {
    if (sendable_ != ReadoutType::CAEN){ throw std::runtime_error("Non CAEN readout type"); }
    if (index >= size() || index + count > size()) { throw std::runtime_error("Out of bounds event requested");}
    std::vector<CAEN_event> event(count);
    const auto datatype = readouts_->getDataType();
    readouts_->select({index}, {count}).read_raw(event.data(), datatype);
    return event;
  }
  RL_API auto get_TTLMonitor(const size_t index, const size_t count) const {
    if (sendable_ != ReadoutType::TTLMonitor){ throw std::runtime_error("Non TTLMonitor readout type"); }
    if (index >= size() || index + count > size()) { throw std::runtime_error("Out of bounds event requested");}
    std::vector<TTLMonitor_event> event(count);
    const auto datatype = readouts_->getDataType();
    readouts_->select({index}, {count}).read_raw(event.data(), datatype);
    return event;
  }
  RL_API auto get_VMM3(const size_t index, const size_t count) const{
    if (sendable_ != ReadoutType::VMM3) { throw std::runtime_error("Non VMM3 readout type"); }
    if (index >= size() || index + count > size()) { throw std::runtime_error("Out of bounds event requested");}
    std::vector<VMM3_event> event(count);
    const auto datatype = readouts_->getDataType();
    readouts_->select({index}, {count}).read_raw(event.data(), datatype);
    return event;
  }
  RL_API auto get_CDT(const size_t index, const size_t count) const{
    if (sendable_ != ReadoutType::CDT) { throw std::runtime_error("Non CDT readout type"); }
    if (index >= size() || index + count > size()) { throw std::runtime_error("Out of bounds event requested");}
    std::vector<CDT_event> event(count);
    const auto datatype = readouts_->getDataType();
    readouts_->select({index}, {count}).read_raw(event.data(), datatype);
    return event;
  }
  RL_API auto get_BM0(const size_t index, const size_t count) const{
    if (sendable_ != ReadoutType::BM0) { throw std::runtime_error("Non BM0 readout type"); }
    if (index >= size() || index + count > size()) { throw std::runtime_error("Out of bounds event requested");}
    std::vector<BM0_event> event(count);
    const auto datatype = readouts_->getDataType();
    readouts_->select({index}, {count}).read_raw(event.data(), datatype);
    return event;
  }
  RL_API auto get_BM2(const size_t index, const size_t count) const{
    if (sendable_ != ReadoutType::BM2) { throw std::runtime_error("Non BM2 readout type"); }
    if (index >= size() || index + count > size()) { throw std::runtime_error("Out of bounds event requested");}
    std::vector<BM2_event> event(count);
    const auto datatype = readouts_->getDataType();
    readouts_->select({index}, {count}).read_raw(event.data(), datatype);
    return event;
  }
  RL_API auto get_BMI(const size_t index, const size_t count) const {
    if (sendable_ != ReadoutType::BMI) { throw std::runtime_error("Non BMI readout type"); }
    if (index >= size() || index + count > size()) { throw std::runtime_error("Out of bounds event requested");}
    std::vector<BMI_event> event(count);
    const auto datatype = readouts_->getDataType();
    readouts_->select({index}, {count}).read_raw(event.data(), datatype);
    return event;
  }

  RL_API auto get_point_CAEN(const size_t point) const {
    const auto [start, end] = point_bounds(point);
    return get_CAEN(start, end - start);
  }
  RL_API auto get_point_TTLMonitor(const size_t point) const {
    const auto [start, end] = point_bounds(point);
    return get_TTLMonitor(start, end - start);
  }
  RL_API auto get_point_VMM3(const size_t point) const {
    const auto [start, end] = point_bounds(point);
    return get_VMM3(start, end - start);
  }
  RL_API auto get_point_CDT(const size_t point) const {
    const auto [start, end] = point_bounds(point);
    return get_CDT(start, end - start);
  }
  RL_API auto get_point_BM0(const size_t point) const {
    const auto [start, end] = point_bounds(point);
    return get_BM0(start, end - start);
  }
  RL_API auto get_point_BM2(const size_t point) const {
    const auto [start, end] = point_bounds(point);
    return get_BM2(start, end - start);
  }
  RL_API auto get_point_BMI(const size_t point) const {
    const auto [start, end] = point_bounds(point);
    return get_BMI(start, end - start);
  }
};

class ReaderSource {
  std::string filename_;
  std::shared_ptr<HighFive::File> file_;
  std::optional<HighFive::Group> parameters_;
  std::vector<Reader> readers_;
  std::vector<std::string> parameter_names_;
  std::vector<ParameterDatasetView> parameter_views_;
  size_t points_{0};

  static bool is_parameter_group(const HighFive::Group & group) {
    return group.hasAttribute(CollectorSink::type_attribute())
      && group.getAttribute(CollectorSink::type_attribute()).read<std::string>() == CollectorSink::parameter_group_type();
  }

  static bool is_collector_group(const HighFive::Group & group) {
    return group.hasAttribute(CollectorSink::type_attribute())
      && group.getAttribute(CollectorSink::type_attribute()).read<std::string>() == CollectorSink::collector_group_type();
  }

public:
  RL_API explicit ReaderSource(const std::string& filename): filename_{filename} {
    try {
      file_ = std::make_shared<HighFive::File>(filename, HighFive::File::ReadOnly);
    } catch (HighFive::Exception & ex) {
      std::stringstream s;
      s << "Error opening file " << filename << ":\n" << ex.what();
      throw std::runtime_error(s.str());
    }

    std::string program;
    if (file_->hasAttribute(CollectorSink::program_attribute_name())) {
      file_->getAttribute(CollectorSink::program_attribute_name()).read(program);
    }
    if (program != CollectorSink::program_attribute_value()) {
      throw std::runtime_error("The provided HDF file was not produced using libreadout Collector output");
    }
    if (!file_->hasAttribute(CollectorSink::version_attribute_name()) || !file_->hasAttribute(CollectorSink::revision_attribute_name())) {
      throw std::runtime_error("Collector file is missing required version attributes");
    }

    const auto version = file_->getAttribute(CollectorSink::version_attribute_name()).read<std::string>();
    const auto this_version = std::string(reinterpret_cast<const char *>(libreadout::version::version_number));
    if (version != this_version){
      std::cout << "The file was produced using libreadout " << version;
      std::cout << " not current " << this_version << std::endl;
    }

    const auto root = file_->getGroup("/");
    std::vector<std::string> collector_names;
    for (size_t i=0; i<root.getNumberObjects(); ++i) {
      const auto name = root.getObjectName(i);
      if (root.getObjectType(name) != HighFive::ObjectType::Group) {
        continue;
      }
      const auto group = root.getGroup(name);
      if (name == CollectorSink::parameter_group_name() || is_parameter_group(group)) {
        parameters_ = group;
      } else if (is_collector_group(group)) {
        collector_names.push_back(name);
      }
    }

    if (collector_names.empty()) {
      throw std::runtime_error("Collector file has no collector groups. Legacy flat readout files are not supported.");
    }
    std::ranges::sort(collector_names);
    readers_.reserve(collector_names.size());
    for (const auto & name : collector_names) {
      readers_.emplace_back(file_, name);
    }

    points_ = readers_.front().points();
    for (const auto & reader : readers_) {
      if (reader.points() != points_) {
        throw std::runtime_error("Collector groups have inconsistent point counts");
      }
    }

    if (parameters_.has_value()) {
      for (size_t i=0; i<parameters_->getNumberObjects(); ++i) {
        const auto name = parameters_->getObjectName(i);
        if (parameters_->getObjectType(name) != HighFive::ObjectType::Dataset) {
          continue;
        }
        const auto ds = parameters_->getDataSet(name);
        if (const auto dims = ds.getDimensions(); dims.size() != 1u || dims.back() != points_) {
          throw std::runtime_error("Parameter datasets must be 1-D and match collector point count");
        }
        parameter_names_.push_back(name);
        ParameterDatasetView view{name, ds.getDataType(), std::nullopt, std::nullopt};
        if (ds.hasAttribute(CollectorSink::parameter_unit_attribute_name())) {
          view.unit = ds.getAttribute(CollectorSink::parameter_unit_attribute_name()).read<std::string>();
        }
        if (ds.hasAttribute(CollectorSink::parameter_description_attribute_name())) {
          view.description = ds.getAttribute(CollectorSink::parameter_description_attribute_name()).read<std::string>();
        }
        parameter_views_.push_back(std::move(view));
      }
      std::ranges::sort(parameter_names_);
      std::ranges::sort(parameter_views_, [](const auto & a, const auto & b){ return a.name < b.name; });
    }
  }

  RL_API [[nodiscard]] size_t points() const { return points_; }
  RL_API [[nodiscard]] const std::vector<Reader> & readers() const { return readers_; }
  RL_API [[nodiscard]] bool has_parameters() const { return parameters_.has_value(); }
  RL_API [[nodiscard]] const std::vector<std::string> & parameter_names() const { return parameter_names_; }
  RL_API [[nodiscard]] const std::vector<ParameterDatasetView> & parameter_views() const { return parameter_views_; }

  RL_API [[nodiscard]] const Reader & reader(const std::string & collector_name) const {
    for (const auto & reader : readers_) {
      if (reader.collector_name() == collector_name) {
        return reader;
      }
    }
    throw std::runtime_error("Requested collector reader does not exist");
  }

  RL_API [[nodiscard]] std::vector<uint8_t> parameter_value(const std::string & name, const size_t point) const {
    if (!parameters_.has_value()) {
      throw std::runtime_error("Collector file does not contain a parameters group");
    }
    if (point >= points_) {
      throw std::runtime_error("Out of bounds parameter point requested");
    }
    if (!parameters_->exist(name) || parameters_->getObjectType(name) != HighFive::ObjectType::Dataset) {
      throw std::runtime_error("Requested parameter dataset does not exist");
    }
    const auto ds = parameters_->getDataSet(name);
    const auto type = ds.getDataType();
    std::vector<uint8_t> buffer(type.getSize());
    ds.select({point}, {1}).read_raw(buffer.data(), type);
    return buffer;
  }

  RL_API [[nodiscard]] bool parameter_is_double(const std::string & name) const {
    for (const auto & parameter_view : parameter_views_) {
      if (parameter_view.name == name) {
        return parameter_view.datatype == HighFive::AtomicType<double>();
      }
    }
    return false;
  }

  RL_API [[nodiscard]] double parameter_double_value(const std::string & name, const size_t point) const {
    if (!parameters_.has_value()) {
      throw std::runtime_error("Collector file does not contain a parameters group");
    }
    if (point >= points_) {
      throw std::runtime_error("Out of bounds parameter point requested");
    }
    if (!parameters_->exist(name) || parameters_->getObjectType(name) != HighFive::ObjectType::Dataset) {
      throw std::runtime_error("Requested parameter dataset does not exist");
    }
    const auto ds = parameters_->getDataSet(name);
    const auto type = ds.getDataType();
    if (type != HighFive::AtomicType<double>()) {
      throw std::runtime_error("Requested parameter dataset does not have type double");
    }
    return ds.select({point}, {1}).read<double>();
  }

  RL_API [[nodiscard]] bool parameter_is_float(const std::string & name) const {
    for (const auto & parameter_view : parameter_views_) {
      if (parameter_view.name == name) {
        return parameter_view.datatype == HighFive::AtomicType<float>();
      }
    }
    return false;
  }
  RL_API [[nodiscard]] float parameter_float_value(const std::string & name, const size_t point) const {
    if (!parameters_.has_value()) {
      throw std::runtime_error("Collector file does not contain a parameters group");
    }
    if (point >= points_) {
      throw std::runtime_error("Out of bounds parameter point requested");
    }
    if (!parameters_->exist(name) || parameters_->getObjectType(name) != HighFive::ObjectType::Dataset) {
      throw std::runtime_error("Requested parameter dataset does not exist");
    }
    const auto ds = parameters_->getDataSet(name);
    const auto type = ds.getDataType();
    if (type != HighFive::AtomicType<float>()) {
      throw std::runtime_error("Requested parameter dataset does not have type float");
    }
    return ds.select({point}, {1}).read<float>();
  }


  RL_API [[nodiscard]] bool parameter_is_char(const std::string & name) const {
    for (const auto & parameter_view : parameter_views_) {
      if (parameter_view.name == name) {
        return parameter_view.datatype == HighFive::AtomicType<char>();
      }
    }
    return false;
  }
  RL_API [[nodiscard]] char parameter_char_value(const std::string & name, const size_t point) const {
    if (!parameters_.has_value()) {
      throw std::runtime_error("Collector file does not contain a parameters group");
    }
    if (point >= points_) {
      throw std::runtime_error("Out of bounds parameter point requested");
    }
    if (!parameters_->exist(name) || parameters_->getObjectType(name) != HighFive::ObjectType::Dataset) {
      throw std::runtime_error("Requested parameter dataset does not exist");
    }
    const auto ds = parameters_->getDataSet(name);
    const auto type = ds.getDataType();
    if (type != HighFive::AtomicType<char>()) {
      throw std::runtime_error("Requested parameter dataset does not have type char");
    }
    return ds.select({point}, {1}).read<char>();
  }

  RL_API [[nodiscard]] bool parameter_is_int(const std::string & name) const {
    for (const auto & parameter_view : parameter_views_) {
      if (parameter_view.name == name) {
        return parameter_view.datatype == HighFive::AtomicType<int>();
      }
    }
    return false;
  }
  RL_API [[nodiscard]] int parameter_int_value(const std::string & name, const size_t point) const {
    if (!parameters_.has_value()) {
      throw std::runtime_error("Collector file does not contain a parameters group");
    }
    if (point >= points_) {
      throw std::runtime_error("Out of bounds parameter point requested");
    }
    if (!parameters_->exist(name) || parameters_->getObjectType(name) != HighFive::ObjectType::Dataset) {
      throw std::runtime_error("Requested parameter dataset does not exist");
    }
    const auto ds = parameters_->getDataSet(name);
    const auto type = ds.getDataType();
    if (type != HighFive::AtomicType<int>()) {
      throw std::runtime_error("Requested parameter dataset does not have type int");
    }
    return ds.select({point}, {1}).read<int>();
  }

  RL_API [[nodiscard]] bool parameter_is_int32(const std::string & name) const {
    for (const auto & parameter_view : parameter_views_) {
      if (parameter_view.name == name) {
        return parameter_view.datatype == HighFive::AtomicType<int32_t>();
      }
    }
    return false;
  }
  RL_API [[nodiscard]] int32_t parameter_int32_value(const std::string & name, const size_t point) const {
    if (!parameters_.has_value()) {
      throw std::runtime_error("Collector file does not contain a parameters group");
    }
    if (point >= points_) {
      throw std::runtime_error("Out of bounds parameter point requested");
    }
    if (!parameters_->exist(name) || parameters_->getObjectType(name) != HighFive::ObjectType::Dataset) {
      throw std::runtime_error("Requested parameter dataset does not exist");
    }
    const auto ds = parameters_->getDataSet(name);
    const auto type = ds.getDataType();
    if (type != HighFive::AtomicType<int32_t>()) {
      throw std::runtime_error("Requested parameter dataset does not have type int32_t");
    }
    return ds.select({point}, {1}).read<int32_t>();
  }
  
  RL_API [[nodiscard]] bool parameter_is_int64(const std::string & name) const {
    for (const auto & parameter_view : parameter_views_) {
      if (parameter_view.name == name) {
        return parameter_view.datatype == HighFive::AtomicType<int64_t>();
      }
    }
    return false;
  }
  RL_API [[nodiscard]] int64_t parameter_int64_value(const std::string & name, const size_t point) const {
    if (!parameters_.has_value()) {
      throw std::runtime_error("Collector file does not contain a parameters group");
    }
    if (point >= points_) {
      throw std::runtime_error("Out of bounds parameter point requested");
    }
    if (!parameters_->exist(name) || parameters_->getObjectType(name) != HighFive::ObjectType::Dataset) {
      throw std::runtime_error("Requested parameter dataset does not exist");
    }
    const auto ds = parameters_->getDataSet(name);
    const auto type = ds.getDataType();
    if (type != HighFive::AtomicType<int64_t>()) {
      throw std::runtime_error("Requested parameter dataset does not have type int64_t");
    }
    return ds.select({point}, {1}).read<int64_t>();
  }
  
  RL_API [[nodiscard]] bool parameter_is_uint(const std::string & name) const {
    for (const auto & parameter_view : parameter_views_) {
      if (parameter_view.name == name) {
        return parameter_view.datatype == HighFive::AtomicType<unsigned int>();
      }
    }
    return false;
  }
  RL_API [[nodiscard]] unsigned int parameter_uint_value(const std::string & name, const size_t point) const {
    if (!parameters_.has_value()) {
      throw std::runtime_error("Collector file does not contain a parameters group");
    }
    if (point >= points_) {
      throw std::runtime_error("Out of bounds parameter point requested");
    }
    if (!parameters_->exist(name) || parameters_->getObjectType(name) != HighFive::ObjectType::Dataset) {
      throw std::runtime_error("Requested parameter dataset does not exist");
    }
    const auto ds = parameters_->getDataSet(name);
    const auto type = ds.getDataType();
    if (type != HighFive::AtomicType<unsigned int>()) {
      throw std::runtime_error("Requested parameter dataset does not have type unsigned int");
    }
    return ds.select({point}, {1}).read<unsigned int>();
  }
  
  RL_API [[nodiscard]] bool parameter_is_uint32(const std::string & name) const {
    for (const auto & parameter_view : parameter_views_) {
      if (parameter_view.name == name) {
        return parameter_view.datatype == HighFive::AtomicType<uint32_t>();
      }
    }
    return false;
  }
  RL_API [[nodiscard]] uint32_t parameter_uint32_value(const std::string & name, const size_t point) const {
    if (!parameters_.has_value()) {
      throw std::runtime_error("Collector file does not contain a parameters group");
    }
    if (point >= points_) {
      throw std::runtime_error("Out of bounds parameter point requested");
    }
    if (!parameters_->exist(name) || parameters_->getObjectType(name) != HighFive::ObjectType::Dataset) {
      throw std::runtime_error("Requested parameter dataset does not exist");
    }
    const auto ds = parameters_->getDataSet(name);
    const auto type = ds.getDataType();
    if (type != HighFive::AtomicType<uint32_t>()) {
      throw std::runtime_error("Requested parameter dataset does not have type uint32_t");
    }
    return ds.select({point}, {1}).read<uint32_t>();
  }
  
  RL_API [[nodiscard]] bool parameter_is_uint64(const std::string & name) const {
    for (const auto & parameter_view : parameter_views_) {
      if (parameter_view.name == name) {
        return parameter_view.datatype == HighFive::AtomicType<uint64_t>();
      }
    }
    return false;
  }
  RL_API [[nodiscard]] uint64_t parameter_uint64_value(const std::string & name, const size_t point) const {
    if (!parameters_.has_value()) {
      throw std::runtime_error("Collector file does not contain a parameters group");
    }
    if (point >= points_) {
      throw std::runtime_error("Out of bounds parameter point requested");
    }
    if (!parameters_->exist(name) || parameters_->getObjectType(name) != HighFive::ObjectType::Dataset) {
      throw std::runtime_error("Requested parameter dataset does not exist");
    }
    const auto ds = parameters_->getDataSet(name);
    const auto type = ds.getDataType();
    if (type != HighFive::AtomicType<uint64_t>()) {
      throw std::runtime_error("Requested parameter dataset does not have type uint64_t");
    }
    return ds.select({point}, {1}).read<uint64_t>();
  }
  
  RL_API [[nodiscard]] bool parameter_is_string(const std::string & name) const {
    for (const auto & parameter_view : parameter_views_) {
      if (parameter_view.name == name) {
        return parameter_view.datatype.isFixedLenStr() || parameter_view.datatype.isVariableStr();
      }
    }
    return false;
  }
  RL_API [[nodiscard]] std::string parameter_string_value(const std::string & name, const size_t point) const {
    if (!parameters_.has_value()) {
      throw std::runtime_error("Collector file does not contain a parameters group");
    }
    if (point >= points_) {
      throw std::runtime_error("Out of bounds parameter point requested");
    }
    if (!parameters_->exist(name) || parameters_->getObjectType(name) != HighFive::ObjectType::Dataset) {
      throw std::runtime_error("Requested parameter dataset does not exist");
    }
    const auto ds = parameters_->getDataSet(name);
    const auto type = ds.getDataType();
    if (!type.isFixedLenStr() && !type.isVariableStr()) {
      throw std::runtime_error("Requested parameter dataset does not have a string type");
    }
    return ds.select({point}, {1}).read<std::string>();
  }

};
