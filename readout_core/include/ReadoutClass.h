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
#include "enums.h"
#include "hdf_interface.h"
#include "version.hpp"
#include "efu_time.h"
#include "writer.h"

/** \brief Runtime-streaming readout generator used by the legacy Readout components.
 *
 * Buffers ESS readout packets and sends them over UDP to one EFU while the
 * simulation runs. The weighted addReadout() draws n ~ Poisson(weight) and
 * sends the event that many times (weight <= 0 marks a noise event, sent
 * exactly once); it can also mirror every stored record to a legacy flat
 * HDF5 file via dump_to(). The replay path uses Sender instead.
 *
 * Pulse (reference) times are ticks of an epoch-anchored pulse_grid, so every
 * Readout -- one per MPI rank -- and mccode-plumber's mp-tdc agree on them.
 */
class RL_API Readout {
public:
  Readout(
      std::string IpAddress,
        const int UDPPort,
        const int TCPPort,
        const int Type,
        const pulse_grid grid
  ): Type(detectorType_from_int(Type)),
     ipaddr(std::move(IpAddress)),
     port(UDPPort),
     tcp_port(TCPPort),
     grid(grid),
     period(grid.period()),
     sender{ipaddr, static_cast<uint16_t>(UDPPort)}
  {
//    sockOpen(ipaddr, port);
    hp = (PacketHeaderV0*)&buffer[0];
    set_pulse(grid.at_or_before(efu_time::now_nanoseconds()));
    newPacket();
  }

  Readout(
      std::string IpAddress,
        const int UDPPort,
        const int TCPPort,
        const int Type=0x34,
        const efu_time p = efu_time(1)
  ): Readout(std::move(IpAddress), UDPPort, TCPPort, Type, pulse_grid::from_period(p)) {}

  ~Readout() {
    // ensure any buffered data is sent before the object is destroyed
    send();
    report_long_tof();
  }

  /// Add a weighted readout: draws n ~ Poisson(weight) and buffers the event n
  /// times (weight <= 0 is a noise event, sent once); a full buffer is
  /// transmitted and a new packet started automatically.
  void addReadout(uint8_t Ring, uint8_t FEN, double tof, double weight, const void * data);
  /// Add a single readout with an explicit event time.
  void addReadout(uint8_t Ring, uint8_t FEN, efu_time t, const void * data);
  // Specializations for handled data types
  void addReadout(uint8_t Ring, uint8_t FEN, efu_time t, const CAEN_readout_t * data);
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
  void update_time(){
    send();
    // the latest pulse on the grid; the previous one a period before it, as for a
    // continuously pulsed source (the CAEN EFUs reject a gap of more than five)
    set_pulse(grid.at_or_before(efu_time::now_nanoseconds()));
    newPacket();
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

  /// Also store every added readout to a legacy flat HDF5 file (see Writer).
  void dump_to(const std::string & filename, const std::string & dataset_name = "events");

  /// Choose whether add-by-time-of-flight readouts are stamped at
  /// pulse + (tof % period) — attributing each event to the frame it would be
  /// detected in, as the real readout system reports it — instead of pulse + tof.
  /// Either way, a time-of-flight of a period or more is counted and reported.
  void fold_tof(const bool fold) { fold_tof_ = fold; }
  /// How many readouts so far had a time-of-flight of at least one pulse period.
  [[nodiscard]] uint64_t long_tof_count() const { return long_tof_; }

  /// Send on this output queue (0 until set). An EFU keeps one packet sequence per
  /// queue, so several processes sending to one EFU -- MPI ranks -- each need their own.
  void output_queue(const int queue) {
    OutputQueue = queue;
    hp->OutputQueue = static_cast<uint8_t>(queue);
  }
  [[nodiscard]] int output_queue() const { return OutputQueue; }
  /// The number the next packet will be sent with.
  [[nodiscard]] int sequence_number() const { return SeqNum; }

  void enable_network() {network = true;}
  void disable_network() {network = false;}

  void set_random_seed(const uint32_t seed) {
    random_engine.seed(seed);
  }

  int random_poisson(const double mean) {
    std::poisson_distribution<int> distribution(mean);
    return distribution(random_engine);
  }

private:
  HighFive::CompoundType datatype() const {
    return ::hdf_compound_type(readoutType_from_detectorType(Type));
  }

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
  DetectorType Type;

  // TX Buffer
  PacketHeaderV0 *hp{};
  char buffer[9000]{};
  const int MaxDataSize{8950};
  int DataSize{0};
  // IP and port number
  std::string ipaddr;
  int port{9000};
  int tcp_port{8888};
  int verbosity{0};

  std::optional<Writer> writer{std::nullopt};
  bool network{true};
  bool fold_tof_{false};
  uint64_t long_tof_{0};
  void report_long_tof() const;
  pulse_grid grid;
  efu_time period, time;
  cluon::UDPSender sender;

  /// Make ``ns``, a grid tick, the current pulse and the one before it the previous.
  void set_pulse(const uint64_t ns) {
    time = efu_time::from_nanoseconds(ns);
    const auto prev = efu_time::from_nanoseconds(ns - grid.period_ns());
    setPulseTime(time.high(), time.low(), prev.high(), prev.low());
  }

  std::mt19937 random_engine{std::default_random_engine{}()};
};
