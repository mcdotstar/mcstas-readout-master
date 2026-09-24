#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>
#include "cluon-complete.hpp"

#include <Readout.h>
#include <Structs.h>
#include <enums.h>
#include "test_utils.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
int find_port() {
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    std::cerr << "WSAStartup failed" << std::endl;
    return -1;
  }

  int port = 0;
  SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock == INVALID_SOCKET) {
    std::cerr << "Socket creation failed: " << WSAGetLastError() << std::endl;
    WSACleanup();
    return -1;
  }

  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);

  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
    std::cerr << "Bind failed: " << WSAGetLastError() << std::endl;
    closesocket(sock);
    WSACleanup();
    return -1;
  }

  int len = sizeof(addr);
  if (getsockname(sock, (struct sockaddr *)&addr, &len) == SOCKET_ERROR) {
    std::cerr << "getsockname failed: " << WSAGetLastError() << std::endl;
    closesocket(sock);
    WSACleanup();
    return -1;
  }

  port = ntohs(addr.sin_port);
  closesocket(sock);
  WSACleanup();
  return port;
}
#else
int find_port() {
  int port = 0;
  int sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    perror("socket");
    return -1;
  }
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);
  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    close(sock);
    return -1;
  }
  socklen_t len = sizeof(addr);
  if (getsockname(sock, (struct sockaddr *)&addr, &len) == -1) {
    perror("getsockname");
    close(sock);
    return -1;
  }
  port = ntohs(addr.sin_port);
  close(sock);
  return port;
}
#endif

TEST_CASE("Send and receive CAEN packets","[c][CAEN]"){
  const uint16_t max{1000};
  uint32_t detector_type{0x34};

  int detector_port = find_port();
  auto stats = std::make_shared<UDPStats>();

  cluon::UDPReceiver detector_receiver("127.0.0.1", detector_port,
      [stats,detector_type](std::string && data, std::string &&, std::chrono::system_clock::time_point &&) noexcept {;
        // data must contain [PacketHeaderV0, readout, readout, ...].
        auto ptr = data.data();
        auto * header = reinterpret_cast<PacketHeaderV0*>(ptr);
        REQUIRE(header->Padding0 == 0);
        REQUIRE(header->Version == 0);
        auto type = (header->CookieAndType) >> 24;
        auto cookie =  (header->CookieAndType - (type << 24));
        REQUIRE(cookie == 0x535345);  // ESS identifier
        REQUIRE(detector_type == type);
        ptr += sizeof(PacketHeaderV0);
        size_t readout_size =sizeof(struct CaenData);
        auto readouts = (header->TotalLength - sizeof(PacketHeaderV0)) / readout_size;
//        auto stats = Singleton::instance();
        for (size_t i=0; i<readouts; ++i){
          auto *r = reinterpret_cast<CaenData *>(ptr + i * readout_size);
          REQUIRE(r->Ring == 1);
          REQUIRE(r->FEN == 0);
          REQUIRE(r->Tube == 3);
          REQUIRE(r->AmplA == stats->readouts + i);
          REQUIRE(r->AmplB == max - i - stats->readouts);
          REQUIRE(r->AmplC == 0);
          REQUIRE(r->AmplD == 0);
        }
        stats->packets++;
        stats->readouts += readouts;
      });
  REQUIRE(detector_receiver.isRunning());

  char addr[] = "127.0.0.1";
  {
    auto detector_efu = readout_create(addr, detector_port, 8888, 1 / 14., static_cast<int>(detector_type));
    CAEN_readout_t caen_data;
    for (uint16_t i = 0; i < max; ++i) {
      uint8_t ring = 1;
      uint8_t fen = 0;
      uint8_t tube = 3;
      double tof = static_cast<double>(i) / static_cast<double>(max);
      caen_data.channel = tube;
      caen_data.a = i;
      caen_data.b = max - i;
      caen_data.c = 0;
      caen_data.d = 0;
      // Setting the weight to 0, otherwise it is used to send a random number of packets
      readout_add(detector_efu, ring, fen, tof, 0., static_cast<const void *>(&caen_data));
    }
    readout_destroy(detector_efu);
  }
  auto expected = max;
  if (stats->readouts < expected){
    // wait a bit in case the receiver is doing something?
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  REQUIRE(stats->readouts == expected);
}


TEST_CASE("Send and receive beam-monitor packets as the cbm EFU expects them","[c][CBM]"){
  const uint16_t max{1000};
  int monitor_port = find_port();
  auto stats = std::make_shared<UDPStats>();

  cluon::UDPReceiver monitor_receiver("127.0.0.1", monitor_port,
    [stats](std::string && data, std::string &&, std::chrono::system_clock::time_point &&) noexcept {
      // data must contain [PacketHeaderV0, readout, readout, ...].
      auto ptr = data.data();
      auto * header = reinterpret_cast<PacketHeaderV0*>(ptr);
      REQUIRE(header->Padding0 == 0);
      REQUIRE(header->Version == 0);
      auto type = (header->CookieAndType) >> 24;
      auto cookie =  (header->CookieAndType - (type << 24));
      REQUIRE(cookie == 0x535345);  // ESS identifier
      // every beam-monitor format travels as the EFU's DetectorType::CBM
      REQUIRE(type == CBM_PACKET_TYPE);
      ptr += sizeof(PacketHeaderV0);
      size_t readout_size = sizeof(struct BM0Data);
      auto readouts = (header->TotalLength - sizeof(PacketHeaderV0)) / readout_size;
      for (size_t i=0; i < readouts; ++i){
        auto *r = reinterpret_cast<BM0Data *>(ptr + i * readout_size);
        REQUIRE(r->Ring == 22);  // the fibre of MonitorRing 11
        REQUIRE(r->FEN == 0);
        REQUIRE(r->Type == 1);   // CbmType::EVENT_0D
        REQUIRE((r->Channel == 1 || r->Channel == 0));
      }
      stats->packets++;
      stats->readouts += readouts;
    });
  REQUIRE(monitor_receiver.isRunning());

  {
    char addr[] = "127.0.0.1";
    auto monitor_efu = readout_create(addr, monitor_port, 8889, 1 / 14., static_cast<int>(CBM0));
    BM0_readout_t bm0_data;
    for (uint16_t i = 0; i < max; ++i) {
      double tof = static_cast<double>(i) / static_cast<double>(max) / 14.;
      // Setting the weight to 0, otherwise it is used to send a random number of packets
      bm0_data.channel = 0;
      readout_add(monitor_efu, 22, 0, tof, 0.0, static_cast<const void *>(&bm0_data));
      bm0_data.channel = 1;
      readout_add(monitor_efu, 22, 0, tof, 0.0, static_cast<const void *>(&bm0_data));
    }
    readout_destroy(monitor_efu);
  }
  // the lambda function defined above gets called for each packet
  // and all 2 * max produced events are received
  auto expected = 2 * max;
  if (stats->readouts < expected){
    // wait a bit in case the receiver is doing something?
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  REQUIRE(stats->readouts == expected);
}