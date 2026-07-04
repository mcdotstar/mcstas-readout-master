#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <thread>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "cluon-complete.hpp"

#include <CollectorClass.h>
#include <TypeDescriptionParser.h>
#include <readout_type_descriptions.h>
#include <hdf_interface.h>
#include <reader.h>
#include <replay.h>
#include <Readout.h>
#include "test_utils.h"

namespace {

std::string star_temp_h5(const std::string & base) {
  namespace fs = std::filesystem;
  const auto path = fs::temp_directory_path() / fs::path(base + std::to_string(::getpid()) + ".h5");
  fs::remove(path);
  return path.string();
}

struct UserRecord {
  double time;
  uint32_t pixel;
};

} // namespace

TEST_CASE("Canonical type descriptions match the registry compound types", "[star][registry][antidrift]") {
  // The description strings are what description-based collectors (and the
  // Collector{ReadoutType}.comp components) use; if they drift from the C++ event
  // structs, files stop being EFU-sendable. Enforce exact HDF5 type equality.
  const std::pair<ReadoutType, size_t> cases[] = {
    {ReadoutType::CAEN, sizeof(CAEN_event)},
    {ReadoutType::TTLMonitor, sizeof(TTLMonitor_event)},
    {ReadoutType::CDT, sizeof(CDT_event)},
    {ReadoutType::VMM3, sizeof(VMM3_event)},
    {ReadoutType::BM0, sizeof(BM0_event)},
    {ReadoutType::BM2, sizeof(BM2_event)},
    {ReadoutType::BMI, sizeof(BMI_event)},
  };
  for (const auto & [readout, event_size] : cases) {
    INFO("readout type " << readoutType_name(readout));
    const auto * description = readout_type_description(readout);
    REQUIRE(description != nullptr);
    const auto schema = parse_type_description(description);
    CHECK(schema.total_size == event_size);
    const auto parsed = build_hdf5_compound_type(schema);
    const auto registry = hdf_compound_type(readout);
    CHECK(parsed == registry);
  }
}

TEST_CASE("Description-based collector with canonical CAEN layout is sendable and replays", "[star][engine][replay]") {
  const auto filename = star_temp_h5("star_caen_");
  const uint16_t rays{500};
  {
    Collector collector(filename, "events", std::string(readout_type_description(ReadoutType::CAEN)), 1u, 0x34);
    REQUIRE(collector.record_size() == sizeof(CAEN_event));
    CAEN_event event{};
    for (uint16_t i = 0; i < rays; ++i) {
      event.ring = 1;
      event.fen = 0;
      event.time = static_cast<double>(i) / rays;
      event.weight = 1.0;
      event.channel = 3;
      event.a = i;
      event.b = rays - i;
      collector.addRecord(event.weight, &event);
    }
  }
  {
    const ReaderSource source(filename);
    const auto & reader = source.reader("events");
    // sendability comes from the datatype, even though no detector/readout attributes exist
    REQUIRE(reader.sendable_readout_type().has_value());
    CHECK(reader.sendable_readout_type().value() == ReadoutType::CAEN);
    CHECK(reader.readout_type() == ReadoutType::CAEN);
    CHECK(reader.detector_type() == DetectorType::BIFROST);
    REQUIRE(reader.type_description().has_value());
    CHECK(reader.size() == rays);
    CHECK(reader.point_weight(0) == Catch::Approx(static_cast<double>(rays)));
    // typed access round-trips the records
    const auto event = reader.get_CAEN(10, 1).front();
    CHECK(event.ring == 1);
    CHECK(event.channel == 3);
    CHECK(event.a == 10);
  }
  // a star-written file replays through the same pipeline as a typed one
  auto stats = std::make_shared<UDPStats>();
  const int port{9024};
  cluon::UDPReceiver receiver("127.0.0.1", port,
    [stats](std::string && data, std::string &&, std::chrono::system_clock::time_point &&) noexcept {
      const auto * header = reinterpret_cast<const PacketHeaderV0*>(data.data());
      stats->packets++;
      if (!ess_header_ok(header->CookieAndType, 0x34)) { stats->bad++; return; }
      stats->readouts += static_cast<int>((header->TotalLength - sizeof(PacketHeaderV0)) / sizeof(struct CaenData));
    });
  REQUIRE(receiver.isRunning());
  ReplayConfig config;
  config.default_port = port;
  replay(filename, config);
  int previous{-1};
  for (int i = 0; i < 40 && stats->readouts != previous; ++i) {
    previous = stats->readouts;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  CHECK(stats->readouts == rays);
  CHECK(stats->bad == 0);
  std::remove(filename.c_str());
}

TEST_CASE("User-described records store, read back raw, and are skipped by replay", "[star][engine][user]") {
  const auto filename = star_temp_h5("star_user_");
  const std::string description{"double time; uint32_t pixel;"};
  const uint16_t records{100};
  {
    // one user-described group and one typed CAEN group in the same file
    Collector user(filename, "extra", description, 1u);
    Collector typed(filename, "events", 0x34, 1u);
    REQUIRE(user.record_size() == sizeof(UserRecord));
    UserRecord record{};
    CAEN_readout_t data{3, 0, 0, 0, 0};
    for (uint16_t i = 0; i < records; ++i) {
      record.time = static_cast<double>(i);
      record.pixel = 7u * i;
      user.addRecord(0.5, &record);
      typed.addReadout(1, 0, record.time, 1.0, &data);
    }
  }
  {
    const ReaderSource source(filename);
    const auto & user_reader = source.reader("extra");
    CHECK(!user_reader.sendable_readout_type().has_value());
    CHECK_THROWS(user_reader.readout_type());
    REQUIRE(user_reader.type_description().has_value());
    CHECK(user_reader.type_description().value() == description);
    CHECK(user_reader.record_size() == sizeof(UserRecord));
    CHECK(user_reader.size() == records);
    CHECK(user_reader.point_weight(0) == Catch::Approx(0.5 * records));
    // raw round-trip
    const auto raw = user_reader.get_raw(10, 2);
    REQUIRE(raw.size() == 2 * sizeof(UserRecord));
    UserRecord back{};
    std::memcpy(&back, raw.data(), sizeof(UserRecord));
    CHECK(back.time == Catch::Approx(10.0));
    CHECK(back.pixel == 70u);
  }
  // replay must deliver the typed group and skip (not throw on) the user group
  auto stats = std::make_shared<UDPStats>();
  const int port{9025};
  cluon::UDPReceiver receiver("127.0.0.1", port,
    [stats](std::string && data, std::string &&, std::chrono::system_clock::time_point &&) noexcept {
      const auto * header = reinterpret_cast<const PacketHeaderV0*>(data.data());
      stats->packets++;
      if (!ess_header_ok(header->CookieAndType, 0x34)) { stats->bad++; return; }
      stats->readouts += static_cast<int>((header->TotalLength - sizeof(PacketHeaderV0)) / sizeof(struct CaenData));
    });
  REQUIRE(receiver.isRunning());
  ReplayConfig config;
  config.default_port = port;
  REQUIRE_NOTHROW(replay(filename, config));
  int previous{-1};
  for (int i = 0; i < 40 && stats->readouts != previous; ++i) {
    previous = stats->readouts;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  CHECK(stats->readouts == records);
  CHECK(stats->bad == 0);
  std::remove(filename.c_str());
}

TEST_CASE("User-described collector files concatenate into multi-point files", "[star][engine][concatenate]") {
  const auto file_a = star_temp_h5("star_cat_a_");
  const auto file_b = star_temp_h5("star_cat_b_");
  const auto file_out = star_temp_h5("star_cat_out_");
  const std::string description{"double time; uint32_t pixel;"};

  auto write_point = [&description](const std::string & filename, const double scan_val, const uint16_t records) {
    Collector collector(filename, "extra", description, 1u);
    collector.addParameter("scan_val", scan_val, std::optional<std::string>("arb"), std::nullopt);
    UserRecord record{};
    for (uint16_t i = 0; i < records; ++i) {
      record.time = static_cast<double>(i);
      record.pixel = i;
      collector.addRecord(1.0, &record);
    }
  };
  write_point(file_a, 1.0, 20);
  write_point(file_b, 2.0, 30);

  REQUIRE(concatenate_collector_files(file_out, {file_a, file_b}));
  {
    const ReaderSource source(file_out);
    REQUIRE(source.points() == 2);
    const auto & reader = source.reader("extra");
    CHECK(reader.point_size(0) == 20);
    CHECK(reader.point_size(1) == 30);
    CHECK(reader.point_weight(0) == Catch::Approx(20.0));
    CHECK(reader.point_weight(1) == Catch::Approx(30.0));
    CHECK(!reader.sendable_readout_type().has_value());
    CHECK(source.parameter_double_value("scan_val", 0) == Catch::Approx(1.0));
    CHECK(source.parameter_double_value("scan_val", 1) == Catch::Approx(2.0));
  }
  std::remove(file_a.c_str());
  std::remove(file_b.c_str());
  std::remove(file_out.c_str());
}
