#include <algorithm>
#include <map>
#include <optional>
#include <ostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "replay.h"
#include "reader.h"
#include "Sender.h"

void StreamParameterPublisher::publish(const size_t point, const std::string & name, const std::string & value, const std::optional<std::string> & unit) {
  out_ << "point " << point << ": " << name << " = " << value;
  if (unit.has_value()) {
    out_ << " [" << unit.value() << "]";
  }
  out_ << "\n";
}

namespace {

// Runtime state for a ReplaySubset: a global index over (point, group, readout) in replay order
struct SubsetState {
  ReplaySubset spec;
  size_t index{0};
  size_t emitted{0};
  bool wants() {
    const auto i = index++;
    if (emitted >= spec.number || i < spec.first || (i - spec.first) % spec.every != 0) {
      return false;
    }
    ++emitted;
    return true;
  }
  [[nodiscard]] bool done() const { return emitted >= spec.number; }
};

std::string parameter_as_string(const ReaderSource & source, const std::string & name, const size_t point) {
  std::ostringstream s;
  if (source.parameter_is_double(name)) { s << source.parameter_double_value(name, point); }
  else if (source.parameter_is_float(name)) { s << source.parameter_float_value(name, point); }
  else if (source.parameter_is_int(name)) { s << source.parameter_int_value(name, point); }
  else if (source.parameter_is_int32(name)) { s << source.parameter_int32_value(name, point); }
  else if (source.parameter_is_int64(name)) { s << source.parameter_int64_value(name, point); }
  else if (source.parameter_is_uint(name)) { s << source.parameter_uint_value(name, point); }
  else if (source.parameter_is_uint32(name)) { s << source.parameter_uint32_value(name, point); }
  else if (source.parameter_is_uint64(name)) { s << source.parameter_uint64_value(name, point); }
  else if (source.parameter_is_char(name)) { s << source.parameter_char_value(name, point); }
  else if (source.parameter_is_string(name)) { s << source.parameter_string_value(name, point); }
  else { s << "<unsupported type>"; }
  return s.str();
}

void publish_point(const ReaderSource & source, const size_t point, ParameterPublisher & publisher) {
  for (const auto & view : source.parameter_views()) {
    publisher.publish(point, view.name, parameter_as_string(source, view.name, point), view.unit);
  }
  publisher.point_ready(point);
}

std::map<std::pair<DetectorType, ReadoutType>, Sender> make_senders(const ReaderSource & source, const ReplayConfig & config) {
  std::map<std::pair<DetectorType, ReadoutType>, Sender> senders;
  for (const auto & reader : source.readers()) {
    const auto key = std::make_pair(reader.detector_type(), reader.readout_type());
    if (senders.contains(key)) {
      continue;
    }
    auto sender_config = config.senders.find(key.first, key.second);
    if (!sender_config.has_value()) {
      sender_config = SenderConfig{key.first, key.second, config.default_address, static_cast<uint16_t>(config.default_port), 0};
    }
    senders.emplace(std::piecewise_construct, std::forward_as_tuple(key), std::forward_as_tuple(sender_config.value()));
  }
  return senders;
}

template<class EventT, std::vector<EventT> (Reader::*Get)(size_t, size_t) const>
void stream_point(const Reader & reader, Sender & sender, const size_t point, const ReplayConfig & config,
                  std::mt19937 & rng, std::optional<SubsetState> & subset) {
  const auto offset = reader.point_offset(point);
  const auto count = reader.point_size(point);
  // Only the shuffled path needs to buffer the sampled events; sequential sends immediately
  std::vector<EventT> buffered;
  auto emit = [&](const EventT & event) {
    if (config.random_order) {
      buffered.push_back(event);
    } else {
      event.add(sender);
    }
  };
  const auto chunk = std::max<size_t>(config.chunk_size, 1);
  for (size_t done = 0; done < count; done += chunk) {
    if (subset.has_value() && subset->done()) {
      break;
    }
    const auto n = std::min(chunk, count - done);
    const auto events = (reader.*Get)(offset + done, n);
    for (const auto & event : events) {
      if (subset.has_value() && !subset->wants()) {
        continue;
      }
      size_t copies{1};
      if (config.counting_time.has_value()) {
        // n_i ~ Poisson(w_i * T): per-ray draws reproduce the aggregate Poisson(W * T)
        // event count exactly, by the Poisson-multinomial decomposition
        const auto mean = event.weight * config.counting_time.value();
        if (mean > 0.) {
          std::poisson_distribution<size_t> distribution(mean);
          copies = distribution(rng);
        } else {
          copies = 0;
        }
      }
      for (size_t k = 0; k < copies; ++k) {
        emit(event);
      }
    }
  }
  if (config.random_order) {
    std::ranges::shuffle(buffered, rng);
    for (const auto & event : buffered) {
      event.add(sender);
    }
  }
}

void stream_reader_point(const Reader & reader, Sender & sender, const size_t point, const ReplayConfig & config,
                         std::mt19937 & rng, std::optional<SubsetState> & subset) {
  switch (reader.readout_type()) {
    case ReadoutType::CAEN: return stream_point<CAEN_event, &Reader::get_CAEN>(reader, sender, point, config, rng, subset);
    case ReadoutType::TTLMonitor: return stream_point<TTLMonitor_event, &Reader::get_TTLMonitor>(reader, sender, point, config, rng, subset);
    case ReadoutType::VMM3: return stream_point<VMM3_event, &Reader::get_VMM3>(reader, sender, point, config, rng, subset);
    case ReadoutType::CDT: return stream_point<CDT_event, &Reader::get_CDT>(reader, sender, point, config, rng, subset);
    case ReadoutType::BM0: return stream_point<BM0_event, &Reader::get_BM0>(reader, sender, point, config, rng, subset);
    case ReadoutType::BM2: return stream_point<BM2_event, &Reader::get_BM2>(reader, sender, point, config, rng, subset);
    case ReadoutType::BMI: return stream_point<BMI_event, &Reader::get_BMI>(reader, sender, point, config, rng, subset);
    default: throw std::runtime_error("Replay of this readout type is not implemented");
  }
}

} // namespace

void replay(const std::string & filename, const ReplayConfig & config, ParameterPublisher & publisher) {
  const ReaderSource source(filename);
  auto senders = make_senders(source, config);
  const auto seed = config.seed ? config.seed : std::random_device{}();
  std::mt19937 rng{seed};

  std::optional<SubsetState> subset{std::nullopt};
  if (config.subset.has_value()) {
    if (config.subset->every == 0) {
      throw std::runtime_error("The replay step (every) must be at least 1");
    }
    size_t total{0};
    for (const auto & reader : source.readers()) {
      total += reader.size();
    }
    if (config.subset->first >= total) {
      throw std::runtime_error("Requested first replay index is out of bounds");
    }
    subset = SubsetState{config.subset.value()};
  }

  for (size_t point = 0; point < source.points(); ++point) {
    publish_point(source, point, publisher);
    for (const auto & reader : source.readers()) {
      auto & sender = senders.at({reader.detector_type(), reader.readout_type()});
      stream_reader_point(reader, sender, point, config, rng, subset);
    }
    // a point boundary acts like a pulse: flush buffered packets and advance the pulse times
    for (auto & [key, sender] : senders) {
      sender.update_time();
    }
  }

  if (subset.has_value() && subset->emitted < subset->spec.number) {
    throw std::runtime_error("Requested replay subset exceeds available events");
  }
}

void replay(const std::string & filename, const ReplayConfig & config) {
  NullParameterPublisher publisher;
  replay(filename, config, publisher);
}

void replay_all(const std::string & filename, const std::string & address, const int port, const int control) {
  ReplayConfig config;
  config.random_order = control & RANDOM;
  config.default_address = address;
  config.default_port = port;
  replay(filename, config);
}

void replay_subset(const std::string & filename, const std::string & address, const int port, const size_t first, const size_t number, const size_t every, const int control) {
  ReplayConfig config;
  config.random_order = control & RANDOM;
  config.default_address = address;
  config.default_port = port;
  config.subset = ReplaySubset{first, number, every};
  replay(filename, config);
}
