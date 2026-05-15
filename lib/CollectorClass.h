// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief UDP readout collector class
///
//===----------------------------------------------------------------------===//
#pragma once

#include <string>
#include <iomanip>
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

RL_API void ensure_file_attributes(HighFive::File & file);
RL_API void ensure_collector_group_attributes(HighFive::Group & group);
RL_API void ensure_parameter_group_attributes(HighFive::Group & group);
RL_API void ensure_dataset_attributes(HighFive::DataSet & dataset, DetectorType detector, ReadoutType readout);

RL_API std::string validate_file_attributes(const HighFive::File & file);
RL_API std::string validate_collector_group(const HighFive::Group & group);
RL_API std::string validate_collector_root(const HighFive::Group & group) ;

RL_API bool validate_collector_files(
  const std::vector<std::string> & in_filenames,
  std::set<std::string> & datasets,
  std::map<std::string, size_t> & sizes,
  std::set<std::string> & valid_files,
  std::string & collector_name,
  std::set<std::string> & parameters
  );

RL_API void append_collector_files(const std::string & out_filename, const std::vector<std::string> & in_filenames, bool reset_datasets);
RL_API void concatenate_collector_files(const std::string & out_filename, const std::vector<std::string> & in_filenames);

RL_API void merge_collector_files(const std::string & out_filename, const std::vector<std::string> & in_filenames, bool remove_after_merge);

/// \brief Merge dataset(s) from multiple collector files into a single output file, for a specified scan point if applicable.
///
/// \param out_filename the output file to create or open for writing
/// \param in_filenames the input files to merge
/// \param which_dataset if non-empty, only merge the dataset with this name (which must be present in all input files), otherwise merge all datasets which are present in all input files
/// \param remove_after_merge if true, remove the input dataset(s) after merging
///
RL_API void merge_collector_datasets(const std::string & out_filename, const std::vector<std::string> & in_filenames, const std::string & which_dataset, bool remove_after_merge);

/// \brief Copy parameters from multiple collector files into a single output file, for a specified scan point if applicable.
///
/// \param out_filename the output file to create or open for writing
/// \param in_filenames the input files to merge
///
/// A parameter group, named "parameters", which is located in the file root group or a (point, points)-based
/// collector group, will be copied from one of the input files to the output file.
RL_API void copy_collector_parameters(const std::string & out_filename, const std::vector<std::string> & in_filenames);

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
  static const std::string & type_attribute() {
    static const std::string name{"type"};
    return name;
  }
  static const std::string & collector_group_type() {
    static const std::string name{"Readouts"};
    return name;
  }
  static const std::string & readout_dataset_name() {
    static const std::string name{"readouts"};
    return name;
  }
  static const std::string & cue_dataset_name() {
    static const std::string name{"cues"};
    return name;
  }
  static const std::string & weight_dataset_name() {
    static const std::string name{"weights"};
    return name;
  }
  static const std::string & normalization_dataset_name() {
    static const std::string name{"normalizations"};
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

  static const std::string & parameter_group_type() {
    static const std::string name{"Parameters"};
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

  CollectorSink(CollectorSink &other) = delete;
  void operator=(const CollectorSink &) = delete;
  static CollectorSink * instance();
  static void destroy();

  bool is_setup() const { return filename_.has_value(); }
  [[nodiscard]] std::string current_filename() const { return filename_.value_or(""); }
  [[nodiscard]] size_t user_count() const { return users_.size(); }

  void setup(const std::string& filename) {
    filename_ = filename;
    try {
      // If the file doesn't exist, create it, otherwise open it for read/write so we can add to it
      file_ = HighFive::File(filename, HighFive::File::OpenOrCreate);
    } catch (HighFive::Exception & ex) {
      std::cout << "Error opening file " << filename << " for writing:\n" << ex.what();
      throw;
    }
    collector_ = file_->getGroup("/");
    ensure_file_attributes(*file_);
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


  RL_API std::optional<HighFive::Group> getCollector(
    const std::string& name,
    const DetectorType& detector,
    const ReadoutType& readout
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
      auto group = collector_->createGroup(name);
      group.createAttribute<std::string>(type_attribute(), collector_group_type());

      // but should chunk file operations to avoid too much disk IO?
      DataSetCreateProps props;
      props.add(Chunking(std::vector<hsize_t>{100}));

      // create the readouts dataset, set to an empty vector []
      auto ds = group.createDataSet(readout_dataset_name(), DataSpace({0}, {DataSpace::UNLIMITED}), compound_type, props);
      ensure_dataset_attributes(ds, detector, readout);
      // create the weights dataset, and set its value to [0.]
      const auto ws = group.createDataSet(weight_dataset_name(), DataSpace({1}, {DataSpace::UNLIMITED}), AtomicType<double>(), props);
      ws.select({0},{1}).write(0.);
      // create the cues dataset, and set it to [0u]
      const auto cs = group.createDataSet(cue_dataset_name(), DataSpace({1}, {DataSpace::UNLIMITED}), AtomicType<uint32_t>(), props);
      cs.select({0},{1}).write(0);
      // create the normalizations dataset, and set its value to [0]
      const auto ns = group.createDataSet(normalization_dataset_name(), DataSpace({1}, {DataSpace::UNLIMITED}), AtomicType<uint64_t>(), props);
      ns.select({0},{1}).write(0);

      users_.insert(name);
    }
    // TODO verify that the group has the right components?
    return collector_->getGroup(name);
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
      ensure_parameter_group_attributes(parameters_.value());
    }
    if (!parameters_->exist(name)) {
      DataSetCreateProps props;
      props.add(Chunking(std::vector<hsize_t>{100}));
      auto ds = parameters_->createDataSet(name, DataSpace({1}, {DataSpace::UNLIMITED}), create_datatype<T>(), props);
      ds.select({0},{1}).write(value);
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
      if (existing_ds.select({existing_ds.getDimensions().back()-1},{1}).read<T>() != value) {
        std::cerr << "Parameter " << name << " already exists with a different value! Overwriting." << std::endl;
        existing_ds.write(value);
      }
    }
  }
};


class Collector {
  std::string name_;
  std::optional<HighFive::Group> group_;
  std::optional<HighFive::DataSet> dataset_;
  double weight_{0};
  uint64_t normalization_{0};
  DetectorType detector_{DetectorType::Reserved};
  ReadoutType readout_{ReadoutType::CAEN};

public:

  RL_API explicit Collector(
    const std::string &filename,
    std::string name,
    const int Type=0x34,
    const uint64_t normalization=0
  ): name_{std::move(name)}, normalization_{normalization}, detector_{detectorType_from_int(Type)}, readout_{readoutType_from_detectorType(detector_)} {
    const auto sink = CollectorSink::instance();
    if (!sink->is_setup()) {
      sink->setup(filename);
    }
    group_ = sink->getCollector(name_, detector_, readout_);
    dataset_ = group_->getDataSet(CollectorSink::readout_dataset_name());
  }

  // Add a readout with time and weight information to the writer's storage
  RL_API void addReadout(const uint8_t Ring, const uint8_t FEN, const double tof, const double weight, const void * data) {
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
    // Add the accumulated weight to the existing dataset value -- which was initialized to [0.]
    const auto wds = group_->getDataSet(CollectorSink::weight_dataset_name());
    auto selection = wds.select({wds.getDimensions().back() - 1}, {1});
    selection.write(weight_ + selection.read<double>());
    // Record the number of readouts written -- where this points' readouts *end* (was initialized to [0u])
    if (dataset_.has_value()) {
      const auto count = dataset_->getDimensions().back();
      const auto cds = group_->getDataSet(CollectorSink::cue_dataset_name());
      auto cs = cds.select({cds.getDimensions().back()-1}, {1});
      cs.write<uint32_t>(static_cast<uint32_t>(count));
    }
    // Add the normalization to the existing dataset value -- which was initialized to [0]
    const auto nds = group_->getDataSet(CollectorSink::normalization_dataset_name());
    auto ns = nds.select({nds.getDimensions().back() - 1}, {1});
    ns.write(normalization_ + selection.read<uint64_t>());

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
    //TODO Consider buffering this internally and only writing when the buffer is full (or closing the file)
    if (!dataset_.has_value()) {
      std::cerr << "Dataset not initialized, cannot save readout!" << std::endl;
      return;
    }
    auto & d = dataset_.value();
    // the dataset should be 1-D ... hopefully that's true
    auto pos = d.getDimensions().back();
    auto size = pos + 1;
    d.resize({size});
    d.select({pos}, {1}).write(data); // select(offset, count)
  }
};


std::string filename_for_collector(const std::string & basepath, const std::string & basename, const std::string & extension = "h5");
std::string filename_for_collector_node(const std::string & basepath, const std::string & basename, int node, int nodes);

