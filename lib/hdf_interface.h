#pragma once
#include <chrono>
#include <thread>

#include <highfive/H5File.hpp>
#include <highfive/H5DataSet.hpp>
#include <highfive/H5DataSpace.hpp>

#include "Readout.h"

#ifdef WIN32
// Export symbols if compile flags "READOUT_SHARED" and "READOUT_EXPORT" are set on Windows.
    #ifdef READOUT_SHARED
        #ifdef READOUT_EXPORT
            #define RL_API __declspec(dllexport)
        #else
            #define RL_API __declspec(dllimport)
        #endif
    #else
        // Disable definition if linking statically.
        #define RL_API
    #endif
#else
// Disable definition for non-Win32 systems.
#define RL_API
#endif

//template<class T>
class RL_API Event{
public:
  uint8_t ring;
  uint8_t fen;
  double time;
  double weight;
  Event() = default;
  Event(uint8_t ring, uint8_t fen, double time, double weight)
  : ring(ring), fen(fen), time(time), weight(weight) {}
};

class RL_API CAEN_event: public Event {
public:
  uint8_t channel;
  uint16_t a;
  uint16_t b;
  uint16_t c;
  uint16_t d;
  explicit CAEN_event() = default;
  CAEN_event(uint8_t r, uint8_t f, double t, double w, const CAEN_readout * ro)
  : Event(r, f, t, w), channel{ro->channel}, a{ro->a}, b{ro->b}, c{ro->c}, d{ro->d} {}
  template<class T> void add(T & readout) const {
    auto r = CAEN_readout{channel, a, b, c, d};
    readout.addReadout(ring, fen, time, weight, static_cast<void *>(&r));
//    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
};

class RL_API TTLMonitor_event: public Event {
public:
  uint8_t channel;
  uint8_t pos;
  uint16_t adc;
  explicit TTLMonitor_event() = default;
  TTLMonitor_event(uint8_t r, uint8_t f, double t, double w, const TTLMonitor_readout * p)
  : Event(r, f, t, w), channel{p->channel}, pos{p->pos}, adc{p->adc} {}
  template<class T> void add(T & readout) const {
    auto r = TTLMonitor_readout{channel, pos, adc};
    readout.addReadout(ring, fen, time, weight, static_cast<void *>(&r));
//    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
};

class RL_API CDT_event: public Event {
public:
  uint8_t om;
  uint8_t cathode;
  uint8_t anode;
  explicit CDT_event() = default;
  CDT_event(uint8_t r, uint8_t f, double t, double w, const CDT_readout * p)
  : Event(r, f, t, w), om{p->om}, cathode{p->cathode}, anode{p->anode} {}
  template<class T> void add(T & readout) const {
    auto r = CDT_readout{om, cathode, anode};
    readout.addReadout(ring, fen, time, weight, static_cast<void *>(&r));
//    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
};

class RL_API VMM3_event: public Event {
public:
  uint16_t bc;
  uint16_t otadc;
  uint8_t geo;
  uint8_t tdc;
  uint8_t vmm;
  uint8_t channel;
  explicit VMM3_event() = default;
  VMM3_event(uint8_t r, uint8_t f, double t, double w, const VMM3_readout * p)
  : Event(r, f, t, w), bc{p->bc}, otadc{p->otadc}, geo{p->geo}, tdc{p->tdc}, vmm{p->vmm}, channel{p->channel} {}
  template<class T> void add(T & readout) const {
    auto r = VMM3_readout{bc, otadc, geo, tdc, vmm, channel};
    readout.addReadout(ring, fen, time, weight, static_cast<void *>(&r));
//    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
};

class RL_API BM0_event: public Event {
public:
  uint8_t channel;
  explicit BM0_event() = default;
  BM0_event(uint8_t r, uint8_t f, double t, double w, const BM0_readout * p)
  : Event(r, f, t, w), channel{p->channel} {}
  template<class T> void add(T & readout) const {
    auto r = BM0_readout{channel};
    readout.addReadout(ring, fen, time, weight, static_cast<void *>(&r));
  }
};

class RL_API BM2_event: public Event {
public:
  uint8_t channel;
  uint16_t pos_x;
  uint16_t pos_y;
  explicit BM2_event() = default;
  BM2_event(uint8_t r, uint8_t f, double t, double w, const BM2_readout * p)
  : Event(r, f, t, w), channel{p->channel} , pos_x{p->pos_x}, pos_y{p->pos_y} {}
  template<class T> void add(T & readout) const {
    auto r = BM2_readout{channel, pos_x, pos_y};
    readout.addReadout(ring, fen, time, weight, static_cast<void *>(&r));
  }
};

class RL_API BMI_event: public Event {
public:
  uint8_t channel;
  uint8_t sum;
  uint32_t adc; // 24 bit ADC value, but stored in a 32-bit integer for alignment reasons
  explicit BMI_event() = default;
  BMI_event(uint8_t r, uint8_t f, double t, double w, const BMI_readout * p)
  : Event(r, f, t, w), channel{p->channel} , sum{p->sum}, adc{p->adc} {}
  template<class T> void add(T & readout) const {
    auto r = BMI_readout{channel, sum, adc};
    readout.addReadout(ring, fen, time, weight, static_cast<void *>(&r));
  }
};


namespace HighFive {
  template<> RL_API DataType create_datatype<CAEN_event>();
  template<> RL_API DataType create_datatype<TTLMonitor_event>();
  template<> RL_API DataType create_datatype<CDT_event>();
  template<> RL_API DataType create_datatype<VMM3_event>();
  template<> RL_API DataType create_datatype<BM0_event>();
  template<> RL_API DataType create_datatype<BM2_event>();
  template<> RL_API DataType create_datatype<BMI_event>();
}
