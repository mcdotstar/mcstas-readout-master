#define CATCH_CONFIG_MAIN
#include <cmath>
#include <filesystem>
#include <tuple>
#include <catch2/catch_test_macros.hpp>
#include "cluon-complete.hpp"

#include <Readout.h>
#include <Structs.h>
#include <CollectorClass.h>
#include <reader.h>
#include <replay.h>
#include <SenderConfigs.h>
#include "test_utils.h"

#if defined(_MSC_VER) || defined(__MINGW32__)
#include <process.h>
#include <winsock2.h>
#include <ws2tcpip.h>
int process_id() {return _getpid();}
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
int process_id() {return static_cast<int>(getpid());}
#endif

std::string pid_filename(const std::string& base, const std::string& ext){
  return base + std::to_string(process_id()) + ext;
}

#if defined(_WIN32)
static int find_free_udp_port() {
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    return -1;
  }
  SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock == INVALID_SOCKET) {
    WSACleanup();
    return -1;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(0);
  if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
    closesocket(sock);
    WSACleanup();
    return -1;
  }
  int len = static_cast<int>(sizeof(addr));
  if (getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &len) == SOCKET_ERROR) {
    closesocket(sock);
    WSACleanup();
    return -1;
  }
  const int port = ntohs(addr.sin_port);
  closesocket(sock);
  WSACleanup();
  return port;
}
#else
static int find_free_udp_port() {
  const int sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    return -1;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(0);
  if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    close(sock);
    return -1;
  }
  socklen_t len = sizeof(addr);
  if (getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &len) == -1) {
    close(sock);
    return -1;
  }
  const int port = ntohs(addr.sin_port);
  close(sock);
  return port;
}
#endif

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

namespace {

class RecordingPublisher final : public ParameterPublisher {
public:
  std::vector<std::tuple<size_t, std::string, std::string, std::optional<std::string>>> published;
  std::vector<size_t> ready;
  void publish(const size_t point, const std::string & name, const std::string & value, const std::optional<std::string> & unit) override {
    published.emplace_back(point, name, value, unit);
  }
  void point_ready(const size_t point) override { ready.push_back(point); }
};

// wait until the receiver's readout count stops changing (or the timeout passes)
int settled_readouts(const std::shared_ptr<UDPStats> & stats) {
  int previous{-1};
  for (int i = 0; i < 40 && stats->readouts != previous; ++i) {
    previous = stats->readouts;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return stats->readouts;
}

std::string write_caen_point_file(const std::string & base, const double chopper_speed, const uint16_t rays, const double weight) {
  namespace fs = std::filesystem;
  auto filepath = fs::temp_directory_path() / fs::path(pid_filename(base, ".h5"));
  fs::remove(filepath);
  const auto filename = filepath.string();
  Collector collector(filename, "events", 0x34, 1u);
  collector.addParameter("chopper_speed", chopper_speed, std::optional<std::string>("Hz"), std::nullopt);
  CAEN_readout_t data;
  for (uint16_t i = 0; i < rays; ++i) {
    data.channel = 3;
    data.a = i;
    data.b = rays - i;
    data.c = 0;
    data.d = 0;
    const auto tof = static_cast<double>(i) / static_cast<double>(rays);
    collector.addReadout(1, 0, tof, weight, static_cast<const void *>(&data));
  }
  return filename;
}

/// Write a single file with two named CAEN collector groups pointing at different EFU ports.
/// Returns the filename. The "left" group gets \p left_count readouts at \p left_port,
/// and "right" gets \p right_count readouts at \p right_port.
/// Both groups share a single parameter "scan_val" so the file is also concatenatable.
std::string write_two_group_file(const std::string & base,
                                 uint16_t left_count, int left_port,
                                 uint16_t right_count, int right_port) {
  namespace fs = std::filesystem;
  auto filepath = fs::temp_directory_path() / fs::path(pid_filename(base, ".h5"));
  fs::remove(filepath);
  const auto filename = filepath.string();
  {
    Collector left(filename, "left", 0x34, 1u);
    left.setEFU("127.0.0.1", left_port);
    left.addParameter("scan_val", 1.0, std::optional<std::string>("arb"), std::nullopt);
    Collector right(filename, "right", 0x34, 1u);
    right.setEFU("127.0.0.1", right_port);
    right.addParameter("scan_val", 1.0, std::optional<std::string>("arb"), std::nullopt);
    CAEN_readout_t data{};
    for (uint16_t i = 0; i < left_count; ++i) {
      data.channel = 1; data.a = i;
      left.addReadout(0, 0, static_cast<double>(i), 1.0, static_cast<const void *>(&data));
    }
    for (uint16_t i = 0; i < right_count; ++i) {
      data.channel = 2; data.a = i;
      right.addReadout(0, 0, static_cast<double>(i), 1.0, static_cast<const void *>(&data));
    }
  }
  return filename;
}

} // namespace

TEST_CASE("Multi-point replay samples Poisson events and publishes parameters", "[replay][points][statistics]") {
  namespace fs = std::filesystem;
  const uint16_t rays{1000};
  const double weight{1.0};
  // two single-point files with consistent-but-not-identical parameters, concatenated to two points
  const auto file_a = write_caen_point_file("replay_point_a", 100.0, rays, weight);
  const auto file_b = write_caen_point_file("replay_point_b", 200.0, rays, weight);
  auto multi_path = fs::temp_directory_path() / fs::path(pid_filename("replay_multi", ".h5"));
  fs::remove(multi_path);
  const auto multi = multi_path.string();
  concatenate_collector_files(multi, {file_a, file_b});

  {
    const ReaderSource source(multi);
    REQUIRE(source.points() == 2);
    REQUIRE(source.readers().size() == 1);
    REQUIRE(source.has_parameters());
    REQUIRE(source.parameter_names() == std::vector<std::string>{"chopper_speed"});
    const auto & reader = source.reader("events");
    REQUIRE(reader.point_size(0) == rays);
    REQUIRE(reader.point_size(1) == rays);
  }

  auto stats = std::make_shared<UDPStats>();
  const int port{9016};
  cluon::UDPReceiver receiver("127.0.0.1", port,
    [stats](std::string && data, std::string &&, std::chrono::system_clock::time_point &&) noexcept {
      const auto * header = reinterpret_cast<const PacketHeaderV0*>(data.data());
      stats->packets++;
      if (!ess_header_ok(header->CookieAndType, 0x34)) { stats->bad++; return; }
      stats->readouts += static_cast<int>((header->TotalLength - sizeof(PacketHeaderV0)) / sizeof(struct CaenData));
    });
  REQUIRE(receiver.isRunning());

  // total stored rate is 2 * rays * weight; a 5 s counting time expects 10000 events
  const double counting_time{5.0};
  const double mean = 2.0 * rays * weight * counting_time;

  ReplayConfig config;
  config.counting_time = counting_time;
  config.seed = 424242u;
  config.default_port = port;
  RecordingPublisher publisher;
  replay(multi, config, publisher);

  // the per-ray Poisson draws must reproduce the aggregate Poisson(W * T) count: check a 5 sigma window
  const auto received = settled_readouts(stats);
  const auto tolerance = 5.0 * std::sqrt(mean);
  CHECK(std::abs(static_cast<double>(received) - mean) < tolerance);
  CHECK(stats->bad == 0);

  // each point's parameters were published, in point order, before its readouts were sent
  REQUIRE(publisher.published.size() == 2);
  CHECK(publisher.published[0] == std::make_tuple(size_t{0}, std::string("chopper_speed"), std::string("100"), std::optional<std::string>("Hz")));
  CHECK(publisher.published[1] == std::make_tuple(size_t{1}, std::string("chopper_speed"), std::string("200"), std::optional<std::string>("Hz")));
  CHECK(publisher.ready == std::vector<size_t>{0, 1});

  // without a counting time, every stored readout is sent exactly once
  auto all_stats = std::make_shared<UDPStats>();
  const int all_port{9017};
  cluon::UDPReceiver all_receiver("127.0.0.1", all_port,
    [all_stats](std::string && data, std::string &&, std::chrono::system_clock::time_point &&) noexcept {
      const auto * header = reinterpret_cast<const PacketHeaderV0*>(data.data());
      all_stats->packets++;
      if (!ess_header_ok(header->CookieAndType, 0x34)) { all_stats->bad++; return; }
      all_stats->readouts += static_cast<int>((header->TotalLength - sizeof(PacketHeaderV0)) / sizeof(struct CaenData));
    });
  REQUIRE(all_receiver.isRunning());
  ReplayConfig all_config;
  all_config.default_port = all_port;
  replay(multi, all_config);
  CHECK(settled_readouts(all_stats) == 2 * rays);
  CHECK(all_stats->bad == 0);

  fs::remove(fs::path(file_a));
  fs::remove(fs::path(file_b));
  fs::remove(multi_path);
}

TEST_CASE("Replay with negligible counting time sends no events", "[replay][statistics]") {
  namespace fs = std::filesystem;
  const uint16_t rays{100};
  const auto file_a = write_caen_point_file("replay_zero_a", 100.0, rays, 1.0);

  auto stats = std::make_shared<UDPStats>();
  const int port{9018};
  cluon::UDPReceiver receiver("127.0.0.1", port,
    [stats](std::string && data, std::string &&, std::chrono::system_clock::time_point &&) noexcept {
      const auto * header = reinterpret_cast<const PacketHeaderV0*>(data.data());
      stats->packets++;
      if (!ess_header_ok(header->CookieAndType, 0x34)) { stats->bad++; return; }
      stats->readouts += static_cast<int>((header->TotalLength - sizeof(PacketHeaderV0)) / sizeof(struct CaenData));
    });
  REQUIRE(receiver.isRunning());

  ReplayConfig config;
  config.counting_time = 1e-12;
  config.seed = 424242u;
  config.default_port = port;
  replay(file_a, config);

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  CHECK(stats->readouts == 0);
  CHECK(stats->bad == 0);

  fs::remove(fs::path(file_a));
}

TEST_CASE("Replay routes two groups to their file-embedded EFU ports", "[replay][routing][efu]") {
  // File with a "left" group (300 readouts, port 9020) and a "right" group (700 readouts, port 9021).
  // Both groups share CAEN detector type; with the new per-reader resolution they must be sent
  // to separate ports derived from the embedded attributes.
  namespace fs = std::filesystem;
  const uint16_t left_count{300};
  const uint16_t right_count{700};
  const int left_port = find_free_udp_port();
  REQUIRE(left_port > 0);
  int right_port = find_free_udp_port();
  REQUIRE(right_port > 0);
  if (right_port == left_port) {
    right_port = find_free_udp_port();
    REQUIRE(right_port > 0);
    REQUIRE(right_port != left_port);
  }
  const auto filename = write_two_group_file("replay_route", left_count, left_port, right_count, right_port);

  auto left_stats  = std::make_shared<UDPStats>();
  auto right_stats = std::make_shared<UDPStats>();

  auto make_receiver = [](int port, std::shared_ptr<UDPStats> stats) {
    return cluon::UDPReceiver("127.0.0.1", port,
      [stats](std::string && data, std::string &&, std::chrono::system_clock::time_point &&) noexcept {
        const auto * header = reinterpret_cast<const PacketHeaderV0*>(data.data());
        stats->packets++;
        if (!ess_header_ok(header->CookieAndType, 0x34)) { stats->bad++; return; }
        stats->readouts += static_cast<int>((header->TotalLength - sizeof(PacketHeaderV0)) / sizeof(struct CaenData));
      });
  };

  cluon::UDPReceiver left_recv  = make_receiver(left_port,  left_stats);
  cluon::UDPReceiver right_recv = make_receiver(right_port, right_stats);
  REQUIRE(left_recv.isRunning());
  REQUIRE(right_recv.isRunning());

  // Replay without counting time (every stored readout sent exactly once) and without a
  // default port that could mask misrouting.
  ReplayConfig config;
  config.default_port = 19999; // unused: both groups have embedded attributes
  replay(filename, config);

  // Allow time for UDP delivery
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  CHECK(settled_readouts(left_stats)  == static_cast<int>(left_count));
  CHECK(settled_readouts(right_stats) == static_cast<int>(right_count));
  CHECK(left_stats->bad == 0);
  CHECK(right_stats->bad == 0);

  fs::remove(fs::path(filename));
}

TEST_CASE("Explicit SenderConfigs override file-embedded EFU routing", "[replay][routing][efu][precedence]") {
  // Same two-group file as the routing test.  An explicit senders entry for (CAEN,CAEN)
  // pointing at port 9022 should take precedence over the embedded attributes at 9020/9021,
  // causing all 1000 readouts to arrive at 9022 and nothing at 9020 or 9021.
  namespace fs = std::filesystem;
  const uint16_t left_count{300};
  const uint16_t right_count{700};
  const int left_port = find_free_udp_port();
  REQUIRE(left_port > 0);
  int right_port = find_free_udp_port();
  REQUIRE(right_port > 0);
  while (right_port == left_port) {
    right_port = find_free_udp_port();
    REQUIRE(right_port > 0);
  }
  int override_port = find_free_udp_port();
  REQUIRE(override_port > 0);
  while (override_port == left_port || override_port == right_port) {
    override_port = find_free_udp_port();
    REQUIRE(override_port > 0);
  }
  const auto filename = write_two_group_file("replay_prec", left_count, left_port, right_count, right_port);

  auto left_stats     = std::make_shared<UDPStats>();
  auto right_stats    = std::make_shared<UDPStats>();
  auto override_stats = std::make_shared<UDPStats>();

  auto make_receiver = [](int port, std::shared_ptr<UDPStats> stats) {
    return cluon::UDPReceiver("127.0.0.1", port,
      [stats](std::string && data, std::string &&, std::chrono::system_clock::time_point &&) noexcept {
        const auto * header = reinterpret_cast<const PacketHeaderV0*>(data.data());
        stats->packets++;
        if (!ess_header_ok(header->CookieAndType, 0x34)) { stats->bad++; return; }
        stats->readouts += static_cast<int>((header->TotalLength - sizeof(PacketHeaderV0)) / sizeof(struct CaenData));
      });
  };

  cluon::UDPReceiver left_recv     = make_receiver(left_port,     left_stats);
  cluon::UDPReceiver right_recv    = make_receiver(right_port,    right_stats);
  cluon::UDPReceiver override_recv = make_receiver(override_port, override_stats);
  REQUIRE(left_recv.isRunning());
  REQUIRE(right_recv.isRunning());
  REQUIRE(override_recv.isRunning());

  // Build an explicit SenderConfigs that routes BIFROST (0x34) → CAEN readouts to override_port
  const std::string senders_json = R"({"senders":[
    {"detector_type": "BIFROST", "readout_type": "CAEN",
     "ip_address": "127.0.0.1", "udp_port": )" +
    std::to_string(override_port) + R"(, "tcp_port": 0}
  ]})";
  ReplayConfig config;
  config.senders = SenderConfigs::from_json(senders_json);
  config.default_port = 19999; // also unused
  replay(filename, config);

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  const int total = static_cast<int>(left_count + right_count);
  CHECK(settled_readouts(override_stats) == total);
  CHECK(override_stats->bad == 0);
  CHECK(left_stats->readouts  == 0);
  CHECK(right_stats->readouts == 0);

  fs::remove(fs::path(filename));
}
