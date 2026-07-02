#define CATCH_CONFIG_MAIN
#include <filesystem>
#include <catch2/catch_test_macros.hpp>
#include "cluon-complete.hpp"

#include <Readout.h>
#include <Structs.h>
#include <reader.h>
#include <replay.h>
#include "test_utils.h"

#if defined(_MSC_VER) || defined(__MINGW32__)
#include <process.h>
int process_id() {return _getpid();}
#else
#include <unistd.h>
int process_id() {return static_cast<int>(getpid());}
#endif

std::string pid_filename(const std::string& base, const std::string& ext){
  return base + std::to_string(process_id()) + ext;
}

TEST_CASE("Store and retrieve CAEN packets", "[c][CAEN][io]"){
  // create a temporary filename where we can store an HDF5 file:
  namespace fs=std::filesystem;
  auto tdir = fs::temp_directory_path();
  fs::path filepath = tdir;
  filepath /= fs::path(pid_filename("replay_caen",".h5"));
  auto filename = filepath.string();

  const uint16_t max{1000};
  constexpr int detector_type{0x34};
  auto * collector = collector_new(filename.c_str(), "events", detector_type, 1u);
  REQUIRE(collector != nullptr);
  CAEN_readout_t caen_data;
  for (uint16_t i=0; i<max; ++i){
    uint8_t ring = 1;
    uint8_t fen = 0;
    uint8_t tube = 3;
    double tof = static_cast<double>(i)/static_cast<double>(max);
    caen_data.channel = tube;
    caen_data.a = i;
    caen_data.b = max - i;
    caen_data.c = 0;
    caen_data.d = 0;
    collector_add(collector, ring, fen, tof, static_cast<double>(i), static_cast<const void *>(&caen_data));
  }
  collector_free(collector);

  ReaderSource source(filename);
  const auto & reader = source.reader("events");
  REQUIRE(ReadoutType::CAEN == reader.readout_type());
  REQUIRE(max == reader.size());
  for (size_t i=0; i<reader.size(); ++i){
    auto event = reader.get_CAEN(i, 1).front();
    REQUIRE(1 == event.ring);
    REQUIRE(0 == event.fen);
    REQUIRE(3 == event.channel);
    double tof = static_cast<double>(i)/static_cast<double>(reader.size());
    REQUIRE(tof == event.time);
    REQUIRE(static_cast<double>(i) == event.weight);
    REQUIRE(i == event.a);
    REQUIRE(max - static_cast<uint16_t>(i) == event.b);
    REQUIRE(0 == event.c);
    REQUIRE(0 == event.d);
  }
}

// Test filling a file, then reading back its contents and sending them to a UDP listener ...
//
TEST_CASE("Store, replay and receive TTLMonitor packets","[c][TTLMonitor][io]"){
  // create a temporary filename where we can store an HDF5 file:
  namespace fs=std::filesystem;
  auto tdir = fs::temp_directory_path();
  fs::path filepath = tdir;
  filepath /= fs::path(pid_filename("replay_ttlmonitor",".h5"));
  auto filename = filepath.string();

  const uint16_t max{1000};
  uint32_t monitor_type{0x10};
  int monitor_port{9004};

  auto stats = std::make_shared<UDPStats>();

  cluon::UDPReceiver monitor_receiver("127.0.0.1", monitor_port,
    [stats,monitor_type](std::string && data, std::string &&, std::chrono::system_clock::time_point &&) noexcept {
      // data must contain [PacketHeaderV0, readout, readout, ...].
      auto ptr = data.data();
      auto * header = reinterpret_cast<PacketHeaderV0*>(ptr);
      REQUIRE(header->Padding0 == 0);
      REQUIRE(header->Version == 0);
      auto type = (header->CookieAndType) >> 24;
      auto cookie =  (header->CookieAndType - (type << 24));
      REQUIRE(cookie == 0x535345);  // ESS identifier
      REQUIRE(monitor_type == type);
      ptr += sizeof(PacketHeaderV0);
      size_t readout_size = sizeof(struct TTLMonitorData);
      auto readouts = (header->TotalLength - sizeof(PacketHeaderV0)) / readout_size;
      for (size_t i=0; i < readouts; ++i){
        auto *r = reinterpret_cast<TTLMonitorData *>(ptr + i * readout_size);
        REQUIRE(r->Ring == 0);
        REQUIRE(r->FEN == 100);
        REQUIRE(r->Pos == 3);
        REQUIRE((r->Channel == 1 || r->Channel == 0));
      }
      stats->packets++;
      stats->readouts += readouts;
    });
  REQUIRE(monitor_receiver.isRunning());

  char addr[] = "127.0.0.1";
  auto * collector = collector_new(filename.c_str(), "events", static_cast<int>(monitor_type), 1u);
  REQUIRE(collector != nullptr);
  TTLMonitor_readout_t ttl_data;
  for (uint16_t i=0; i<max; ++i){
    uint8_t tube = 3;
    double tof = static_cast<double>(i)/static_cast<double>(max);
    ttl_data.pos = tube;
    ttl_data.channel = 0;
    ttl_data.adc = i;
    collector_add(collector, 0, 100, tof, 0.0, static_cast<const void *>(&ttl_data));
    ttl_data.channel = 1;
    ttl_data.adc = max - i;
    collector_add(collector, 0, 100, tof, 0.0, static_cast<const void *>(&ttl_data));
  }
  collector_free(collector);

  REQUIRE(stats->readouts == 0); // No UDP communication has occurred yet
  {
    ReaderSource source(filename);
    const auto & reader = source.reader("events");
    REQUIRE(ReadoutType::TTLMonitor == reader.readout_type());
    REQUIRE(2 * max == reader.size());
  }
  // send all events in order
  replay_all(filename, addr, monitor_port, Replay::ALL & Replay::SEQUENTIAL);
  // the lambda function defined above gets called for each packet
  // and all 2 * max produced events are received
  auto expected = 2 * max;
  if (stats->readouts < expected){
    // wait a bit in case the receiver is doing something?
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  REQUIRE(stats->readouts == expected);
}
