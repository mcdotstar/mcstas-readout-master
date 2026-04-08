#pragma once
#include <cstring>
#include "CollectorClass.h"
#include "Sender.h"
#include "ctream/algorithms.hpp"


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

class SubStreamer {
  // This class will handle streaming events for a single point & ring/FEN combination,
  // and will be owned by the main Streamer class which will handle stepping through scan points and sending parameter updates.
  HighFive::DataSet dataset;
  HighFive::DataType datatype; // file attribute
  double weight; // dataset attribute
  DetectorType detector_; // dataset attribute
  ReadoutType readout_; // dataset attribute
public:
  template <class T> using sampler_t = ctream::ReservoirSamplerWRSWRSKIP<T>;

  RL_API explicit SubStreamer(const HighFive::DataSet& dataset): dataset{dataset} {
    try {
      weight = dataset.getAttribute("total_weight").read<double>();
    } catch (HighFive::Exception & ex) {
      std::cout << "Error reading weight attribute from dataset " << dataset.getPath() << ":\n" << ex.what() << std::endl;
      weight = 0.0;
    }
    try {
      detector_ = detectorType_from_name(dataset.getAttribute("detector").read<std::string>());
      readout_ = readoutType_from_name(dataset.getAttribute("readout").read<std::string>());
    } catch (HighFive::Exception & ex) {
      std::cout << "Error determining dataset \"" << dataset.getPath() << "\" detector and readout types, with message:\n";
      std::cout << ex.what() << std::endl;
      throw std::runtime_error("Dataset missing required attributes");
    }
    datatype = dataset.getFile().getDataType(readoutType_name(readout_));
  }

  RL_API [[nodiscard]] DetectorType detector() const {return detector_;}
  RL_API [[nodiscard]] ReadoutType readout() const {return readout_;}

  RL_API sampler_t<CAEN_event> get_CAEN_sampler(const double fraction, const uint32_t seed) const {
    if (readout_ != ReadoutType::CAEN){ throw std::runtime_error("Non CAEN readout type"); }
    const auto samples = static_cast<size_t>(weight * fraction);
    return sampler_t<CAEN_event>(samples, seed);
  }
  RL_API sampler_t<TTLMonitor_event> get_TTLMonitor_sampler(const double fraction, const uint32_t seed) const {
    if (readout_ != ReadoutType::TTLMonitor){ throw std::runtime_error("Non TTLMonitor readout type"); }
    const auto samples = static_cast<size_t>(weight * fraction);
    return sampler_t<TTLMonitor_event>(samples, seed);
  }
  RL_API sampler_t<VMM3_event> get_VMM3_sampler(const double fraction, const uint32_t seed) const {
    if (readout_ != ReadoutType::VMM3) { throw std::runtime_error("Non VMM3 readout type"); }
    const auto samples = static_cast<size_t>(weight * fraction);
    return sampler_t<VMM3_event>(samples, seed);
  }
  RL_API sampler_t<CDT_event> get_CDT_sampler(const double fraction, const uint32_t seed) const {
    if (readout_ != ReadoutType::CDT) { throw std::runtime_error("Non CDT readout type"); }
    const auto samples = static_cast<size_t>(weight * fraction);
    return sampler_t<CDT_event>(samples, seed);
  }
  RL_API sampler_t<BM0_event> get_BM0_sampler(const double fraction, const uint32_t seed) const {
    if (readout_ != ReadoutType::BM0) { throw std::runtime_error("Non BM0 readout type"); }
    const auto samples = static_cast<size_t>(weight * fraction);
    return sampler_t<BM0_event>(samples, seed);
  }
  RL_API sampler_t<BM2_event> get_BM2_sampler(const double fraction, const uint32_t seed) const {
    if (readout_ != ReadoutType::BM2) { throw std::runtime_error("Non BM2 readout type"); }
    const auto samples = static_cast<size_t>(weight * fraction);
    return sampler_t<BM2_event>(samples, seed);
  }
  RL_API sampler_t<BMI_event> get_BMI_sampler(const double fraction, const uint32_t seed) const {
    if (readout_ != ReadoutType::BMI) { throw std::runtime_error("Non BMI readout type"); }
    const auto samples = static_cast<size_t>(weight * fraction);
    return sampler_t<BMI_event>(samples, seed);
  }

  RL_API [[nodiscard]] size_t size() const {
    return dataset.getDimensions().back();
  }

  template<class T>
  RL_API auto get_samples(sampler_t<T> sampler, const std::optional<size_t> chunk_size = std::nullopt) const {
    std::vector<T> chunk(chunk_size.value_or(size()));
    for (size_t offset = 0; offset < size(); offset += chunk.size()) {
      const auto count = std::min(chunk.size(), size() - offset);
      dataset.select({offset}, {count}).read_raw(chunk.data(), datatype);
      for (size_t i = 0; i < count; ++i) {
        sampler.fit(chunk[i], chunk[i].weight); // events always have individual weights
      }
    }
    return sampler.value();
  }

  /// \brief Sample events from this dataset and add them to the sender, with the number of samples determined by the fraction and the total weight of the dataset.
  ///
  /// \param sender The Sender to which the sampled events will be added.
  /// \param fraction The fraction of the total weight of the dataset to sample.
  /// \param seed The random seed for sampling.
  /// \param chunk_size Optional chunk size for reading the dataset in chunks. If not provided, the entire dataset will be read at once.
  ///
  /// The sender must be pre-configured to handle the readout type of this dataset, and the events will be added to the sender using the appropriate add method for the readout type.
  RL_API auto add_samples(Sender & sender, const double fraction, const uint32_t seed, const std::optional<size_t> chunk_size = std::nullopt) const {
    switch (readout_) {
      case ReadoutType::CAEN: {
        for (const auto & sample : get_samples(get_CAEN_sampler(fraction, seed), chunk_size)) sample.add(sender);
        break;
      }
      case ReadoutType::TTLMonitor: {
        for (const auto & sample : get_samples(get_TTLMonitor_sampler(fraction, seed), chunk_size)) sample.add(sender);
        break;
      }
      case ReadoutType::VMM3: {
        for (const auto & sample : get_samples(get_VMM3_sampler(fraction, seed), chunk_size)) sample.add(sender);
        break;
      }
      case ReadoutType::CDT: {
        for (const auto & sample : get_samples( get_CDT_sampler(fraction, seed), chunk_size)) sample.add(sender);
        break;
      }
      case ReadoutType::BM0: {
        for (const auto & sample : get_samples(get_BM0_sampler(fraction, seed), chunk_size)) sample.add(sender);
        break;
      }
      case ReadoutType::BM2: {
        for (const auto & sample : get_samples(get_BM2_sampler(fraction, seed), chunk_size)) sample.add(sender);
        break;
      }
      case ReadoutType::BMI: {
        for (const auto & sample : get_samples(get_BMI_sampler(fraction, seed), chunk_size)) sample.add(sender);
        break;
      }
      default: throw std::runtime_error("This readout type not implemented yet!");
    }
  }


};


/** \brief Streamer class for reading events from an HDF5 file produced by libreadout via the Collector class,
 * and sending them to an EFU via the Sender class.
 *
 */
class Streamer{
  std::string filename;
  std::optional<HighFive::File> file;
  std::vector<HighFive::Group> collectors;
  std::optional<HighFive::Group> parameters;
  std::optional<HighFive::DataType> datatype;
  std::set<std::string> keys;
  DetectorType detector{DetectorType::Reserved};
  ReadoutType readout{ReadoutType::CAEN};
public:
  RL_API DetectorType detector_type() const {return detector;}
  RL_API ReadoutType readout_type() const {return readout;}

  RL_API explicit Streamer(const std::string& filename): filename{filename} {
    try {
      file = HighFive::File(filename, HighFive::File::ReadOnly);  
    } catch (HighFive::Exception & ex) {
      std::cout << "Error opening file " << filename << ":\n" << ex.what();
      file = std::nullopt;
      return;
    }
    const auto total_points = validate_collector_file_impl(file.value(), filename);
    if (total_points < 0) {
      std::cout << "File " << filename << " is not a valid collector file.\n";
      file = std::nullopt;
      return;
    }
    const auto version = file->getAttribute("version").read<std::string>();
    const auto this_version = std::string(reinterpret_cast<const char *>(libreadout::version::version_number));
    if (version != this_version){
      std::cout << "The file was produced using libreadout " << version;
      std::cout << " not current " << this_version << std::endl;
    }

    const auto root = file->getGroup("/");
    if (total_points == 0) {
      collectors.push_back(root);
    } else {
      collectors.reserve(total_points);
      for (size_t i=0; i < root.getNumberObjects(); ++i) {
        if (auto name = root.getObjectName(i); root.getObjectType(name) == HighFive::ObjectType::Group) {
          if (auto group = root.getGroup(name); group.hasAttribute("scan_point")) {
            collectors.push_back(group);
          }
        }
      }
      std::ranges::sort(collectors, [](const HighFive::Group & a, const HighFive::Group & b){
        const int sa = a.getAttribute("scan_point").read<int>();
        const int sb = b.getAttribute("scan_point").read<int>();
        return sa < sb;
      });
    }
    auto get_keys = [](const HighFive::Group & group) {
      std::set<std::string> the_keys;
      for (size_t i=0; i < group.getNumberObjects(); ++i) {
        if (auto name = group.getObjectName(i); group.getObjectType(name) == HighFive::ObjectType::Dataset) {
          if (auto ds = group.getDataSet(name); ds.hasAttribute("detector") && ds.hasAttribute("readout")) {
            the_keys.insert(name);
          }
        }
      }
      return the_keys;
    };
    // The rings and fens are supposed to be constant across points:
    keys = get_keys(collectors.front());
    // Verify that assumption by checking all points and warning if it is not true:
    for (const auto & point : collectors) {
      if (get_keys(point) != keys) {
        std::cout << "Warning: rings and fens are not consistent across scan points, this may cause problems when streaming events.\n";
        break;
      }
    }
  }

  void scan(const double fraction, const uint32_t seed) const {
    // create a map of senders (typically only one, but maybe more if we've mixed readout types in the same file)
    std::map<std::pair<DetectorType, ReadoutType>, Sender> senders;
    for (const auto & key: keys) {
      try {
        SubStreamer sub(collectors.front().getDataSet(key));
        auto key_tuple = std::make_pair(sub.detector(), sub.readout());
        if (!senders.contains(key_tuple)) {
          senders[key_tuple] = Sender(sub.detector(), sub.readout());
        }
      } catch (HighFive::Exception & ex) {
        std::cout << "Dataset for key " << key << " not found in group " << collectors.front().getPath() << ":\n" << ex.what() << std::endl;
      } catch (std::exception & ex) {
        std::cout << "Error initializing sender for key " << key << " in group " << collectors.front().getPath() << ":\n" << ex.what() << std::endl;
      }
    }

    for (const auto & point : collectors) {
      send(senders, point, fraction, seed);
    }
  }

  void send(std::map<std::pair<DetectorType, ReadoutType>, Sender>& senders, const HighFive::Group & point, const double fraction, const uint32_t seed) const {
    for (const auto & key : keys) {
      try {
        SubStreamer sub(point.getDataSet(key));
        sub.add_samples(senders.at({sub.detector(), sub.readout()}), fraction, seed);
      } catch (HighFive::Exception & ex) {
        std::cout << "Dataset for key " << key << " not found in group " << point.getPath() << ":\n" << ex.what() << std::endl;
      } catch (std::exception & ex) {
        std::cout << "Error sending events for key " << key << " in group " << point.getPath() << ":\n" << ex.what() << std::endl;
      }
    }
  }


};
