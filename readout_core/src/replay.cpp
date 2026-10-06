#include <algorithm>
#include <map>
#include <variant>
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

inline bool stop_requested(const ReplayConfig & config) {
  return config.stop != nullptr && config.stop->load(std::memory_order_relaxed);
}

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

/// Resolve the SenderConfig for a given reader using three-level precedence:
///  1. Explicit config.senders entry for (detector, readout) — highest precedence
///  2. EFU address + port attributes embedded in the reader's collector group
///  3. config.default_address / config.default_port — lowest precedence
SenderConfig resolve_sender_config(const Reader & reader, const ReplayConfig & config) {
  // only called for sendable readers, where the datatype-verified readout type exists
  const auto readout = reader.sendable_readout_type().value();
  auto cfg = config.senders.find(reader.detector_type(), readout);
  if (cfg.has_value()) {
    return cfg.value();
  }
  if (reader.efu_address().has_value() && reader.efu_port().has_value()) {
    return SenderConfig{reader.detector_type(), readout,
                        reader.efu_address().value(), reader.efu_port().value(), 0};
  }
  return SenderConfig{reader.detector_type(), readout,
                      config.default_address, static_cast<uint16_t>(config.default_port), 0};
}

/// Build a Sender for each unique resolved SenderConfig across all readers.
/// Readers that resolve to the same SenderConfig share one Sender (e.g. two groups
/// aimed at the same EFU, or an explicit config override that collapses multiple groups).
std::map<SenderConfig, Sender> make_senders(const ReaderSource & source, const ReplayConfig & config) {
  const auto grid = pulse_grid::from_rate(config.pulse_rate);
  std::map<SenderConfig, Sender> senders;
  for (const auto & reader : source.readers()) {
    if (!reader.sendable_readout_type().has_value()) {
      continue;
    }
    const auto cfg = resolve_sender_config(reader, config);
    if (!senders.contains(cfg)) {
      auto [it, inserted] = senders.emplace(std::piecewise_construct, std::forward_as_tuple(cfg), std::forward_as_tuple(cfg, grid));
      it->second.fold_tof(config.fold_tof);
    }
  }
  return senders;
}

/// Sample one collector group's readouts for one point, handing each event to be sent to
/// ``deliver`` -- in stored order, or shuffled with config.random_order.
template<class EventT, std::vector<EventT> (Reader::*Get)(size_t, size_t) const, class Deliver>
void stream_point(const Reader & reader, const size_t point, const ReplayConfig & config,
                  std::mt19937 & rng, std::optional<SubsetState> & subset, Deliver && deliver) {
  const auto offset = reader.point_offset(point);
  const auto count = reader.point_size(point);
  // stored weights carry the simulated-ray count (w_i = p_i * ncount, accumulated across
  // appends), so the physical rate of a record is w_i / normalization
  double normalization{1.};
  if (config.counting_time.has_value() && count > 0) {
    normalization = static_cast<double>(reader.point_normalization(point));
    if (!(normalization > 0.)) {
      throw std::runtime_error("Collector point has no normalization; cannot Poisson-sample event counts");
    }
  }
  // Only the shuffled path needs to buffer the sampled events; sequential sends immediately
  std::vector<EventT> buffered;
  auto emit = [&](const EventT & event) {
    if (config.random_order) {
      buffered.push_back(event);
    } else {
      deliver(event);
    }
  };
  const auto chunk = std::max<size_t>(config.chunk_size, 1);
  for (size_t done = 0; done < count; done += chunk) {
    if (stop_requested(config)) {
      return;
    }
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
        // n_i ~ Poisson(w_i / N * T): per-ray draws reproduce the aggregate Poisson(W / N * T)
        // event count exactly, by the Poisson-multinomial decomposition
        const auto mean = event.weight / normalization * config.counting_time.value();
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
  if (config.random_order && !stop_requested(config)) {
    std::ranges::shuffle(buffered, rng);
    for (const auto & event : buffered) {
      deliver(event);
    }
  }
}

/// stream_point for whichever readout type ``reader`` stores; ``deliver`` is called with
/// each event as its own type, so it has to accept any of them.
template<class Deliver>
void stream_reader_point(const Reader & reader, const size_t point, const ReplayConfig & config,
                         std::mt19937 & rng, std::optional<SubsetState> & subset, Deliver && deliver) {
  // dispatch on the datatype-verified type, never the (optional, unverified) attribute
  switch (reader.sendable_readout_type().value()) {
    case ReadoutType::CAEN: return stream_point<CAEN_event, &Reader::get_CAEN>(reader, point, config, rng, subset, deliver);
    case ReadoutType::VMM3: return stream_point<VMM3_event, &Reader::get_VMM3>(reader, point, config, rng, subset, deliver);
    case ReadoutType::CDT: return stream_point<CDT_event, &Reader::get_CDT>(reader, point, config, rng, subset, deliver);
    case ReadoutType::BM0: return stream_point<BM0_event, &Reader::get_BM0>(reader, point, config, rng, subset, deliver);
    case ReadoutType::BM2: return stream_point<BM2_event, &Reader::get_BM2>(reader, point, config, rng, subset, deliver);
    case ReadoutType::BMI: return stream_point<BMI_event, &Reader::get_BMI>(reader, point, config, rng, subset, deliver);
    default: throw std::runtime_error("Replay of this readout type is not implemented");
  }
}

/// One pulse's events for one collector group, whatever type they are
using PulseEvents = std::variant<std::vector<CAEN_event>, std::vector<VMM3_event>, std::vector<CDT_event>,
                                 std::vector<BM0_event>, std::vector<BM2_event>, std::vector<BMI_event>>;

/// Put ``event`` in ``pulse``, which holds nothing yet or events of the same type
template<class EventT>
void hold(PulseEvents & pulse, const EventT & event) {
  if (!std::holds_alternative<std::vector<EventT>>(pulse)) {
    pulse = std::vector<EventT>{};
  }
  std::get<std::vector<EventT>>(pulse).push_back(event);
}

/// Begin one pulse at the same grid tick for every sender, and tell every EFU it began
uint64_t begin_shared_pulse(std::map<SenderConfig, Sender> & senders, const bool announce) {
  // Each beginning it in turn would wait for its own next tick, a period apart.
  uint64_t tick{0};
  for (const auto & [key, sender] : senders) {
    tick = std::max(tick, sender.next_pulse());
  }
  for (auto & [key, sender] : senders) {
    sender.begin_pulse_at(tick);
    if (announce) {
      sender.announce_pulse();
    }
  }
  return tick;
}

/// Send one point's events spread over config.pulses_per_point pulses.
///
/// Each sampled event goes to one of the point's pulses at random, so each pulse carries an
/// independent share -- by Poisson thinning, with counting_time, a Poisson count of its own.
/// Every pulse begins at one grid tick for all senders and is announced to every EFU, even
/// one with nothing to receive in it, so each EFU sees exactly pulses_per_point pulses per
/// point. An EFU summing that many pulses into a histogram then publishes one per point.
bool replay_paced_point(const ReaderSource & source, std::map<SenderConfig, Sender> & senders,
                        const size_t point, const ReplayConfig & config, std::mt19937 & rng,
                        std::optional<SubsetState> & subset, ParameterPublisher & publisher) {
  const auto pulses = config.pulses_per_point;
  std::uniform_int_distribution<size_t> which(0, pulses - 1);
  // sampled before the first pulse begins, so sending a pulse is only sending
  std::vector<std::pair<Sender *, std::vector<PulseEvents>>> groups;
  for (const auto & reader : source.readers()) {
    if (!reader.sendable_readout_type().has_value()) {
      continue;
    }
    auto & group = groups.emplace_back(&senders.at(resolve_sender_config(reader, config)),
                                       std::vector<PulseEvents>(pulses));
    stream_reader_point(reader, point, config, rng, subset,
                        [&](const auto & event) { hold(group.second[which(rng)], event); });
  }
  if (stop_requested(config)) {
    return false;
  }
  for (size_t pulse = 0; pulse < pulses; ++pulse) {
    const auto tick = begin_shared_pulse(senders, true);
    if (pulse == 0 && !senders.empty()) {
      publisher.pulse_ready(point, tick);
    }
    if (stop_requested(config)) {
      return false;
    }
    for (auto & [sender, events] : groups) {
      std::visit([&](const auto & held) {
        for (const auto & event : held) {
          event.add(*sender);
        }
      }, events[pulse]);
    }
  }
  return true;
}

} // namespace

bool replay(const std::string & filename, const ReplayConfig & config, ParameterPublisher & publisher) {
  if (!(config.pulse_rate > 0.)) {
    throw std::runtime_error("The pulse rate must be positive");
  }
  const ReaderSource source(filename);
  auto senders = make_senders(source, config);
  // a paced replay puts every event in the pulse it chose for it
  for (auto & [key, sender] : senders) {
    sender.hold_pulse(config.pulses_per_point > 0);
  }
  const auto seed = config.seed ? config.seed : std::random_device{}();
  std::mt19937 rng{seed};

  std::optional<SubsetState> subset{std::nullopt};
  if (config.subset.has_value()) {
    if (config.subset->every == 0) {
      throw std::runtime_error("The replay step (every) must be at least 1");
    }
    size_t total{0};
    for (const auto & reader : source.readers()) {
      if (reader.sendable_readout_type().has_value()) {
        total += reader.size();
      }
    }
    if (config.subset->first >= total) {
      throw std::runtime_error("Requested first replay index is out of bounds");
    }
    subset = SubsetState{config.subset.value()};
  }

  for (const auto & reader : source.readers()) {
    if (!reader.sendable_readout_type().has_value()) {
      std::cout << "Collector group \"" << reader.collector_name()
                << "\" stores user-described records that match no EFU readout type; skipping." << std::endl;
    }
  }

  for (size_t point = 0; point < source.points(); ++point) {
    if (stop_requested(config)) {
      return false;
    }
    publish_point(source, point, publisher);
    // a stop requested while the point's parameters were published suppresses
    // every event of that point
    if (stop_requested(config)) {
      return false;
    }
    if (config.pulses_per_point > 0) {
      if (!replay_paced_point(source, senders, point, config, rng, subset, publisher)) {
        return false;
      }
      continue;
    }
    // start a fresh pulse only after the point's parameters are published, so
    // the parameter timestamps precede the reference times of the point's events;
    // one tick for every sender, so all the point's events share a reference time
    const auto tick = begin_shared_pulse(senders, false);
    // Every sender keeps the same epoch-anchored grid and has just begun the same
    // pulse -- the next tick after the parameters were published -- so it is the
    // point's reference. With no senders there is no pulse to report.
    if (!senders.empty()) {
      publisher.pulse_ready(point, tick);
      if (stop_requested(config)) {
        return false;
      }
    }
    for (const auto & reader : source.readers()) {
      if (!reader.sendable_readout_type().has_value()) {
        continue;
      }
      auto & sender = senders.at(resolve_sender_config(reader, config));
      stream_reader_point(reader, point, config, rng, subset,
                          [&](const auto & event) { event.add(sender); });
    }
  }

  if (stop_requested(config)) {
    return false;
  }
  if (config.pulses_per_point > 0 && !senders.empty()) {
    // One more pulse, with nothing in it: an EFU summing pulses into histograms
    // publishes the last point's only when it sees the pulse after its last
    begin_shared_pulse(senders, true);
  }
  if (subset.has_value() && subset->emitted < subset->spec.number) {
    throw std::runtime_error("Requested replay subset exceeds available events");
  }
  return true;
}

bool replay(const std::string & filename, const ReplayConfig & config) {
  NullParameterPublisher publisher;
  return replay(filename, config, publisher);
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
