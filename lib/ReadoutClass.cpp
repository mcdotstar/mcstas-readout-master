// Copyright (C) 2022 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief ESS UDP readout generator class implementation
///
//===----------------------------------------------------------------------===//
#include "ReadoutClass.h"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <tuple>

void Readout::setPulseTime(const uint32_t PHI, const uint32_t PLO, const uint32_t PPHI, const uint32_t PPLO) {
  phi = PHI;
  plo = PLO;
  pphi = PPHI;
  pplo = PPLO;
}

std::pair<uint32_t, uint32_t> Readout::lastPulseTime() const {
  return std::make_pair(phi, plo);
}
std::pair<uint32_t, uint32_t> Readout::prevPulseTime() const {
  return std::make_pair(pphi, pplo);
}
std::pair<uint32_t, uint32_t> Readout::lastEventTime() const {
  return std::make_pair(lasthi, lastlo);
}

void Readout::newPacket() {
  memset(buffer, 0x00, sizeof(buffer));
  hp->Padding0 = 0;
  hp->Version = 0;
  hp->CookieAndType = (Type << 24) + 0x535345;
  hp->OutputQueue = OutputQueue;
  hp->TotalLength = sizeof(struct PacketHeaderV0);
  hp->SeqNum = SeqNum++;
  hp->TimeSource = 0;
  hp->PulseHigh = phi;
  hp->PulseLow = plo;
  hp->PrevPulseHigh = pphi;
  hp->PrevPulseLow = pplo;
  DataSize = sizeof(struct PacketHeaderV0);
}

void Readout::check_size_and_send() {
  if (DataSize >= MaxDataSize) {
    send();
    newPacket();
  }
}

void Readout::addReadout(const uint8_t Ring, const uint8_t FEN, const efu_time t, const CAEN_readout_t *data) {
  check_size_and_send();
  if (verbosity > 2){
    std::cout << "Add to the packet Ring=" << static_cast<unsigned>(Ring) << " FEN=" << static_cast<unsigned>(FEN);
    std::cout << " TimeHigh=" << t.high() << " TimeLow=" << t.low() << " Tube=" << static_cast<unsigned>(data->channel);
    std::cout << " AmplA=" << data->a << " AmplB=" << data->b << std::endl;
  }
  auto *dp = (struct CaenData *)(buffer + DataSize);
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
  hp->TotalLength = DataSize;
}

void Readout::addReadout(const uint8_t Ring, const uint8_t FEN, const efu_time t, const TTLMonitor_readout_t *data) {
  if (verbosity > 2){
    std::cout << "Add to the packet Ring=" << static_cast<unsigned>(Ring) << " FEN=" << static_cast<unsigned>(FEN);
    std::cout << " TimeHigh=" << t.high() << " TimeLow=" << t.low() << " Pos=" << static_cast<unsigned>(data->pos);
    std::cout << " Channel=" << static_cast<unsigned>(data->channel) << " ADC=" << data->adc << std::endl;
  }
  check_size_and_send();
  auto *dp = (struct TTLMonitorData *)(buffer + DataSize);
  dp->Ring = Ring;
  dp->FEN = FEN;
  dp->Length = sizeof(struct TTLMonitorData);
  dp->TimeHigh = t.high();
  dp->TimeLow = t.low();
  dp->Pos = data->pos;
  dp->Channel = data->channel;
  dp->ADC = data->adc;
  DataSize += dp->Length;
  hp->TotalLength = DataSize;
}

void Readout::addReadout(const uint8_t Ring, const uint8_t FEN, const efu_time t, const CDT_readout_t *data) {
  check_size_and_send();
  auto *dp = (struct CDTData *)(buffer + DataSize);
  dp->Ring = Ring;
  dp->FEN = FEN;
  dp->Length = sizeof(struct CDTData);
  dp->TimeHigh = t.high();
  dp->TimeLow = t.low();
  dp->OM = data->om;
  dp->Cathode = data->cathode;
  dp->Anode = data->anode;
  DataSize += dp->Length;
  hp->TotalLength = DataSize;
}

void Readout::addReadout(const uint8_t Ring, const uint8_t FEN, const efu_time t, const VMM3_readout_t *data) {
  check_size_and_send();
  auto *dp = (struct VMM3Data *)(buffer + DataSize);
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
  hp->TotalLength = DataSize;
}

void Readout::addReadout(const uint8_t Ring, const uint8_t FEN, const efu_time t, const BM0_readout_t *data) {
  check_size_and_send();
  auto *dp = (struct BM0Data *)(buffer + DataSize);
  dp->Ring = Ring;
  dp->FEN = FEN;
  dp->Length = sizeof(struct BM0Data);
  dp->TimeHigh = t.high();
  dp->TimeLow = t.low();
  dp->Channel = data->channel;
  DataSize += dp->Length;
  hp->TotalLength = DataSize;
}

void Readout::addReadout(const uint8_t Ring, const uint8_t FEN, const efu_time t, const BM2_readout_t *data) {
  check_size_and_send();
  auto *dp = (struct BM2Data *)(buffer + DataSize);
  dp->Ring = Ring;
  dp->FEN = FEN;
  dp->Length = sizeof(struct BM2Data);
  dp->TimeHigh = t.high();
  dp->TimeLow = t.low();
  dp->Channel = data->channel;
  dp->X = data->pos_x;
  dp->Y = data->pos_y;
  DataSize += dp->Length;
  hp->TotalLength = DataSize;
}

void Readout::addReadout(const uint8_t Ring, const uint8_t FEN, const efu_time t, const BMI_readout_t *data) {
  check_size_and_send();
  const uint32_t pack = (static_cast<uint32_t>(data->sum) << 24) + data->adc; // or is this backwards?
  // alternative
  // const uint32_t pack = (data->adc << 8) + data->sum;

  auto *dp = (struct BMIData *)(buffer + DataSize);
  dp->Ring = Ring;
  dp->FEN = FEN;
  dp->Length = sizeof(struct BMIData);
  dp->TimeHigh = t.high();
  dp->TimeLow = t.low();
  dp->Channel = data->channel;
  dp->Pack = pack;
  DataSize += dp->Length;
  hp->TotalLength = DataSize;
}


void Readout::addReadout(const uint8_t Ring, const uint8_t FEN, const double tof, const double weight, const void *data) {
  // store the readout to file if requested
  if (writer.has_value()) writer->saveReadout(Ring, FEN, tof, weight, data);
  if (!network){
    if (verbosity > 1) std::cout << "No readout added to buffer due to disabled network" << std::endl;
    return;
  }
  // provided time-of-flight plus the current pulse time
  auto t = efu_time(tof) + time;
  // TODO implement t = (tof % period) + time -- such that we have realistic reference times
  lasthi = t.high();
  lastlo = t.low();
  // send the same event (possibly) multiple times, depending on the weighted counting rate
  if (weight > 0) {
    // only sample the Poisson distribution _once_ per weighted readout:
    const auto n_samples = random_poisson(weight);
    for (int i = 0; i < n_samples; ++i) addReadout(Ring, FEN, t, data);
  } else {
    // this is a noise event, which has randomized data and should always be sent
    addReadout(Ring, FEN, t, data);
  }
}


void Readout::addReadout(const uint8_t Ring, const uint8_t FEN, const efu_time t, const void *data){
  const auto type = readoutType_from_detectorType(Type);
  switch (type) {
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


void Readout::dump_to(const std::string & filename, const std::string & dataset_name){
  writer = Writer(filename, Type, readoutType_from_detectorType(Type), dataset_name);
}


int Readout::send() {
  if (!network){
    if (verbosity > 1) std::cout << "No packet sent due to disabled network" << std::endl;
    return 0;
  }
  auto chr_ptr = reinterpret_cast<char *>(buffer);
  auto [bytes, error_code] = sender.send(std::string(chr_ptr, chr_ptr + DataSize));
  if (error_code < 0 && verbosity > -1){
    std::cout << "Sending UDP data failed: returns " << error_code << "\n";
  }
  newPacket();
  return error_code;
}

static int check_and_send_tcp(const std::string & addr, uint16_t port, std::string && message, const int verbosity){
  cluon::TCPConnection connection(addr, port,
     [](std::string &&data, auto &&ts) noexcept {
       const auto timestamp(std::chrono::system_clock::to_time_t(ts));
       std::cout << "Received " << data.size() << " bytes"
                 << " at " << std::put_time(std::localtime(&timestamp), "%Y-%m-%d %X")
                 << ", containing '" << data << "'." << std::endl;
     },
     [](){ std::cout << "Connection lost." << std::endl; });
  auto len = static_cast<long>(message.size());
  auto [bytes, error_code] = connection.send(std::move(message));
  if (bytes != len && verbosity > -1){
    std::cout << "Failed to send full message, sent " << bytes << " of " << len << " bytes\n";
    return -1;
  }
  return error_code;
}

int Readout::command_shutdown() const {
  if (!network){
    if (verbosity > 1) std::cout << "No shutdown command sent due to disabled network" << std::endl;
    return 0;
  }
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
    ok = check_and_send_tcp(ipaddr, tcp_port, nullptr, verbosity);
  }
  if (ok > 0 && verbosity > 0){
    // the server is still alive.
    std::cout << "The server is still alive after a successful EXIT command" << std::endl;
  }
  return 0;
}


