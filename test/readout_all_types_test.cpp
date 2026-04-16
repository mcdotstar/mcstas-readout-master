#include <catch2/catch_test_macros.hpp>
#include "cluon-complete.hpp"
#include <Readout.h>
#include <Structs.h>
#include "test_utils.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
static int find_free_port() {
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return -1;
  SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock == INVALID_SOCKET) { WSACleanup(); return -1; }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(0);
  if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) { closesocket(sock); WSACleanup(); return -1; }
  int len = sizeof(addr);
  if (getsockname(sock, (struct sockaddr*)&addr, &len) == SOCKET_ERROR) { closesocket(sock); WSACleanup(); return -1; }
  int port = ntohs(addr.sin_port);
  closesocket(sock);
  WSACleanup();
  return port;
}
#else
static int find_free_port() {
  int sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) return -1;
  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(0);
  if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(sock); return -1; }
  socklen_t len = sizeof(addr);
  if (getsockname(sock, (struct sockaddr*)&addr, &len) == -1) { close(sock); return -1; }
  int port = ntohs(addr.sin_port);
  close(sock);
  return port;
}
#endif

// Helper: verify ESS packet header
static void verify_header(const std::string& data, uint32_t expected_type) {
  auto ptr = data.data();
  auto* header = reinterpret_cast<const PacketHeaderV0*>(ptr);
  REQUIRE(header->Padding0 == 0);
  REQUIRE(header->Version == 0);
  auto type = (header->CookieAndType) >> 24;
  auto cookie = (header->CookieAndType - (type << 24));
  REQUIRE(cookie == 0x535345);
  REQUIRE(type == expected_type);
}

TEST_CASE("Send and receive CDT packets", "[c][CDT]") {
  const uint16_t max = 100;
  uint32_t det_type = 0x60; // DREAM -> CDT
  int port = find_free_port();
  REQUIRE(port > 0);
  auto stats = std::make_shared<UDPStats>();

  cluon::UDPReceiver receiver("127.0.0.1", port,
    [stats, det_type](std::string&& data, std::string&&, std::chrono::system_clock::time_point&&) noexcept {
      verify_header(data, det_type);
      auto ptr = data.data() + sizeof(PacketHeaderV0);
      auto readouts = (reinterpret_cast<const PacketHeaderV0*>(data.data())->TotalLength - sizeof(PacketHeaderV0)) / sizeof(CDTData);
      for (size_t i = 0; i < readouts; ++i) {
        auto* r = reinterpret_cast<const CDTData*>(ptr + i * sizeof(CDTData));
        REQUIRE(r->Ring == 1);
        REQUIRE(r->FEN == 2);
      }
      stats->packets++;
      stats->readouts += static_cast<int>(readouts);
    });
  REQUIRE(receiver.isRunning());

  {
    char addr[] = "127.0.0.1";
    auto efu = readout_create(addr, port, 8888, 1.0 / 14.0, static_cast<int>(det_type));
    CDT_readout_t cdt{3, 10, 20};
    for (uint16_t i = 0; i < max; ++i) {
      double tof = static_cast<double>(i) / static_cast<double>(max);
      readout_add(efu, 1, 2, tof, 0.0, &cdt);
    }
    readout_destroy(efu);
  }
  if (stats->readouts < max) std::this_thread::sleep_for(std::chrono::milliseconds(200));
  CHECK(stats->readouts == max);
}

TEST_CASE("Send and receive VMM3 packets", "[c][VMM3]") {
  const uint16_t max = 100;
  uint32_t det_type = 0x40; // TREX -> VMM3
  int port = find_free_port();
  REQUIRE(port > 0);
  auto stats = std::make_shared<UDPStats>();

  cluon::UDPReceiver receiver("127.0.0.1", port,
    [stats, det_type](std::string&& data, std::string&&, std::chrono::system_clock::time_point&&) noexcept {
      verify_header(data, det_type);
      auto readouts = (reinterpret_cast<const PacketHeaderV0*>(data.data())->TotalLength - sizeof(PacketHeaderV0)) / sizeof(VMM3Data);
      stats->packets++;
      stats->readouts += static_cast<int>(readouts);
    });
  REQUIRE(receiver.isRunning());

  {
    char addr[] = "127.0.0.1";
    auto efu = readout_create(addr, port, 8888, 1.0 / 14.0, static_cast<int>(det_type));
    VMM3_readout_t vmm3{42, 100, 3, 7, 1, 12};
    for (uint16_t i = 0; i < max; ++i) {
      double tof = static_cast<double>(i) / static_cast<double>(max);
      readout_add(efu, 0, 1, tof, 0.0, &vmm3);
    }
    readout_destroy(efu);
  }
  if (stats->readouts < max) std::this_thread::sleep_for(std::chrono::milliseconds(200));
  CHECK(stats->readouts == max);
}

TEST_CASE("Send and receive BM0 packets", "[c][BM0]") {
  const uint16_t max = 100;
  uint32_t det_type = 0xf0; // CBM0 -> BM0
  int port = find_free_port();
  REQUIRE(port > 0);
  auto stats = std::make_shared<UDPStats>();

  cluon::UDPReceiver receiver("127.0.0.1", port,
    [stats, det_type](std::string&& data, std::string&&, std::chrono::system_clock::time_point&&) noexcept {
      verify_header(data, det_type);
      auto readouts = (reinterpret_cast<const PacketHeaderV0*>(data.data())->TotalLength - sizeof(PacketHeaderV0)) / sizeof(BM0Data);
      stats->packets++;
      stats->readouts += static_cast<int>(readouts);
    });
  REQUIRE(receiver.isRunning());

  {
    char addr[] = "127.0.0.1";
    auto efu = readout_create(addr, port, 8888, 1.0 / 14.0, static_cast<int>(det_type));
    BM0_readout_t bm0{5};
    for (uint16_t i = 0; i < max; ++i) {
      double tof = static_cast<double>(i) / static_cast<double>(max);
      readout_add(efu, 0, 0, tof, 0.0, &bm0);
    }
    readout_destroy(efu);
  }
  if (stats->readouts < max) std::this_thread::sleep_for(std::chrono::milliseconds(200));
  CHECK(stats->readouts == max);
}

TEST_CASE("Send and receive BM2 packets", "[c][BM2]") {
  const uint16_t max = 100;
  uint32_t det_type = 0x50; // BEER -> BM2
  int port = find_free_port();
  REQUIRE(port > 0);
  auto stats = std::make_shared<UDPStats>();

  cluon::UDPReceiver receiver("127.0.0.1", port,
    [stats, det_type](std::string&& data, std::string&&, std::chrono::system_clock::time_point&&) noexcept {
      verify_header(data, det_type);
      auto readouts = (reinterpret_cast<const PacketHeaderV0*>(data.data())->TotalLength - sizeof(PacketHeaderV0)) / sizeof(BM2Data);
      stats->packets++;
      stats->readouts += static_cast<int>(readouts);
    });
  REQUIRE(receiver.isRunning());

  {
    char addr[] = "127.0.0.1";
    auto efu = readout_create(addr, port, 8888, 1.0 / 14.0, static_cast<int>(det_type));
    BM2_readout_t bm2{3, 150, 250};
    for (uint16_t i = 0; i < max; ++i) {
      double tof = static_cast<double>(i) / static_cast<double>(max);
      readout_add(efu, 1, 1, tof, 0.0, &bm2);
    }
    readout_destroy(efu);
  }
  if (stats->readouts < max) std::this_thread::sleep_for(std::chrono::milliseconds(200));
  CHECK(stats->readouts == max);
}

TEST_CASE("Send and receive BMI packets", "[c][BMI]") {
  const uint16_t max = 100;
  uint32_t det_type = 0xfa; // CBMI -> BMI
  int port = find_free_port();
  REQUIRE(port > 0);
  auto stats = std::make_shared<UDPStats>();

  cluon::UDPReceiver receiver("127.0.0.1", port,
    [stats, det_type](std::string&& data, std::string&&, std::chrono::system_clock::time_point&&) noexcept {
      verify_header(data, det_type);
      auto readouts = (reinterpret_cast<const PacketHeaderV0*>(data.data())->TotalLength - sizeof(PacketHeaderV0)) / sizeof(BMIData);
      stats->packets++;
      stats->readouts += static_cast<int>(readouts);
    });
  REQUIRE(receiver.isRunning());

  {
    char addr[] = "127.0.0.1";
    auto efu = readout_create(addr, port, 8888, 1.0 / 14.0, static_cast<int>(det_type));
    BMI_readout_t bmi{2, 7, 0x00ABCD};
    for (uint16_t i = 0; i < max; ++i) {
      double tof = static_cast<double>(i) / static_cast<double>(max);
      readout_add(efu, 0, 0, tof, 0.0, &bmi);
    }
    readout_destroy(efu);
  }
  if (stats->readouts < max) std::this_thread::sleep_for(std::chrono::milliseconds(200));
  CHECK(stats->readouts == max);
}
