#include <algorithm>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "replay.h"
#include "reader.h"
#include "ReadoutClass.h"

namespace {

  /*
   * This is all wrong.
   *
   * Presently it will read and create a vector of all readouts for all collectors keeping track of the
   * collector's specific reader and the index of the readout in its dataset, then either go through all readouts
   * in order or out of order and send them all to a single Readout RMM->EFU forwarder.
   *
   * Instead, it should:
   * 1. perform a for loop over the points in the collector file
   * 2. For each point
   *  a. update the EPICS mailbox with the values of all stored parameters
   *  b. perform a for loop over the collectors, and for each collector
   *    i. loop over (possibly out of order) the readouts within this single point,
   *    ii. send each readout to a readout-type-specific Readout which was initialized for it previously
   *
   * for point in point:
   *   for parameter in parameters.at(point):
   *     update_epics(parameter.name, parameter.value);
   *   for collector in collectors:
   *     for readout in collector.readouts.at(point, ordered=in_order):
   *       send_to_readout(readouts.at(collector.readout_type), readout);
   */

struct EventRef {
  const Reader * reader;
  size_t index;
};

void append_events_for_point(const ReaderSource & source, const size_t point, std::vector<EventRef> & events) {
  for (const auto & reader : source.readers()) {
    const auto offset = reader.point_offset(point);
    const auto count = reader.point_size(point);
    for (size_t i = 0; i < count; ++i) {
      events.push_back(EventRef{&reader, offset + i});
    }
  }
}

std::vector<EventRef> collect_events(const ReaderSource & source, const std::optional<size_t> first = std::nullopt, const std::optional<size_t> number = std::nullopt, const size_t every = 1) {
  if (every == 0) {
    throw std::runtime_error("The replay step (every) must be at least 1");
  }
  std::vector<EventRef> all;
  size_t total{0};
  for (const auto & reader : source.readers()) {
    total += reader.size();
  }
  all.reserve(total);
  for (size_t point = 0; point < source.points(); ++point) {
    append_events_for_point(source, point, all);
  }

  if (!first.has_value() || !number.has_value()) {
    return all;
  }

  const auto begin = first.value();
  if (begin >= all.size()) {
    throw std::runtime_error("Requested first replay index is out of bounds");
  }

  std::vector<EventRef> subset;
  subset.reserve(number.value());
  for (size_t i = begin; i < all.size() && subset.size() < number.value(); i += every) {
    subset.push_back(all.at(i));
  }
  if (subset.size() != number.value()) {
    throw std::runtime_error("Requested replay subset exceeds available events");
  }
  return subset;
}

void add_event_to_readout(const EventRef & event, Readout & readout) {
  switch (event.reader->readout_type()) {
    case ReadoutType::CAEN: event.reader->get_CAEN(event.index, 1).front().add(readout); return;
    case ReadoutType::TTLMonitor: event.reader->get_TTLMonitor(event.index, 1).front().add(readout); return;
    case ReadoutType::VMM3: event.reader->get_VMM3(event.index, 1).front().add(readout); return;
    case ReadoutType::CDT: event.reader->get_CDT(event.index, 1).front().add(readout); return;
    case ReadoutType::BM0: event.reader->get_BM0(event.index, 1).front().add(readout); return;
    case ReadoutType::BM2: event.reader->get_BM2(event.index, 1).front().add(readout); return;
    case ReadoutType::BMI: event.reader->get_BMI(event.index, 1).front().add(readout); return;
    default: throw std::runtime_error("Readout type not implemented");
  }
}

void replay_events(std::vector<EventRef> events, const std::string & address, const int port, const int control) {
  if (events.empty()) {
    return;
  }
  if (control & RANDOM) {
    auto rng = std::default_random_engine{};
    std::ranges::shuffle(events, rng);
  }

  const auto detector = events.front().reader->detector_type();
  for (const auto &[reader, index] : events) {
    if (reader->detector_type() != detector) {
      throw std::runtime_error("Collector groups have mixed detector types, replay expects a single detector");
    }
  }

  auto readout = Readout(address, port, 0, detector);
  for (const auto & event : events) {
    add_event_to_readout(event, readout);
  }
}

} // namespace

void replay_all(const std::string & filename, const std::string & address, int port, int control) {
  const ReaderSource source(filename);
  replay_events(collect_events(source), address, port, control);
}

void replay_subset(const std::string & filename, const std::string & address, int port, size_t first, size_t number, size_t every, int control) {
  const ReaderSource source(filename);
  replay_events(collect_events(source, first, number, every), address, port, control);
}
