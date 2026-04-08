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


class Collector {
public:
  using key_t = std::pair<uint8_t, uint8_t>; // Ring, FEN

  RL_API static std::string key_dataset_name(const key_t& key, const int num_digits_ring, const int num_digits_fen) {
    std::stringstream ss;
    ss << "ring_" << std::setfill('0') << std::setw(num_digits_ring) << static_cast<int>(key.first)
       << "_fen_" << std::setfill('0') << std::setw(num_digits_fen) << static_cast<int>(key.second);
    return ss.str();
  }
private:
  std::string filename;
  std::optional<HighFive::File> file;
  std::optional<HighFive::Group> group;
  std::optional<HighFive::DataSet> dataset;
  std::optional<HighFive::Group> parameters;
  std::map<key_t, HighFive::DataSet> datasets;
  std::map<key_t, double> weights;
  std::optional<HighFive::DataType> datatype;
  DetectorType detector{DetectorType::Reserved};
  ReadoutType readout{ReadoutType::CAEN};

public:
  RL_API explicit Collector(
    std::string filename,
    const int scan_point=0,
    const int total_points=0,
    const int rings=0,
    const int fens=0,
    std::optional<std::string> dataset_name = std::nullopt,
    const int Type=0x34
  ): filename{std::move(filename)}, detector{detectorType_from_int(Type)}, readout{readoutType_from_detectorType(detector)} {
    try {
      file = HighFive::File(filename, HighFive::File::OpenOrCreate);
    } catch (HighFive::Exception & ex) {
      std::cout << "Error opening file " << filename << " for writing:\n" << ex.what();
      file = std::nullopt;
      return;
    }
    // we want to output an events list which can grow forever
    auto dataspace = HighFive::DataSpace({0}, {HighFive::DataSpace::UNLIMITED});
    // but should chunk file operations to avoid too much disk IO?
    HighFive::DataSetCreateProps props;
    props.add(HighFive::Chunking(std::vector<hsize_t>{100}));

    // Assign useful information as attributes:
    /* FIXME C++20 has char8_t but C++17 does not, so these strings _might_ already be chars
     *       instead of unsigned chars. If that's the case this lambda is a non-op.
     */
    auto u8str = [](const auto * p){return std::string(reinterpret_cast<const char *>(p));};

    auto hc_type = hdf_compound_type();
    try {
      if (auto existing_type = file->getDataType(readoutType_name(readout)); existing_type != hc_type) {
        std::cout << "Warning: file already has a type for " << readoutType_name(readout) << " which does not match the expected structure. This may cause problems when writing events.\n";
      }
    } catch (HighFive::Exception & ex) {
      std::cout << "No existing type for " << readoutType_name(readout) << " found in file, committing new type.\n";
      hc_type.commit(file.value(), readoutType_name(readout));
      file->createAttribute<std::string>("program", "libreadout");
      file->createAttribute<std::string>("version", u8str(libreadout::version::version_number));
      file->createAttribute<std::string>("revision", u8str(libreadout::version::git_revision));
      file->createAttribute<int>("total_points", total_points);
    }
    // build and create a group
    if (total_points > 0) {
      std::stringstream ss;
      auto num_digits = static_cast<int>(std::to_string(total_points).length());
      ss << "point_" << std::setfill('0') << std::setw(num_digits) << scan_point;
      group = file.value().createGroup(ss.str());
      group->createAttribute<int>("scan_point", scan_point);
    } else {
      group = file.value().getGroup("/");
    }

    group->createAttribute<int>("rings", rings);
    group->createAttribute<int>("fens", fens);

    dataset_name = dataset_name.value_or("events");

    if (rings > 0 && fens > 0) {
      auto num_digits_ring = static_cast<int>(std::to_string(rings).length());
      auto num_digits_fen = static_cast<int>(std::to_string(fens).length());

      auto events_group = group->createGroup(dataset_name.value());

      for (int ring=0; ring < rings; ++ring) {
        for (int fen=0; fen < fens; ++fen) {
          key_t key{static_cast<uint8_t>(ring), static_cast<uint8_t>(fen)};
          auto ds_name = key_dataset_name(key, num_digits_ring, num_digits_fen);
          datasets[key] = events_group.createDataSet(ds_name, dataspace, hc_type, props);
          datasets[key].createAttribute("detector", detectorType_name(detector));
          datasets[key].createAttribute("readout", readoutType_name(readout));
          datasets[key].createAttribute<int>("ring", ring);
          datasets[key].createAttribute<int>("fen", fen);
          if (total_points > 0) {
            datasets[key].createAttribute<int>("scan_point", scan_point);
          }
          weights[key] = 0.0; // initialize weight for this dataset
        }
      }
    } else {
      dataset = group.value().createDataSet(dataset_name.value(), dataspace, hc_type, props);
      dataset->createAttribute("detector", detectorType_name(detector));
      dataset->createAttribute("readout", readoutType_name(readout));
      if (total_points > 0) {
        dataset->createAttribute<int>("scan_point", scan_point);
      }
      weights[key_t{0, 0}] = 0.0; // initialize weight for single dataset
    }
  }

  // Add a readout with time and weight information to the writer's storage
  RL_API void addReadout(uint8_t Ring, uint8_t FEN, const double tof, const double weight, const void * data) {
    const key_t key{Ring, FEN};
    if (weights.contains(key)) {
      weights[key] += weight; // accumulate weight for this dataset
    } else {
      weights[key_t{0, 0}] += weight; // no separate datasets, accumulate in default
    }
    switch (readout) {
      case ReadoutType::CAEN: {
        const CAEN_event event{Ring, FEN, tof, weight, static_cast<const CAEN_readout_t*>(data)};
        return saveReadout(key, event);
      }
      case ReadoutType::TTLMonitor: {
        const TTLMonitor_event event{Ring, FEN, tof, weight, static_cast<const TTLMonitor_readout_t*>(data)};
        return saveReadout(key, event);
      }
      case ReadoutType::CDT: {
        const CDT_event event{Ring, FEN, tof, weight, static_cast<const CDT_readout_t*>(data)};
        return saveReadout(key, event);
      }
      case ReadoutType::VMM3: {
        const VMM3_event event{Ring, FEN, tof, weight, static_cast<const VMM3_readout_t*>(data)};
        return saveReadout(key, event);
      }
      case ReadoutType::BM0: {
        const BM0_event event{Ring, FEN, tof, weight, static_cast<const BM0_readout_t*>(data)};
        return saveReadout(key, event);
      }
      case ReadoutType::BM2: {
        const BM2_event event{Ring, FEN, tof, weight, static_cast<const BM2_readout_t*>(data)};
        return saveReadout(key, event);
      }
      case ReadoutType::BMI: {
        const BMI_event event{Ring, FEN, tof, weight, static_cast<const BMI_readout_t*>(data)};
        return saveReadout(key, event);
      }
      default:
        throw std::runtime_error("Saving this readout type is not implemented yet!");
    }
  }

  ~Collector() {
    // add any weights as attributes to the datasets before closing the file
    if (datasets.size() > 0) {
      for (auto& [key, ds] : datasets) {
        ds.createAttribute<double>("total_weight", weights[key]);
      }
    } else if (dataset.has_value()) {
      dataset->createAttribute<double>("total_weight", weights[key_t{0, 0}]);
    }
  }

  template<class T>
  RL_API void addParameter(
    const std::string& name,
    T value,
    const std::optional<std::string> & unit = std::nullopt,
    const std::optional<std::string> & description = std::nullopt
    ) {
    using namespace HighFive;
    if (!file.has_value()) {
      std::cerr << "File not initialized, cannot add parameter!" << std::endl;
      return;
    }
    if (!parameters.has_value()) {
      parameters = group->createGroup("parameters");
    }
    auto ds = parameters->createDataSet(name, DataSpace(1), create_datatype<T>());
    ds.write(value);
    if (unit.has_value()) {
      ds.createAttribute("unit", unit.value());
    }
    if (description.has_value()) {
      ds.createAttribute("description", description.value());
    }
  }

  static void merge_files(const std::string & out_filename, const std::vector<std::string> & in_filenames, int scan_point, int total_points) {
    // This function would implement merging of multiple files into one, handling the combination of datasets and attributes appropriately.
    // The implementation would depend on the specific requirements for merging (e.g., how to handle overlapping datasets, how to combine weights, etc.).
    // For now, we can just print a message indicating that this function is a placeholder.
    std::cout << "Merging files into " << out_filename << " with scan point " << scan_point << " and total points " << total_points << ".\n";
    // Actual merging logic would go here.
  }

private:
  template<class T>
  void saveReadout(const key_t key, T data) {
    auto & ds = dataset;
    if (datasets.contains(key)) {
      ds = datasets[key];
    }
    if (!ds.has_value()) {
      std::cerr << "Dataset not initialized, cannot save readout!" << std::endl;
      return;
    }
    auto & d = ds.value();
    // the dataset should be 1-D ... hopefully that's true
    auto pos = d.getDimensions().back();
    auto size = pos + 1;
    d.resize({size});
    d.select({pos}, {1}).write(data); // select(offset, count)

  }

  HighFive::CompoundType hdf_compound_type() const {
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

};
