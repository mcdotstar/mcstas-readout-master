// Copyright (C) 2022 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief UDP readout generator class
///
//===----------------------------------------------------------------------===//
#pragma once

#include "cluon-complete.hpp"

#include <string>
#include <utility>
#include <optional>
#include <random>

#include "Structs.h"
#include "Readout.h"
#include "SenderConfigs.h"
#include "enums.h"
#include "hdf_interface.h"
#include "version.hpp"
#include "efu_time.h"

/** \brief Buffers ESS readout records and transmits them as UDP packets to one EFU.
 *
 * Records are packed into ESS readout packets (packet-type byte from the
 * DetectorType, record layout from the ReadoutType) and sent to the
 * configured address when the buffer fills, when the reference time
 * advances (update_time()), or on destruction.
 */
class Sender {
public:
  Sender(
      std::string IpAddress, const int UDPPort, const int TCPPort, const DetectorType detector_type, const ReadoutType readout_type, efu_time p = efu_time(1), efu_time t = efu_time()
  ): detector_type(detector_type),
     readout_type(readout_type),
     ipaddr(std::move(IpAddress)),
     port(UDPPort),
     tcp_port(TCPPort),
     period(p),
     time(t),
     sender{ipaddr, static_cast<uint16_t>(UDPPort)}
  {
    // hp = reinterpret_cast<PacketHeaderV0 *>(&buffer[0]);
    auto prev = time - period;
    setPulseTime(time.high(), time.low(), prev.high(), prev.low());
    newPacket();
  }

  explicit Sender(const SenderConfig & config, efu_time p = efu_time(1), efu_time t = efu_time())
      : Sender(config.ip_address, config.udp_port, config.tcp_port, config.detector_type, config.readout_type, p, t) {}

  ~Sender() {
    // ensure any buffered data is sent before the object is destroyed
    send();
  }

  /// Add one readout with its event time set to the current pulse time plus tof;
  /// the weight is ignored (statistical sampling happens upstream). A full
  /// buffer is transmitted and a new packet started automatically.
  void addReadout(uint8_t Ring, uint8_t FEN, double tof, double weight, const void * data);
  /// Add a single readout with an explicit event time.
  void addReadout(uint8_t Ring, uint8_t FEN, efu_time t, const void * data);
  // Specializations for handled data types
  void addReadout(uint8_t Ring, uint8_t FEN, efu_time t, const CAEN_readout_t * data);
  void addReadout(uint8_t Ring, uint8_t FEN, efu_time t, const TTLMonitor_readout_t * data);
  void addReadout(uint8_t Ring, uint8_t FEN, efu_time t, const CDT_readout_t * data);
  void addReadout(uint8_t Ring, uint8_t FEN, efu_time t, const VMM3_readout_t * data);
  void addReadout(uint8_t Ring, uint8_t FEN, efu_time t, const BM0_readout_t * data);
  void addReadout(uint8_t Ring, uint8_t FEN, efu_time t, const BM2_readout_t * data);
  void addReadout(uint8_t Ring, uint8_t FEN, efu_time t, const BMI_readout_t * data);


  /// Transmit the current data buffer.
  int send();

  /// Update the pulse and previous pulse times (high/low pairs).
  void setPulseTime(uint32_t PHI, uint32_t PLO, uint32_t PPHI, uint32_t PPLO);

  /// Advance the reference (pulse) time to now, flushing the buffer and
  /// starting a new packet; keeps (now - prev) within the 5-period window
  /// required by the ESS CAEN EFUs.
  void update_time() {
    auto time_lock = std::lock_guard(time_mutex);
    auto now = efu_time();
    if ((now - time) >= &period){
      now = time + period * ((now - time) / period);
    }
    // The ESS Caen EFUs require (now - prev) <= 5 * rep; so we should fake it
    if ((now - time) > (period * 5u)) {
      time = now - period;
    }
    send();
    setPulseTime(now.high(), now.low(), time.high(), time.low());
    newPacket();
    time = now;
  }

  // Query the current pulse and previous pulse times
  [[nodiscard]] std::pair<uint32_t, uint32_t> lastPulseTime() const;
  [[nodiscard]] std::pair<uint32_t, uint32_t> prevPulseTime() const;
  [[nodiscard]] std::pair<uint32_t, uint32_t> lastEventTime() const;

  /// Initialize a new packet with no readouts.
  void newPacket();

  /// Tell the remote EFU to shut down via its TCP command port.
  int command_shutdown() const;

  /// Set verbosity via enum.
  int verbose(const Verbosity v){
    switch (v) {
      case Verbosity::details: verbosity=3; break;
      case Verbosity::info: verbosity=2; break;
      case Verbosity::warnings: verbosity=1; break;
      case Verbosity::errors: verbosity=0; break;
      case Verbosity::silent: verbosity=-1; break;
      default: verbosity=0;
    }
    return verbosity;
  }
  int verbose(const int v){verbosity = v; return verbosity;}
private:

  void check_size_and_send();

  // Packet header
  uint32_t phi{0}; // pulse and prev pulse high and low
  uint32_t plo{0};
  uint32_t pphi{0};
  uint32_t pplo{0};

  uint32_t lasthi{0};
  uint32_t lastlo{0};

  int SeqNum{0};
  int OutputQueue{0};
  DetectorType detector_type;
  ReadoutType readout_type;

  // TX Buffer
  // PacketHeaderV0 *hp{};
  char buffer[9000]{};
  const int MaxDataSize{8950};
  int DataSize{0};
  // IP and port number
  std::string ipaddr;
  int port{9000};
  int tcp_port{8888};
  int verbosity{0};

  efu_time period, time;
  cluon::UDPSender sender;

  std::mutex time_mutex, send_mutex;
};
