#include <algorithm>
#include <random>

#include "replay.h"
#include "reader.h"
#include "ReadoutClass.h"

#include "streamer.h"

constexpr size_t PAGESIZE = 4u << 30;  // this should be user configurable

size_t element_size(ReadoutType type){
  switch (type){
    case ReadoutType::CAEN: return sizeof(CAEN_event);
    case ReadoutType::TTLMonitor: return sizeof(TTLMonitor_event);
    case ReadoutType::VMM3: return sizeof(VMM3_event);
    case ReadoutType::CDT: return sizeof(CDT_event);
    case ReadoutType::BM0: return sizeof(BM0_event);
    case ReadoutType::BM2: return sizeof(BM2_event);
    case ReadoutType::BMI: return sizeof(BMI_event);
    default: throw std::runtime_error("Readout type not implemented");
  }
}

bool loadable(ReadoutType type, size_t count){
  auto size = element_size(type) * count;
  return size <= PAGESIZE;
}


void load_replay_CAEN(const Reader & reader, Readout & readout, size_t first, size_t number, const std::vector<size_t> & indexes){
  auto data = reader.get_CAEN(first, number);
  for (auto i: indexes) data[i].add(readout);
}
void load_replay_TTLMonitor(const Reader & reader, Readout & readout, size_t first, size_t number, const std::vector<size_t> & indexes){
  auto data = reader.get_TTLMonitor(first, number);
  for (auto i: indexes) data[i].add(readout);
}
void load_replay_VMM3(const Reader & reader, Readout & readout, size_t first, size_t number, const std::vector<size_t> & indexes){
  auto data = reader.get_VMM3(first, number);
  for (auto i: indexes) data[i].add(readout);
}
void load_replay_CDT(const Reader & reader, Readout & readout, size_t first, size_t number, const std::vector<size_t> & indexes){
  auto data = reader.get_CDT(first, number);
  for (auto i: indexes) data[i].add(readout);
}
void load_replay_BM0(const Reader & reader, Readout & readout, size_t first, size_t number, const std::vector<size_t> & indexes){
  auto data = reader.get_BM0(first, number);
  for (auto i: indexes) data[i].add(readout);
}
void load_replay_BM2(const Reader & reader, Readout & readout, size_t first, size_t number, const std::vector<size_t> & indexes){
  auto data = reader.get_BM2(first, number);
  for (auto i: indexes) data[i].add(readout);
}
void load_replay_BMI(const Reader & reader, Readout & readout, size_t first, size_t number, const std::vector<size_t> & indexes){
  auto data = reader.get_BMI(first, number);
  for (auto i: indexes) data[i].add(readout);
}


void load_replay(const Reader & reader, Readout & readout, size_t first, size_t number, int control){
  std::vector<size_t> indexes(number);
  std::iota(indexes.begin(), indexes.end(), 0u);
  if (control & RANDOM){
    auto rng = std::default_random_engine {};
    std::shuffle(indexes.begin(), indexes.end(), rng);
  }
  switch (reader.readout_type()) {
    case ReadoutType::CAEN: return load_replay_CAEN(reader, readout, first, number, indexes);
    case ReadoutType::TTLMonitor: return load_replay_TTLMonitor(reader, readout, first, number, indexes);
    case ReadoutType::VMM3: return load_replay_VMM3(reader, readout, first, number, indexes);
    case ReadoutType::CDT: return load_replay_CDT(reader, readout, first, number, indexes);
    case ReadoutType::BM0: return load_replay_BM0(reader, readout, first, number, indexes);
    case ReadoutType::BM2: return load_replay_BM2(reader, readout, first, number, indexes);
    case ReadoutType::BMI: return load_replay_BMI(reader, readout, first, number, indexes);
    default: throw std::runtime_error("Readout type not implemented");
  }
}

void chunk_replay_CAEN(const Reader & reader, Readout & readout, const std::vector<size_t> & indexes){
  for (auto i: indexes) reader.get_CAEN(i, 1).front().add(readout);
}
void chunk_replay_TTLMonitor(const Reader & reader, Readout & readout, const std::vector<size_t> & indexes){
  for (auto i: indexes) reader.get_TTLMonitor(i, 1).front().add(readout);
}
void chunk_replay_VMM3(const Reader & reader, Readout & readout, const std::vector<size_t> & indexes){
  for (auto i: indexes) reader.get_VMM3(i, 1).front().add(readout);
}
void chunk_replay_CDT(const Reader & reader, Readout & readout, const std::vector<size_t> & indexes){
  for (auto i: indexes) reader.get_CDT(i, 1).front().add(readout);
}
void chunk_replay_BM0(const Reader & reader, Readout & readout, const std::vector<size_t> & indexes){
  for (auto i: indexes) reader.get_BM0(i, 1).front().add(readout);
}
void chunk_replay_BM2(const Reader & reader, Readout & readout, const std::vector<size_t> & indexes){
  for (auto i: indexes) reader.get_BM2(i, 1).front().add(readout);
}
void chunk_replay_BMI(const Reader & reader, Readout & readout, const std::vector<size_t> & indexes){
  for (auto i: indexes) reader.get_BMI(i, 1).front().add(readout);
}

void chunk_replay(const Reader & reader, Readout & readout, size_t first, size_t number, size_t every, int control){
  std::vector<size_t> indexes(number);
  size_t i{first};
  std::generate(indexes.begin(), indexes.end(), [&i,every](){auto j=i; i+=every; return j;});
  if (control & RANDOM){
    auto rng = std::default_random_engine {};
    std::shuffle(indexes.begin(), indexes.end(), rng);
  }
  switch (reader.readout_type()) {
    case ReadoutType::CAEN: return chunk_replay_CAEN(reader, readout, indexes);
    case ReadoutType::TTLMonitor: return chunk_replay_TTLMonitor(reader, readout, indexes);
    case ReadoutType::VMM3: return chunk_replay_VMM3(reader, readout, indexes);
    case ReadoutType::CDT: return chunk_replay_CDT(reader, readout, indexes);
    case ReadoutType::BM0: return chunk_replay_BM0(reader, readout, indexes);
    case ReadoutType::BM2: return chunk_replay_BM2(reader, readout, indexes);
    case ReadoutType::BMI: return chunk_replay_BMI(reader, readout, indexes);
    default: throw std::runtime_error("Readout type not implemented");
  }
}

void replay_all(const std::string & filename, const std::string & address, int port, int control) {
  auto reader = Reader(filename);
  auto readout = Readout(address, port, 0, reader.detector_type());
  if (loadable(reader.readout_type(), reader.size())) {
    load_replay(reader, readout, 0, reader.size(), control);
  } else {
    chunk_replay(reader, readout, 0, reader.size(), 1, control);
  }
}

void replay_subset(const std::string & filename, const std::string & address, int port, size_t first, size_t number, size_t every, int control) {
  auto reader = Reader(filename);
  auto readout = Readout(address, port, 0, reader.detector_type());
  if (loadable(reader.readout_type(), number) && 1 == every){
    load_replay(reader, readout, first, number, control);
  } else {
    chunk_replay(reader, readout, first, number, every, control);
  }
}