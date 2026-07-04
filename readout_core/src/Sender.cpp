// Copyright (C) 2022 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief ESS UDP readout generator class implementation
///
//===----------------------------------------------------------------------===//
#include "Sender.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>

void Sender::setPulseTime(const uint32_t PHI, const uint32_t PLO, const uint32_t PPHI, const uint32_t PPLO) {
  phi = PHI;
  plo = PLO;
  pphi = PPHI;
  pplo = PPLO;
}

std::pair<uint32_t, uint32_t> Sender::lastPulseTime() const {
  return std::make_pair(phi, plo);
}
std::pair<uint32_t, uint32_t> Sender::prevPulseTime() const {
  return std::make_pair(pphi, pplo);
}
std::pair<uint32_t, uint32_t> Sender::lastEventTime() const {
  return std::make_pair(lasthi, lastlo);
}

void Sender::newPacket() {
  auto lock = std::lock_guard(send_mutex);
  memset(buffer, 0x00, sizeof(buffer));
  const auto hp = reinterpret_cast<PacketHeaderV0 *>(&buffer[0]);
  hp->Padding0 = 0;
  hp->Version = 0;
  hp->CookieAndType = (static_cast<int>(detector_type) << 24) + 0x535345;
  hp->OutputQueue = OutputQueue;
  hp->TotalLength = sizeof(struct PacketHeaderV0);
  // the sequence number is assigned at transmission, so that only packets
  // actually sent consume a number and the EFU sees a contiguous sequence
  hp->SeqNum = SeqNum;
  hp->TimeSource = 0;
  hp->PulseHigh = phi;
  hp->PulseLow = plo;
  hp->PrevPulseHigh = pphi;
  hp->PrevPulseLow = pplo;
  DataSize = sizeof(struct PacketHeaderV0);
}

void Sender::check_size_and_send() {
  if (DataSize >= MaxDataSize) {
    send();
    maybe_advance_pulse();
    newPacket();
  }
}

void Sender::maybe_advance_pulse() {
  auto time_lock = std::lock_guard(time_mutex);
  const auto now = efu_time();
  if (now > time && (now - time) >= period) {
    time = time + period * ((now - time) / period);
    const auto prev = time - period;
    setPulseTime(time.high(), time.low(), prev.high(), prev.low());
  }
}

void Sender::begin_pulse() {
  auto time_lock = std::lock_guard(time_mutex);
  if (DataSize > static_cast<int>(sizeof(struct PacketHeaderV0))) {
    send();
  }
  auto now = efu_time();
  // the next grid tick strictly after now; the grid is anchored at the first pulse time
  const auto tick = (now > time) ? time + period * ((now - time) / period + 1u) : time + period;
  while (now < tick) {
    std::this_thread::sleep_for(std::chrono::nanoseconds((tick - now).total_nanoseconds()));
    now = efu_time();
  }
  time = tick;
  const auto prev = time - period;
  setPulseTime(time.high(), time.low(), prev.high(), prev.low());
  newPacket();
}

void Sender::addReadout(const uint8_t Ring, const uint8_t FEN, const efu_time t, const CAEN_readout_t *data) {
  check_size_and_send();
  if (verbosity > 2){
    std::cout << "Add to the packet Ring=" << static_cast<unsigned>(Ring) << " FEN=" << static_cast<unsigned>(FEN);
    std::cout << " TimeHigh=" << t.high() << " TimeLow=" << t.low() << " Tube=" << static_cast<unsigned>(data->channel);
    std::cout << " AmplA=" << data->a << " AmplB=" << data->b << std::endl;
  }
  auto lock = std::lock_guard(send_mutex);
  auto *dp = reinterpret_cast<struct CaenData *>(buffer + DataSize);
  dp->Ring = Ring;
  dp->FEN = FEN;
  dp->Length = sizeof(struct CaenData);
  dp->TimeHigh = t.high();
  dp->TimeLow = t.low();
  dp->Tube = data->channel;
  dp->AmplA = data->a;
  dp->AmplB = data->b;
  dp->AmplC = data->c;
  dp->AmplD = data->d;
  DataSize += dp->Length;
  const auto hp = reinterpret_cast<PacketHeaderV0 *>(&buffer[0]);
  hp->TotalLength = DataSize;
}

void Sender::addReadout(const uint8_t Ring, const uint8_t FEN, const efu_time t, const TTLMonitor_readout_t *data) {
  if (verbosity > 2){
    std::cout << "Add to the packet Ring=" << static_cast<unsigned>(Ring) << " FEN=" << static_cast<unsigned>(FEN);
    std::cout << " TimeHigh=" << t.high() << " TimeLow=" << t.low() << " Pos=" << static_cast<unsigned>(data->pos);
    std::cout << " Channel=" << static_cast<unsigned>(data->channel) << " ADC=" << data->adc << std::endl;
  }
  check_size_and_send();
  auto lock = std::lock_guard(send_mutex);
  auto *dp = reinterpret_cast<struct TTLMonitorData *>(buffer + DataSize);
  dp->Ring = Ring;
  dp->FEN = FEN;
  dp->Length = sizeof(struct TTLMonitorData);
  dp->TimeHigh = t.high();
  dp->TimeLow = t.low();
  dp->Pos = data->pos;
  dp->Channel = data->channel;
  dp->ADC = data->adc;
  DataSize += dp->Length;
  const auto hp = reinterpret_cast<PacketHeaderV0 *>(&buffer[0]);
  hp->TotalLength = DataSize;
}

void Sender::addReadout(const uint8_t Ring, const uint8_t FEN, const efu_time t, const CDT_readout_t *data) {
  check_size_and_send();
  auto lock = std::lock_guard(send_mutex);
  auto *dp = reinterpret_cast<struct CDTData *>(buffer + DataSize);
  dp->Ring = Ring;
  dp->FEN = FEN;
  dp->Length = sizeof(struct CDTData);
  dp->TimeHigh = t.high();
  dp->TimeLow = t.low();
  dp->OM = data->om;
  dp->Cathode = data->cathode;
  dp->Anode = data->anode;
  DataSize += dp->Length;
  const auto hp = reinterpret_cast<PacketHeaderV0 *>(&buffer[0]);
  hp->TotalLength = DataSize;
}

void Sender::addReadout(const uint8_t Ring, const uint8_t FEN, const efu_time t, const VMM3_readout_t *data) {
  check_size_and_send();
  auto lock = std::lock_guard(send_mutex);
  auto *dp = reinterpret_cast<struct VMM3Data *>(buffer + DataSize);
  dp->Ring = Ring;
  dp->FEN = FEN;
  dp->Length = sizeof(struct VMM3Data);
  dp->TimeHigh = t.high();
  dp->TimeLow = t.low();
  dp->BC = data->bc;
  dp->OTADC = data->otadc;
  dp->GEO = data->geo;
  dp->TDC = data->tdc;
  dp->VMM = data->vmm;
  dp->Channel = data->channel;
  DataSize += dp->Length;
  const auto hp = reinterpret_cast<PacketHeaderV0 *>(&buffer[0]);
  hp->TotalLength = DataSize;
}


void Sender::addReadout(const uint8_t Ring, const uint8_t FEN, const efu_time t, const BM0_readout_t *data) {
  check_size_and_send();
  auto *dp = reinterpret_cast<struct BM0Data *>(buffer + DataSize);
  dp->Ring = Ring;
  dp->FEN = FEN;
  dp->Length = sizeof(struct BM0Data);
  dp->TimeHigh = t.high();
  dp->TimeLow = t.low();
  dp->Channel = data->channel;
  DataSize += dp->Length;
  const auto hp = reinterpret_cast<PacketHeaderV0 *>(&buffer[0]);
  hp->TotalLength = DataSize;
}

void Sender::addReadout(const uint8_t Ring, const uint8_t FEN, const efu_time t, const BM2_readout_t *data) {
  check_size_and_send();
  auto *dp = reinterpret_cast<struct BM2Data *>(buffer + DataSize);
  dp->Ring = Ring;
  dp->FEN = FEN;
  dp->Length = sizeof(struct BM2Data);
  dp->TimeHigh = t.high();
  dp->TimeLow = t.low();
  dp->Channel = data->channel;
  dp->X = data->pos_x;
  dp->Y = data->pos_y;
  DataSize += dp->Length;
  const auto hp = reinterpret_cast<PacketHeaderV0 *>(&buffer[0]);
  hp->TotalLength = DataSize;
}

void Sender::addReadout(const uint8_t Ring, const uint8_t FEN, const efu_time t, const BMI_readout_t *data) {
  check_size_and_send();
  const uint32_t pack = (static_cast<uint32_t>(data->sum) << 24) + data->adc; // or is this backwards?
  // alternative
  // const uint32_t pack = (data->adc << 8) + data->sum;

  auto *dp = reinterpret_cast<struct BMIData *>(buffer + DataSize);
  dp->Ring = Ring;
  dp->FEN = FEN;
  dp->Length = sizeof(struct BMIData);
  dp->TimeHigh = t.high();
  dp->TimeLow = t.low();
  dp->Channel = data->channel;
  dp->Pack = pack;
  DataSize += dp->Length;
  const auto hp = reinterpret_cast<PacketHeaderV0 *>(&buffer[0]);
  hp->TotalLength = DataSize;
}


void Sender::addReadout(const uint8_t Ring, const uint8_t FEN, const double tof, const double, const void *data) {
  // FIXME Make this class thread safe by adding a mutex lock
  // roll a full packet first, so the event time uses the packet's pulse time
  check_size_and_send();
  // provided time-of-flight plus the current pulse time; folding attributes the
  // event to the frame it would be detected in, as the real readout reports it
  const auto t = fold_tof_ ? time + (efu_time(tof) % period) : time + efu_time(tof);
  lasthi = t.high();
  lastlo = t.low();
  // the sender ignores any weight and just sends.
  addReadout(Ring, FEN, t, data);
}


void Sender::addReadout(const uint8_t Ring, const uint8_t FEN, const efu_time t, const void *data){
  switch (readout_type) {
    case ReadoutType::CAEN: return addReadout(Ring, FEN, t, static_cast<const CAEN_readout_t*>(data));
    case ReadoutType::TTLMonitor: return addReadout(Ring, FEN, t, static_cast<const TTLMonitor_readout_t*>(data));
    case ReadoutType::CDT: return addReadout(Ring, FEN, t, static_cast<const CDT_readout_t*>(data));
    case ReadoutType::VMM3: return addReadout(Ring, FEN, t, static_cast<const VMM3_readout_t*>(data));
    case ReadoutType::BM0: return addReadout(Ring, FEN, t, static_cast<const BM0_readout_t*>(data));
    case ReadoutType::BM2: return addReadout(Ring, FEN, t, static_cast<const BM2_readout_t*>(data));
    case ReadoutType::BMI: return addReadout(Ring, FEN, t, static_cast<const BMI_readout_t*>(data));
    default: throw std::runtime_error("This readout data type not implemented yet!");
  }
}


int Sender::send() {
  int error_code = 0;
  {
    std::lock_guard lock(send_mutex);
    const auto hp = reinterpret_cast<PacketHeaderV0 *>(&buffer[0]);
    hp->SeqNum = SeqNum++;
    const auto chr_ptr = reinterpret_cast<char *>(buffer);
    auto [bytes, ret_code] = sender.send(std::string(chr_ptr, chr_ptr + DataSize));
    error_code = ret_code;
    if (error_code < 0 && verbosity > -1){
      std::cout << "Sending UDP data failed: returns " << error_code << "\n";
    }
  }
  return error_code;
}


static int check_and_send_tcp(const std::string & addr, const uint16_t port, std::string && message, const int verbosity){
  const cluon::TCPConnection connection(addr, port,
     [](std::string &&data, auto &&ts) noexcept {
       const auto timestamp(std::chrono::system_clock::to_time_t(ts));
       std::cout << "Received " << data.size() << " bytes"
                 << " at " << std::put_time(std::localtime(&timestamp), "%Y-%m-%d %X")
                 << ", containing '" << data << "'." << std::endl;
     },
     [](){ std::cout << "Connection lost." << std::endl; });
  const auto len = static_cast<long>(message.size());
  auto [bytes, error_code] = connection.send(std::move(message));
  if (bytes != len && verbosity > -1){
    std::cout << "Failed to send full message, sent " << bytes << " of " << len << " bytes\n";
    return -1;
  }
  return error_code;
}


int Sender::command_shutdown() const {
  int ok = check_and_send_tcp(ipaddr, tcp_port, "EXIT\n", verbosity);
  if (ok < 0) {
    if (verbosity > -1) std::cout << "Could not connect to " << ipaddr << ":" << tcp_port << std::endl;
    return -3;
  }
  if (ok == 0) {
    if (verbosity > -1) std::cout << "Could not read response from EXIT command" << std::endl;
    return -2;
  }
  // ok == 1 means the connection succeeded and the response was read.
  // Let's now check that the EXIT command was executed
  ok *= 100; // try up to 100 times
  while (ok-- > 0){
    ok = check_and_send_tcp(ipaddr, tcp_port, "", verbosity);
  }
  if (ok > 0 && verbosity > 0){
    // the server is still alive.
    std::cout << "The server is still alive after a successful EXIT command" << std::endl;
  }
  return 0;
}


