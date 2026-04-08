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
int validate_collector_file(const std::string & filename);
int validate_collector_file_impl(HighFive::File & file, const std::string & filename);

bool validate_collector_files(
  const std::vector<std::string> & in_filenames, int point, int points,
  std::set<std::string> & datasets,
  std::map<std::string, size_t> & sizes,
  std::set<std::string> & valid_files,
  std::string & collector_name,
  std::set<std::string> & parameters
  );
void merge_collector_files(const std::string & out_filename, const std::vector<std::string> & in_filenames, int point, int points);
HighFive::CompoundType hdf_compound_type(ReadoutType readout);
RL_API void ensure_file_data_type(HighFive::File file, const std::string & type_name, const HighFive::CompoundType & hc_type);

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
  CollectorSink(CollectorSink &other) = delete;
  void operator=(const CollectorSink &) = delete;
  static CollectorSink * instance();
  static void destroy();

  bool is_setup() const { return filename_.has_value(); }

  void setup(const std::string& filename, const int point, const int points) {
    filename_ = filename;
    try {
      file_ = HighFive::File(filename, HighFive::File::OpenOrCreate);
    } catch (HighFive::Exception & ex) {
      std::cout << "Error opening file " << filename << " for writing:\n" << ex.what();
      throw;
    }
    if (points == 0) {
      collector_ = file_->getGroup("/");
    } else {
      std::stringstream ss;
      const auto num_digits = static_cast<int>(std::to_string(points).length());
      ss << "point_" << std::setfill('0') << std::setw(num_digits) << point;
      collector_ = file_->createGroup(ss.str());
      collector_->createAttribute("collector_type", "point");
      collector_->createAttribute<int>("scan_point", point);
    }
    auto u8str = [](const auto * p){return std::string(reinterpret_cast<const char *>(p));};

    file_->createAttribute<std::string>("program", "libreadout");
    file_->createAttribute<std::string>("version", u8str(libreadout::version::version_number));
    file_->createAttribute<std::string>("revision", u8str(libreadout::version::git_revision));
    file_->createAttribute<int>("points", points);
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
      parameters_ = collector_->createGroup("parameters");
    }
    if (!parameters_->exist(name)) {
      auto ds = parameters_->createDataSet(name, DataSpace(1), create_datatype<T>());
      ds.write(value);
      if (unit.has_value()) {
        ds.createAttribute("unit", unit.value());
      }
      if (description.has_value()) {
        ds.createAttribute("description", description.value());
      }
    } else {
      // verify that the existing dataset matches the new value's type
      auto existing_ds = parameters_->getDataSet(name);
      if (existing_ds.getDataType() != create_datatype<T>()) {
        std::cerr << "Parameter " << name << " already exists with a different type!" << std::endl;
      }
      if (unit.has_value() && !existing_ds.hasAttribute("unit") && existing_ds.getAttribute("unit").read<std::string>() != unit.value()) {
        std::cerr << "Parameter " << name << " already exists with a different unit!" << std::endl;
      }
      if (description.has_value() && !existing_ds.hasAttribute("description") && existing_ds.getAttribute("description").read<std::string>() != description.value()) {
        std::cerr << "Parameter " << name << " already exists with a different description!" << std::endl;
      }
      if (existing_ds.read<T>() != value) {
        std::cerr << "Parameter " << name << " already exists with a different value! Overwriting." << std::endl;
        existing_ds.write(value);
      }
    }
  }

  void ensure_data_type(const std::string & type_name, const HighFive::CompoundType & hc_type) const {
    if (!file_.has_value()) {
      std::cerr << "File not initialized, cannot ensure data type!" << std::endl;
      return;
    }
    ensure_file_data_type(file_.value(), type_name, hc_type);
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

    const auto compound_type = hdf_compound_type(readout_);
    sink->ensure_data_type(readoutType_name(readout_), compound_type);
    dataset_ = sink->addCollector(name_, dataspace, compound_type, props);
    dataset_->createAttribute("detector", detectorType_name(detector_));
    dataset_->createAttribute("readout", readoutType_name(readout_));
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
      dataset_->createAttribute<double>("total_weight", weight_);
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

