// Tests of the extern "C" API (readout_capi.h): configuration handles, replay
// with C callbacks, cooperative cancellation, and the combine wrappers.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "cluon-complete.hpp"

#include <Readout.h>
#include <Structs.h>
#include <CollectorClass.h>
#include <readout_capi.h>
#include <replay.h>
#include "test_utils.h"

#if defined(_MSC_VER) || defined(__MINGW32__)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

std::filesystem::path capi_temp_path(const std::string & base) {
#if defined(_MSC_VER) || defined(__MINGW32__)
  const int pid = _getpid();
#else
  const int pid = static_cast<int>(getpid());
#endif
  return std::filesystem::temp_directory_path() / (base + std::to_string(pid) + ".h5");
}

/// One single-point CAEN collector file with a "chopper_speed" parameter.
std::string write_capi_point_file(const std::string & base, const double chopper_speed, const uint16_t rays) {
  namespace fs = std::filesystem;
  const auto filepath = capi_temp_path(base);
  fs::remove(filepath);
  const auto filename = filepath.string();
  Collector collector(filename, "events", 0x34, 1u);
  collector.addParameter("chopper_speed", chopper_speed, std::optional<std::string>("Hz"), std::nullopt);
  CAEN_readout_t data{};
  for (uint16_t i = 0; i < rays; ++i) {
    data.channel = 3;
    data.a = i;
    const auto tof = static_cast<double>(i) / static_cast<double>(rays);
    collector.addReadout(1, 0, tof, 1.0, static_cast<const void *>(&data));
  }
  return filename;
}

/// A two-point file made by concatenating two single-point files.
std::string write_capi_two_point_file(const std::string & base, const uint16_t rays) {
  namespace fs = std::filesystem;
  const auto file_a = write_capi_point_file(base + "_a", 100.0, rays);
  const auto file_b = write_capi_point_file(base + "_b", 200.0, rays);
  const auto multi_path = capi_temp_path(base);
  fs::remove(multi_path);
  const auto multi = multi_path.string();
  REQUIRE(concatenate_collector_files(multi, {file_a, file_b}));
  fs::remove(fs::path(file_a));
  fs::remove(fs::path(file_b));
  return multi;
}

cluon::UDPReceiver make_caen_receiver(const int port, std::shared_ptr<UDPStats> stats) {
  return cluon::UDPReceiver("127.0.0.1", port,
    [stats](std::string && data, std::string &&, std::chrono::system_clock::time_point &&) noexcept {
      const auto * header = reinterpret_cast<const PacketHeaderV0 *>(data.data());
      stats->packets++;
      if (!ess_header_ok(header->CookieAndType, 0x34)) { stats->bad++; return; }
      stats->readouts += static_cast<int>((header->TotalLength - sizeof(PacketHeaderV0)) / sizeof(struct CaenData));
    });
}

// wait until the receiver's readout count stops changing (or the timeout passes)
int capi_settled_readouts(const std::shared_ptr<UDPStats> & stats) {
  int previous{-1};
  for (int i = 0; i < 40 && stats->readouts != previous; ++i) {
    previous = stats->readouts;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return stats->readouts;
}

struct PublishRecord {
  uint64_t point;
  std::string name;
  std::string value;
  bool has_unit;
  std::string unit;
};

/// user_data target for the C callbacks
struct CallbackRecorder {
  std::vector<PublishRecord> published;
  std::vector<uint64_t> ready;
  // stop the replay by returning nonzero from publish once this point is reached
  uint64_t reject_from_point{UINT64_MAX};
  // synchronisation for the threaded-cancel test
  std::mutex mutex;
  std::condition_variable cv;
  bool in_point_ready{false};
  bool may_continue{true};
};

int recording_publish(void * user_data, const uint64_t point, const char * name, const char * value, const char * unit) {
  auto * recorder = static_cast<CallbackRecorder *>(user_data);
  recorder->published.push_back({point, name, value, unit != nullptr, unit != nullptr ? unit : ""});
  return point >= recorder->reject_from_point ? 1 : 0;
}

int recording_point_ready(void * user_data, const uint64_t point) {
  auto * recorder = static_cast<CallbackRecorder *>(user_data);
  recorder->ready.push_back(point);
  return 0;
}

int blocking_point_ready(void * user_data, const uint64_t point) {
  auto * recorder = static_cast<CallbackRecorder *>(user_data);
  recorder->ready.push_back(point);
  std::unique_lock lock(recorder->mutex);
  recorder->in_point_ready = true;
  recorder->cv.notify_all();
  recorder->cv.wait(lock, [recorder] { return recorder->may_continue; });
  return 0;
}

} // namespace

TEST_CASE("C API version accessors", "[capi]") {
  CHECK(readout_capi_abi_version() == READOUT_CAPI_ABI_VERSION);
  REQUIRE(readout_version() != nullptr);
  CHECK(std::strlen(readout_version()) > 0);
  REQUIRE(readout_last_error() != nullptr);
}

TEST_CASE("C API handle lifecycle and setter errors", "[capi][errors]") {
  CHECK(readout_replay_set_seed(nullptr, 1u) == READOUT_ERROR);
  CHECK(std::strlen(readout_last_error()) > 0);

  auto * handle = readout_replay_create();
  REQUIRE(handle != nullptr);

  CHECK(readout_replay_set_counting_time(handle, 2.0) == READOUT_OK);
  CHECK(readout_replay_set_counting_time(handle, -1.0) == READOUT_ERROR);
  CHECK(readout_replay_clear_counting_time(handle) == READOUT_OK);
  CHECK(readout_replay_set_default_endpoint(handle, "127.0.0.1", 9000) == READOUT_OK);
  CHECK(readout_replay_set_default_endpoint(handle, nullptr, 9000) == READOUT_ERROR);
  CHECK(readout_replay_set_default_endpoint(handle, "127.0.0.1", 0) == READOUT_ERROR);
  CHECK(readout_replay_set_chunk_size(handle, 0) == READOUT_ERROR);
  CHECK(readout_replay_set_subset(handle, 0, 10, 0) == READOUT_ERROR);
  CHECK(readout_replay_set_subset(handle, 0, 10, 2) == READOUT_OK);
  CHECK(readout_replay_clear_subset(handle) == READOUT_OK);
  CHECK(readout_replay_set_pulse_rate(handle, 0.0) == READOUT_ERROR);
  CHECK(readout_replay_set_pulse_rate(handle, 14.0) == READOUT_OK);

  CHECK(readout_replay_set_senders_json(handle, "{not json") == READOUT_ERROR);
  CHECK(std::strlen(readout_last_error()) > 0);
  const char * good = R"({"senders":[{"detector_type":"BIFROST","readout_type":"CAEN","ip_address":"127.0.0.1","udp_port":9000,"tcp_port":0}]})";
  CHECK(readout_replay_set_senders_json(handle, good) == READOUT_OK);

  // stop flag plumbing
  CHECK(readout_replay_stop_requested(handle) == 0);
  readout_replay_request_stop(handle);
  CHECK(readout_replay_stop_requested(handle) == 1);
  readout_replay_reset_stop(handle);
  CHECK(readout_replay_stop_requested(handle) == 0);

  readout_replay_destroy(handle);
  readout_replay_destroy(nullptr); // must be a safe no-op
}

TEST_CASE("C API run errors identify the failure", "[capi][errors]") {
  auto * handle = readout_replay_create();
  REQUIRE(handle != nullptr);
  CHECK(readout_replay_run(handle, nullptr, nullptr, nullptr, nullptr) == READOUT_ERROR);
  CHECK(readout_replay_run(handle, "/nonexistent_capi_file.h5", nullptr, nullptr, nullptr) == READOUT_ERROR);
  CHECK(std::strlen(readout_last_error()) > 0);
  readout_replay_destroy(handle);
}

TEST_CASE("C API replay publishes parameters and sends every readout", "[capi][replay]") {
  namespace fs = std::filesystem;
  const uint16_t rays{500};
  const auto multi = write_capi_two_point_file("capi_replay", rays);

  auto stats = std::make_shared<UDPStats>();
  const int port{9030};
  auto receiver = make_caen_receiver(port, stats);
  REQUIRE(receiver.isRunning());

  auto * handle = readout_replay_create();
  REQUIRE(handle != nullptr);
  REQUIRE(readout_replay_set_default_endpoint(handle, "127.0.0.1", port) == READOUT_OK);
  REQUIRE(readout_replay_set_pulse_rate(handle, 50.0) == READOUT_OK);

  CallbackRecorder recorder;
  CHECK(readout_replay_run(handle, multi.c_str(), recording_publish, recording_point_ready, &recorder) == READOUT_OK);
  readout_replay_destroy(handle);

  // without a counting time every stored readout is sent exactly once
  CHECK(capi_settled_readouts(stats) == 2 * rays);
  CHECK(stats->bad == 0);

  REQUIRE(recorder.published.size() == 2);
  CHECK(recorder.published[0].point == 0);
  CHECK(recorder.published[0].name == "chopper_speed");
  CHECK(recorder.published[0].value == "100");
  CHECK(recorder.published[0].has_unit);
  CHECK(recorder.published[0].unit == "Hz");
  CHECK(recorder.published[1].point == 1);
  CHECK(recorder.published[1].value == "200");
  CHECK(recorder.ready == std::vector<uint64_t>{0, 1});

  fs::remove(fs::path(multi));
}

TEST_CASE("C API nonzero publish return stops the replay before the point's events", "[capi][replay][cancel]") {
  namespace fs = std::filesystem;
  const uint16_t rays{500};
  const auto multi = write_capi_two_point_file("capi_cbstop", rays);

  auto stats = std::make_shared<UDPStats>();
  const int port{9031};
  auto receiver = make_caen_receiver(port, stats);
  REQUIRE(receiver.isRunning());

  auto * handle = readout_replay_create();
  REQUIRE(handle != nullptr);
  REQUIRE(readout_replay_set_default_endpoint(handle, "127.0.0.1", port) == READOUT_OK);
  REQUIRE(readout_replay_set_pulse_rate(handle, 50.0) == READOUT_OK);

  CallbackRecorder recorder;
  recorder.reject_from_point = 1; // accept point 0, stop at point 1
  CHECK(readout_replay_run(handle, multi.c_str(), recording_publish, recording_point_ready, &recorder) == READOUT_STOPPED);
  readout_replay_destroy(handle);

  // only point 0 was replayed, and point 1 saw no point_ready after the rejected publish
  CHECK(capi_settled_readouts(stats) == rays);
  CHECK(stats->bad == 0);
  REQUIRE(recorder.published.size() == 2);
  CHECK(recorder.ready == std::vector<uint64_t>{0});

  fs::remove(fs::path(multi));
}

TEST_CASE("C API request_stop from another thread stops a blocked replay", "[capi][replay][cancel][thread]") {
  namespace fs = std::filesystem;
  const uint16_t rays{500};
  const auto multi = write_capi_two_point_file("capi_thrstop", rays);

  auto stats = std::make_shared<UDPStats>();
  const int port{9032};
  auto receiver = make_caen_receiver(port, stats);
  REQUIRE(receiver.isRunning());

  auto * handle = readout_replay_create();
  REQUIRE(handle != nullptr);
  REQUIRE(readout_replay_set_default_endpoint(handle, "127.0.0.1", port) == READOUT_OK);
  REQUIRE(readout_replay_set_pulse_rate(handle, 50.0) == READOUT_OK);

  CallbackRecorder recorder;
  recorder.may_continue = false; // block inside the first point_ready
  std::thread stopper([handle, &recorder] {
    std::unique_lock lock(recorder.mutex);
    recorder.cv.wait(lock, [&recorder] { return recorder.in_point_ready; });
    readout_replay_request_stop(handle);
    recorder.may_continue = true;
    recorder.cv.notify_all();
  });
  CHECK(readout_replay_run(handle, multi.c_str(), nullptr, blocking_point_ready, &recorder) == READOUT_STOPPED);
  stopper.join();

  // the stop landed between point_ready and the pulse start: no events at all
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  CHECK(stats->readouts == 0);
  CHECK(recorder.ready == std::vector<uint64_t>{0});

  // the stop is sticky until reset: an immediate rerun stops before anything happens
  CHECK(readout_replay_run(handle, multi.c_str(), nullptr, nullptr, nullptr) == READOUT_STOPPED);
  readout_replay_reset_stop(handle);
  CHECK(readout_replay_run(handle, multi.c_str(), nullptr, nullptr, nullptr) == READOUT_OK);
  readout_replay_destroy(handle);

  fs::remove(fs::path(multi));
}

TEST_CASE("Replay returns false when the config stop flag is pre-set", "[replay][cancel]") {
  namespace fs = std::filesystem;
  const auto filename = write_capi_point_file("cpp_prestop", 100.0, 100);
  std::atomic<bool> stop{true};
  ReplayConfig config;
  config.stop = &stop;
  CHECK_FALSE(replay(filename, config));
  stop.store(false);
  CHECK(replay(filename, config));
  fs::remove(fs::path(filename));
}

TEST_CASE("C API combine operations", "[capi][combine]") {
  namespace fs = std::filesystem;
  const uint16_t rays{100};
  const auto file_a = write_capi_point_file("capi_comb_a", 100.0, rays);
  const auto file_b = write_capi_point_file("capi_comb_b", 200.0, rays);

  CHECK(readout_validate_collector_file(file_a.c_str()) == 1);
  CHECK(readout_validate_collector_file("/nonexistent_capi_file.h5") == READOUT_ERROR);
  CHECK(std::strlen(readout_last_error()) > 0);
  CHECK(readout_validate_collector_file(nullptr) == READOUT_ERROR);

  // the two files hold different chopper_speed values (100 vs 200): concatenate to two points
  const auto out_path = capi_temp_path("capi_comb_out");
  fs::remove(out_path);
  const auto out = out_path.string();
  const std::vector<const char *> inputs{file_a.c_str(), file_b.c_str()};
  CHECK(readout_concatenate_collector_files(out.c_str(), inputs.data(), inputs.size()) == READOUT_OK);
  CHECK(readout_validate_collector_file(out.c_str()) == 2);

  // combine on the same inputs auto-selects concatenate (different parameter values)
  const auto auto_path = capi_temp_path("capi_comb_auto");
  fs::remove(auto_path);
  const auto auto_out = auto_path.string();
  CHECK(readout_combine_collector_files(auto_out.c_str(), inputs.data(), inputs.size()) == READOUT_OK);
  CHECK(readout_validate_collector_file(auto_out.c_str()) == 2);

  // append rejects different-point files, and reports failure through the C boundary
  const auto bad_path = capi_temp_path("capi_comb_bad");
  fs::remove(bad_path);
  const auto bad_out = bad_path.string();
  CHECK(readout_append_collector_files(bad_out.c_str(), inputs.data(), inputs.size(), 0) == READOUT_ERROR);
  CHECK(std::strlen(readout_last_error()) > 0);

  CHECK(readout_concatenate_collector_files(nullptr, inputs.data(), inputs.size()) == READOUT_ERROR);
  CHECK(readout_concatenate_collector_files(out.c_str(), nullptr, 2) == READOUT_ERROR);

  fs::remove(fs::path(file_a));
  fs::remove(fs::path(file_b));
  fs::remove(out_path);
  fs::remove(auto_path);
  fs::remove(bad_path);
}
