#include <catch2/catch_test_macros.hpp>
#include "cluon-complete.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <Readout.h>
#include <Structs.h>

#ifndef _WIN32
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
int free_udp_port() {
  const int sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) return -1;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(0);
  socklen_t len = sizeof(addr);
  const bool ok = bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0
                  && getsockname(sock, reinterpret_cast<sockaddr *>(&addr), &len) == 0;
  close(sock);
  return ok ? ntohs(addr.sin_port) : -1;
}

/// The sequence numbers received, per output queue, in arrival order.
class SequenceLog {
  mutable std::mutex mutex_;
  std::map<int, std::vector<uint32_t>> by_queue_;
public:
  void add(const std::string & data) {
    const auto * header = reinterpret_cast<const PacketHeaderV0 *>(data.data());
    const std::lock_guard lock(mutex_);
    by_queue_[header->OutputQueue].push_back(header->SeqNum);
  }
  std::map<int, std::vector<uint32_t>> by_queue() const {
    const std::lock_guard lock(mutex_);
    return by_queue_;
  }
};

cluon::UDPReceiver receiver_for(const int port, std::shared_ptr<SequenceLog> log) {
  return cluon::UDPReceiver("127.0.0.1", port,
    [log](std::string && data, std::string &&, std::chrono::system_clock::time_point &&) noexcept {
      log->add(data);
    });
}

bool consecutive(const std::vector<uint32_t> & seq) {
  for (size_t i = 1; i < seq.size(); ++i) if (seq[i] != seq[i - 1] + 1) return false;
  return true;
}
}

TEST_CASE("readout_add sends consecutive sequence numbers", "[c][sequence]") {
  const int port = free_udp_port();
  REQUIRE(port > 0);
  auto log = std::make_shared<SequenceLog>();
  auto receiver = receiver_for(port, log);
  REQUIRE(receiver.isRunning());

  // What ReadoutCAEN does: every readout_add advances the reference time, which sends the
  // packet so far and starts another -- and used to spend two numbers doing it.
  auto * r = readout_create("127.0.0.1", port, 0, 14.0, 0x34);
  readout_newPacket(r);
  CAEN_readout_t data{};
  for (int i = 0; i < 20; ++i) readout_add(r, 0, 0, 0.01, 0.0, &data);
  readout_send(r);
  readout_destroy(r);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  const auto received = log->by_queue();
  REQUIRE(received.count(0) == 1);
  const auto & seq = received.at(0);
  CHECK(seq.size() >= 20);
  CHECK(seq.front() == 0);
  CHECK(consecutive(seq));
}

TEST_CASE("Readouts on different output queues keep separate sequences", "[c][sequence]") {
  const int port = free_udp_port();
  REQUIRE(port > 0);
  auto log = std::make_shared<SequenceLog>();
  auto receiver = receiver_for(port, log);
  REQUIRE(receiver.isRunning());

  // Two MPI ranks sending to one EFU at once
  auto * a = readout_create("127.0.0.1", port, 0, 14.0, 0x34);
  auto * b = readout_create("127.0.0.1", port, 0, 14.0, 0x34);
  CHECK(readout_output_queue(a, 0) == 0);
  CHECK(readout_output_queue(b, 1) == 1);
  readout_newPacket(a);
  readout_newPacket(b);
  CAEN_readout_t data{};
  for (int i = 0; i < 10; ++i) {
    readout_add(a, 0, 0, 0.01, 0.0, &data);
    readout_add(b, 0, 0, 0.01, 0.0, &data);
  }
  readout_destroy(a);
  readout_destroy(b);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  const auto received = log->by_queue();
  REQUIRE(received.size() == 2);
  CHECK(consecutive(received.at(0)));
  CHECK(consecutive(received.at(1)));
}

TEST_CASE("An output queue the EFU does not have is refused", "[c][sequence]") {
  auto * r = readout_create("127.0.0.1", 47999, 0, 14.0, 0x34);
  CHECK(readout_output_queue(r, READOUT_OUTPUT_QUEUES) == -1);
  CHECK(readout_output_queue(r, -1) == -1);
  CHECK(readout_output_queue(r, READOUT_OUTPUT_QUEUES - 1) == READOUT_OUTPUT_QUEUES - 1);
  readout_disable_network(r);
  readout_destroy(r);
}
#endif
