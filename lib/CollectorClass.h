// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief UDP readout collector class
///
//===----------------------------------------------------------------------===//
#pragma once

#include <string>
#include <utility>
#include <optional>
#include <random>
#include <map>
#include <set>

#include "Structs.h"
#include "Readout.h"
#include "enums.h"
#include "hdf_interface.h"
#include "version.hpp"
#include "efu_time.h"


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

/// \brief Validate that a given file is a valid collector file, and return the number of scan points it contains (0 if it's not a point-based collector)
///
/// \returns the number of scan points in the file, or -1 if the file is not a valid collector file
RL_API int validate_collector_file(const std::string & filename);
RL_API int validate_collector_file_impl(const HighFive::File & file, const std::string & filename);

RL_API void ensure_file_attributes(HighFive::File & file, int points);
RL_API void ensure_collector_group_attributes(HighFive::Group & group, int point);
RL_API void ensure_dataset_attributes(HighFive::DataSet & dataset, DetectorType detector, ReadoutType readout);

RL_API std::string validate_is_collector_group(const HighFive::Group & group, std::optional<int> expected_point = std::nullopt, std::optional<int> total_points = std::nullopt);
RL_API std::string validate_collector_group(const HighFive::Group & group, std::optional<int> expected_point = std::nullopt, std::optional<int> total_points = std::nullopt) ;

RL_API bool validate_collector_files(
  const std::vector<std::string> & in_filenames, int point, int points,
  std::set<std::string> & datasets,
  std::map<std::string, size_t> & sizes,
  std::set<std::string> & valid_files,
  std::string & collector_name,
  std::set<std::string> & parameters
  );
RL_API void merge_collector_files(const std::string & out_filename, const std::vector<std::string> & in_filenames, int point, int points);

/// \brief Merge dataset(s) from multiple collector files into a single output file, for a specified scan point if applicable.
///
/// \param out_filename the output file to create or open for writing
/// \param in_filenames the input files to merge
/// \param point the scan point to merge, if the files are point-based collectors (ignored if the files are not point-based collectors)
/// \param points the total number of scan points in the files, if they are point-based collectors (ignored if the files are not point-based collectors)
/// \param which_dataset if non-empty, only merge the dataset with this name (which must be present in all input files), otherwise merge all datasets which are present in all input files
/// \param remove_after_merge if true, remove the input dataset(s) after merging
///
RL_API void merge_collector_datasets(const std::string & out_filename, const std::vector<std::string> & in_filenames, int point, int points, const std::string & which_dataset, bool remove_after_merge);

/// \brief Copy parameters from multiple collector files into a single output file, for a specified scan point if applicable.
///
/// \param out_filename the output file to create or open for writing
/// \param in_filenames the input files to merge
/// \param point the scan point to merge, if the files are point-based collectors (ignored if the files are not point-based collectors)
/// \param points the total number of scan points in the files, if they are point-based collectors (ignored if the files are not point-based collectors)
///
/// A parameter group, named "parameters", which is located in the file root group or a (point, points)-based
/// collector group, will be copied from one of the input files to the output file.
RL_API void copy_collector_parameters(const std::string & out_filename, const std::vector<std::string> & in_filenames, int point, int points);

HighFive::CompoundType hdf_compound_type(ReadoutType readout);


// A singleton object to hold the current runtime's output file for the collector
class CollectorSink {
protected:
  CollectorSink() = default;

  std::set<std::string> users_;
  std::optional<std::string> filename_{std::nullopt};
  std::optional<HighFive::File> file_{std::nullopt};
  std::optional<HighFive::Group> collector_, parameters_{std::nullopt};

  static CollectorSink * instance_;

public:
  static const std::string & weight_attribute_name() {
    static const std::string name{"total_weight"};
    return name;
  }

  static const std::string & detector_attribute_name() {
    static const std::string name{"detector"};
    return name;
  }

  static const std::string & readout_attribute_name() {
    static const std::string name{"readout"};
    return name;
  }

  static const std::string & parameter_group_name() {
    static const std::string name{"parameters"};
    return name;
  }
  static const std::string & parameter_unit_attribute_name() {
    static const std::string name{"unit"};
    return name;
  }
  static const std::string & parameter_description_attribute_name() {
    static const std::string name{"description"};
    return name;
  }
  static const std::string & type_attribute_name() {
    static const std::string name{"collector_type"};
    return name;
  }
  static const std::string & type_attribute_value() {
    static const std::string name{"point"};
    return name;
  }
  static const std::string & point_attribute_name() {
    static const std::string name{"scan_point"};
    return name;
  }
  static const std::string & program_attribute_name() {
    static const std::string name{"program"};
    return name;
  }
  static const std::string & version_attribute_name() {
    static const std::string name{"version"};
    return name;
  }
  static const std::string & revision_attribute_name() {
    static const std::string name{"revision"};
    return name;
  }
  static const std::string & program_attribute_value() {
    static const std::string name{"libreadout"};
    return name;
  }
  static const std::string & version_attribute_value() {
    static const std::string name{reinterpret_cast<const char *>(libreadout::version::version_number)};
    return name;
  }
  static const std::string & revision_attribute_value() {
    static const std::string name{reinterpret_cast<const char *>(libreadout::version::git_revision)};
    return name;
  }
  static const std::string & total_points_attribute_name() {
    static const std::string name{"points"};
    return name;
  }

  CollectorSink(CollectorSink &other) = delete;
  void operator=(const CollectorSink &) = delete;
  static CollectorSink * instance();
  static void destroy();

  bool is_setup() const { return filename_.has_value(); }
  [[nodiscard]] std::string current_filename() const { return filename_.value_or(""); }
  [[nodiscard]] size_t user_count() const { return users_.size(); }

  void setup(const std::string& filename, const int point, const int points) {
    filename_ = filename;
    try {
      // If the file doesn't exist, create it, otherwise open it for read/write so we can add to it
      file_ = HighFive::File(filename, HighFive::File::OpenOrCreate);
    } catch (HighFive::Exception & ex) {
      std::cout << "Error opening file " << filename << " for writing:\n" << ex.what();
      throw;
    }
    if (points == 0) {
      collector_ = file_->getGroup("/");
    } else {
      if (point < 0 || point >= points) {
        std::stringstream s;
        s << "Invalid scan point " << point << " for total points " << points << ".";
        std::cerr << std::endl << s.str() << std::endl << std::endl;
        std::cerr << "  The collector needs a valid point number and total points for point-based collectors." << std::endl;
        std::cerr << "  Valid points for total points " << points << " are [0," << points << ")." << std::endl;
        if (point >= points) std::cerr << "  E.g., points should not exceed " << points-1 << "." << std::endl;
        std::cerr << std::endl;
        throw std::runtime_error(s.str());
      }

      std::stringstream ss;
      const auto num_digits = static_cast<int>(std::to_string(points-1).length());
      ss << "point_" << std::setfill('0') << std::setw(num_digits) << point;
      const auto name = ss.str();
      bool make{true};
      if (file_->exist(name)) {
        make = false;
        collector_ = file_->getGroup(name);
        if (const auto result = validate_is_collector_group(*collector_, point, points); !result.empty()) {
          std::cerr << "Warning: existing group " << name << " in file " << filename
                    << " does not have expected structure for a collector group, overwriting." << std::endl
                    << " Details: " << result << std::endl;
          file_->unlink(name);
          make = true;
        }
      }
      if (make){
        collector_ = file_->createGroup(name);
        ensure_collector_group_attributes(*collector_, point);
      }
    }
    ensure_file_attributes(*file_, points);
  }

  void teardown() {
    if (users_.empty()) {
      if (file_.has_value()) {
        file_->flush();
        file_ = std::nullopt;
      }
      filename_ = std::nullopt;
      collector_ = std::nullopt;
      parameters_ = std::nullopt;
    }
  }

  RL_API std::optional<HighFive::DataSet> addCollector(
    const std::string& name,
    const HighFive::DataSpace& dataspace,
    const HighFive::DataType& datatype,
    const HighFive::DataSetCreateProps& props = HighFive::DataSetCreateProps()
    ) {
    using namespace HighFive;
    if (!file_.has_value()) {
      std::cerr << "File not initialized, cannot add dataset!" << std::endl;
      return std::nullopt;
    }
    if (!collector_.has_value()) {
      collector_ = file_->getGroup("/");
    }
    if (collector_->exist(name)) {
      std::cerr << "Dataset " << name << " already exists!" << std::endl;
      return collector_->getDataSet(name);
    }
    auto ds = collector_->createDataSet(name, dataspace, datatype, props);
    users_.insert(name);
    return ds;
  }

  RL_API std::optional<HighFive::DataSet> getCollector(
    const std::string& name,
    const DetectorType& detector,
    const ReadoutType& readout,
    const HighFive::DataSpace& dataspace,
    const HighFive::DataSetCreateProps& props = HighFive::DataSetCreateProps()
    ) {
    using namespace HighFive;
    if (!file_.has_value()) {
      std::cerr << "File not initialized, cannot get dataset!" << std::endl;
      return std::nullopt;
    }
    if (!collector_.has_value()) {
      collector_ = file_->getGroup("/");
    }
    if (!collector_->exist(name)) {
      const auto compound_type = hdf_compound_type(readout);
      auto ds = collector_->createDataSet(name, dataspace, compound_type, props);
      ensure_dataset_attributes(ds, detector, readout);
      users_.insert(name);
    }
    return collector_->getDataSet(name);
  }

  RL_API [[nodiscard]] auto removeCollector(const std::string& name) {
    return users_.erase(name);
  }

  template<class T>
  RL_API void addParameter(
    const std::string& name,
    T value,
    const std::optional<std::string> & unit = std::nullopt,
    const std::optional<std::string> & description = std::nullopt
    ) {
    using namespace HighFive;
    if (!file_.has_value()) {
      std::cerr << "File not initialized, cannot add parameter!" << std::endl;
      return;
    }
    if (!parameters_.has_value()) {
      parameters_ = collector_->createGroup(parameter_group_name());
    }
    if (!parameters_->exist(name)) {
      auto ds = parameters_->createDataSet(name, DataSpace(1), create_datatype<T>());
      ds.write(value);
      if (unit.has_value()) {
        ds.createAttribute(parameter_unit_attribute_name(), unit.value());
      }
      if (description.has_value()) {
        ds.createAttribute(parameter_description_attribute_name(), description.value());
      }
    } else {
      // verify that the existing dataset matches the new value's type
      auto existing_ds = parameters_->getDataSet(name);
      if (existing_ds.getDataType() != create_datatype<T>()) {
        std::cerr << "Parameter " << name << " already exists with a different type!" << std::endl;
      }
      if (const auto n = parameter_unit_attribute_name();
        unit.has_value()
        && (!existing_ds.hasAttribute(n) || existing_ds.getAttribute(n).read<std::string>() != unit.value())
        ) {
        std::cerr << "Parameter " << name << " already exists with a different unit!" << std::endl;
      }
      if (const auto n = parameter_description_attribute_name();
        description.has_value()
        && (!existing_ds.hasAttribute(n) || existing_ds.getAttribute(n).read<std::string>() != description.value())
        ) {
        std::cerr << "Parameter " << name << " already exists with a different description!" << std::endl;
      }
      if (existing_ds.read<T>() != value) {
        std::cerr << "Parameter " << name << " already exists with a different value! Overwriting." << std::endl;
        existing_ds.write(value);
      }
    }
  }
};


class Collector {
  std::string name_;
  std::optional<HighFive::DataSet> dataset_;
  double weight_{0};
  DetectorType detector_{DetectorType::Reserved};
  ReadoutType readout_{ReadoutType::CAEN};

public:

  RL_API explicit Collector(
    const std::string &filename,
    const std::string &name,
    const int scan_point=0,
    const int total_points=0,
    const int Type=0x34
  ): name_{name}, detector_{detectorType_from_int(Type)}, readout_{readoutType_from_detectorType(detector_)} {
    const auto sink = CollectorSink::instance();
    if (!sink->is_setup()) {
      sink->setup(filename, scan_point, total_points);
    }
    // we want to output an events list which can grow forever
    const auto dataspace = HighFive::DataSpace({0}, {HighFive::DataSpace::UNLIMITED});
    // but should chunk file operations to avoid too much disk IO?
    HighFive::DataSetCreateProps props;
    props.add(HighFive::Chunking(std::vector<hsize_t>{100}));

    dataset_ = sink->getCollector(name_, detector_, readout_, dataspace, props);
  }

  // Add a readout with time and weight information to the writer's storage
  RL_API void addReadout(uint8_t Ring, uint8_t FEN, const double tof, const double weight, const void * data) {
    weight_ += weight;
    switch (readout_) {
      case ReadoutType::CAEN: {
        const CAEN_event event{Ring, FEN, tof, weight, static_cast<const CAEN_readout_t*>(data)};
        return saveReadout(event);
      }
      case ReadoutType::TTLMonitor: {
        const TTLMonitor_event event{Ring, FEN, tof, weight, static_cast<const TTLMonitor_readout_t*>(data)};
        return saveReadout(event);
      }
      case ReadoutType::CDT: {
        const CDT_event event{Ring, FEN, tof, weight, static_cast<const CDT_readout_t*>(data)};
        return saveReadout(event);
      }
      case ReadoutType::VMM3: {
        const VMM3_event event{Ring, FEN, tof, weight, static_cast<const VMM3_readout_t*>(data)};
        return saveReadout(event);
      }
      case ReadoutType::BM0: {
        const BM0_event event{Ring, FEN, tof, weight, static_cast<const BM0_readout_t*>(data)};
        return saveReadout(event);
      }
      case ReadoutType::BM2: {
        const BM2_event event{Ring, FEN, tof, weight, static_cast<const BM2_readout_t*>(data)};
        return saveReadout(event);
      }
      case ReadoutType::BMI: {
        const BMI_event event{Ring, FEN, tof, weight, static_cast<const BMI_readout_t*>(data)};
        return saveReadout(event);
      }
      default:
        throw std::runtime_error("Saving this readout type is not implemented yet!");
    }
  }

  template<class ... Args> void addParameter(Args && ... args) {
    CollectorSink::instance()->addParameter(std::forward<Args>(args)...);
  }

  ~Collector() {
    // add any weights as attributes to the datasets before closing the file
    if (dataset_.has_value()) {
      // The dataset should already have a weight attribute with value 0, but we may have opened this file
      // for appending more data, in which case we need to read the existing weight, add to it, and write it back:
      if (dataset_->hasAttribute(CollectorSink::weight_attribute_name())) {
        weight_ += dataset_->getAttribute(CollectorSink::weight_attribute_name()).read<double>();
        dataset_->deleteAttribute(CollectorSink::weight_attribute_name());
      }
      dataset_->createAttribute<double>(CollectorSink::weight_attribute_name(), weight_);
    }
    const auto sink = CollectorSink::instance();
    if (const auto count = sink->removeCollector(name_); count != 1) {
      std::cerr << "Warning: removed collector " << name_ << " from sink " << count << " times, but expected to remove exactly one." << std::endl;
    }
    // try closing the sink file (internally it checks if any collectors are still using it):
    sink->teardown();
  }

private:
  template<class T>
  void saveReadout(T data) {
    if (!dataset_.has_value()) {
      std::cerr << "Dataset not initialized, cannot save readout!" << std::endl;
      return;
    }
    auto & d = dataset_.value();
    // the dataset should be 1-D ... hopefully that's true
    auto pos = d.getDimensions().back();
    auto size = pos + 1;
    d.resize({size});
    d.select({pos}, {1}).write(data
      ); // select(offset, count)
  }
};


std::string filename_for_collector(const std::string & basepath, const std::string & basename, const std::string & extension = "h5");
std::string filename_for_collector_node(const std::string & basepath, const std::string & basename, int node, int nodes);

