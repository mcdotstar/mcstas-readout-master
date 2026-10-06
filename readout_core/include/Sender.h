// Copyright (C) 2022 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief UDP readout generator class
///
//===----------------------------------------------------------------------===//
#pragma once

#include "cluon-complete.hpp"

#include <atomic>
#include <cstdint>
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
 * configured address when the buffer fills, at pulse boundaries
 * (begin_pulse()), or on destruction.
 *
 * The pulse (reference) times in the packet headers march forward on a fixed
 * grid with the configured period, anchored on the UNIX epoch (pulse_grid), so
 * every Sender -- and mccode-plumber's mp-tdc -- agrees on when a pulse was: whenever
 * a full buffer forces a new packet, the pulse time advances to the latest
 * grid tick at or before the wall clock, so consecutive packets may share a
 * pulse time until the reference clock ticks. begin_pulse() waits for the next
 * grid tick to pass, guaranteeing the new pulse time is a wall-clock instant
 * later than everything that happened before the call. The previous pulse time
 * is always exactly one period earlier, as for a continuously pulsed source.
 */
class RL_API Sender {
public:
  Sender(
      std::string IpAddress, const int UDPPort, const int TCPPort, const DetectorType detector_type, const ReadoutType readout_type, const pulse_grid grid
  ): detector_type(detector_type),
     readout_type(readout_type),
     ipaddr(std::move(IpAddress)),
     port(UDPPort),
     tcp_port(TCPPort),
     grid(grid),
     period(grid.period()),
     sender{ipaddr, static_cast<uint16_t>(UDPPort)}
  {
    // the latest pulse already past, as for a source that has been running all along
    set_pulse(grid.at_or_before(efu_time::now_nanoseconds()));
    newPacket();
  }

  Sender(
      std::string IpAddress, const int UDPPort, const int TCPPort, const DetectorType detector_type, const ReadoutType readout_type, const efu_time p = efu_time(1)
  ): Sender(std::move(IpAddress), UDPPort, TCPPort, detector_type, readout_type, pulse_grid::from_period(p)) {}

  explicit Sender(const SenderConfig & config, const pulse_grid grid)
      : Sender(config.ip_address, config.udp_port, config.tcp_port, config.detector_type, config.readout_type, grid) {}

  explicit Sender(const SenderConfig & config, const efu_time p = efu_time(1))
      : Sender(config, pulse_grid::from_period(p)) {}

  ~Sender() {
    // ensure any buffered readouts are sent before the object is destroyed
    if (DataSize > static_cast<int>(sizeof(struct PacketHeaderV0))) {
      send();
    }
    report_long_tof();
  }

  /// Add one readout with its event time set to the current pulse time plus tof;
  /// the weight is ignored (statistical sampling happens upstream). A full
  /// buffer is transmitted and a new packet started automatically.
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


  /// Transmit the current data buffer; the caller is responsible for starting
  /// a new packet (newPacket()) before adding further readouts.
  int send();

  /// Update the pulse and previous pulse times (high/low pairs).
  void setPulseTime(uint32_t PHI, uint32_t PLO, uint32_t PPHI, uint32_t PPLO);

  /// Start a new pulse: flush any buffered readouts, wait until the next
  /// pulse-grid tick has passed on the wall clock, and start a new packet with
  /// that tick as its pulse time. Everything sent afterwards carries reference
  /// times strictly later than any wall-clock instant preceding this call.
  void begin_pulse();
  /// begin_pulse(), at the grid tick ``at`` (rounded up onto the grid), so that
  /// several Senders can begin one and the same pulse.
  void begin_pulse_at(uint64_t at);
  /// The tick begin_pulse() would begin: the first after now and after the current pulse.
  [[nodiscard]] uint64_t next_pulse() const;
  /// Send the current pulse's header with no readouts, so the EFU sees the pulse even
  /// when nothing is detected in it. An EFU summing a number of pulses into one
  /// histogram counts the pulses it is shown, so a silent one would shift every later
  /// histogram's boundaries.
  void announce_pulse();
  /// Send whatever is buffered for the current pulse now.
  void flush();
  /// Keep the pulse begun by begin_pulse()/begin_pulse_at() however long sending takes,
  /// rather than moving on with the wall clock when a full buffer is sent. For a caller
  /// that places every event in a pulse of its choosing.
  void hold_pulse(const bool hold) { hold_pulse_ = hold; }

  /// Choose whether add-by-time-of-flight readouts are stamped at
  /// pulse + (tof % period) — attributing each event to the frame it would be
  /// detected in, as the real readout system reports it — instead of pulse + tof.
  /// Either way, a time-of-flight of a period or more is counted and reported.
  void fold_tof(const bool fold) { fold_tof_ = fold; }
  /// How many readouts so far had a time-of-flight of at least one pulse period.
  [[nodiscard]] uint64_t long_tof_count() const { return long_tof_; }

  // Query the current pulse and previous pulse times
  [[nodiscard]] std::pair<uint32_t, uint32_t> lastPulseTime() const;
  /// The current pulse exactly, in nanoseconds since the UNIX epoch: a pulse_grid tick
  [[nodiscard]] uint64_t lastPulseNanoseconds() const { return pulse_ns; }
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

  /// Advance the pulse time to the latest grid tick at or before the wall
  /// clock, when at least one period has elapsed since the current pulse.
  void maybe_advance_pulse();

  /// Make ``ns``, a grid tick, the current pulse and the one before it the previous.
  void set_pulse(uint64_t ns);

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
  bool fold_tof_{false};
  bool hold_pulse_{false};
  std::atomic<uint64_t> long_tof_{0};
  void report_long_tof() const;

  pulse_grid grid;
  efu_time period, time;
  uint64_t pulse_ns{0};
  cluon::UDPSender sender;

  std::mutex time_mutex, send_mutex;
};
