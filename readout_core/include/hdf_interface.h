#pragma once
#include <chrono>
#include <thread>

#include <highfive/H5File.hpp>
#include <highfive/H5DataSet.hpp>
#include <highfive/H5DataSpace.hpp>

#include "Readout.h"
#include "TypeDescriptionParser.h"
#include "enums.h"

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

/** \brief Common fields of every stored readout event: routing (ring, FEN),
 * time-of-flight, and the statistical rate-weight.
 */
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

/// \brief CAEN readout event: group channel and amplitudes A-D.
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

/// \brief TTL beam-monitor readout event: channel, position, ADC.
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

/// \brief CDT (DREAM-family) readout event: output module, cathode, anode.
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

/// \brief VMM3 readout event: BC, OTADC, GEO, TDC, VMM, channel.
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

/// \brief Minimal beam-monitor readout event: channel only.
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

/// \brief Position-resolving beam-monitor readout event: channel, x, y.
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

/// \brief Integrating beam-monitor readout event: channel, sum, 24-bit ADC.
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


inline HighFive::CompoundType create_compound_caen_readout(){
  return {
    {"ring",    HighFive::create_datatype<uint8_t>()},
    {"FEN",     HighFive::create_datatype<uint8_t>()},
    {"time",    HighFive::create_datatype<double>()},
    {"weight",  HighFive::create_datatype<double>()},
    {"channel", HighFive::create_datatype<uint8_t>()},
    {"a",       HighFive::create_datatype<uint16_t>()},
    {"b",       HighFive::create_datatype<uint16_t>()},
    {"c",       HighFive::create_datatype<uint16_t>()},
    {"d",       HighFive::create_datatype<uint16_t>()},
  };
}

inline HighFive::CompoundType create_compound_ttlmonitor_readout(){
  return {
    {"ring",    HighFive::create_datatype<uint8_t>()},
    {"FEN",     HighFive::create_datatype<uint8_t>()},
    {"time",    HighFive::create_datatype<double>()},
    {"weight",  HighFive::create_datatype<double>()},
    {"channel", HighFive::create_datatype<uint8_t>()},
    {"pos",     HighFive::create_datatype<uint8_t>()},
    {"adc",     HighFive::create_datatype<uint16_t>()},
  };
}

inline HighFive::CompoundType create_compound_dream_readout(){
  return {
    {"ring",    HighFive::create_datatype<uint8_t>()},
    {"FEN",     HighFive::create_datatype<uint8_t>()},
    {"time",    HighFive::create_datatype<double>()},
    {"weight",  HighFive::create_datatype<double>()},
    {"om",      HighFive::create_datatype<uint8_t>()},
    {"cathode", HighFive::create_datatype<uint8_t>()},
    {"anode",   HighFive::create_datatype<uint8_t>()},
  };
}

inline HighFive::CompoundType create_compound_vmm3_readout(){
  return {
    {"ring",    HighFive::create_datatype<uint8_t>()},
    {"FEN",     HighFive::create_datatype<uint8_t>()},
    {"time",    HighFive::create_datatype<double>()},
    {"weight",  HighFive::create_datatype<double>()},
    {"bc",      HighFive::create_datatype<uint16_t>()},
    {"otadc",   HighFive::create_datatype<uint16_t>()},
    {"geo",     HighFive::create_datatype<uint8_t>()},
    {"tdc",     HighFive::create_datatype<uint8_t>()},
    {"vmm",     HighFive::create_datatype<uint8_t>()},
    {"channel", HighFive::create_datatype<uint8_t>()},
  };
}

inline HighFive::CompoundType create_compound_bm0_readout(){
  return {
    {"ring",    HighFive::create_datatype<uint8_t>()},
    {"FEN",     HighFive::create_datatype<uint8_t>()},
    {"time",    HighFive::create_datatype<double>()},
    {"weight",  HighFive::create_datatype<double>()},
    {"channel", HighFive::create_datatype<uint8_t>()},
  };
}

inline HighFive::CompoundType create_compound_bm2_readout(){
  return {
    {"ring",    HighFive::create_datatype<uint8_t>()},
    {"FEN",     HighFive::create_datatype<uint8_t>()},
    {"time",    HighFive::create_datatype<double>()},
    {"weight",  HighFive::create_datatype<double>()},
    {"channel", HighFive::create_datatype<uint8_t>()},
    {"pos_x",   HighFive::create_datatype<uint16_t>()},
    {"pos_y",   HighFive::create_datatype<uint16_t>()},
  };
}

inline HighFive::CompoundType create_compound_bmi_readout(){
  return {
    {"ring",    HighFive::create_datatype<uint8_t>()},
    {"FEN",     HighFive::create_datatype<uint8_t>()},
    {"time",    HighFive::create_datatype<double>()},
    {"weight",  HighFive::create_datatype<double>()},
    {"channel", HighFive::create_datatype<uint8_t>()},
    {"sum",     HighFive::create_datatype<uint8_t>()},
    {"adc",     HighFive::create_datatype<uint32_t>()},
  };
}

namespace HighFive {
  template<> inline DataType create_datatype<CAEN_event>(){return create_compound_caen_readout();}
  template<> inline DataType create_datatype<TTLMonitor_event>(){return create_compound_ttlmonitor_readout();}
  template<> inline DataType create_datatype<CDT_event>(){return create_compound_dream_readout();}
  template<> inline DataType create_datatype<VMM3_event>(){return create_compound_vmm3_readout();}
  template<> inline DataType create_datatype<BM0_event>(){return create_compound_bm0_readout();}
  template<> inline DataType create_datatype<BM2_event>(){return create_compound_bm2_readout();}
  template<> inline DataType create_datatype<BMI_event>(){return create_compound_bmi_readout();}
}

/// \brief Build the canonical HDF5 compound type for a known readout type.
inline HighFive::CompoundType hdf_compound_type(const ReadoutType readout) {
  switch (readout) {
    case ReadoutType::CAEN: return create_compound_caen_readout();
    case ReadoutType::TTLMonitor: return create_compound_ttlmonitor_readout();
    case ReadoutType::CDT: return create_compound_dream_readout();
    case ReadoutType::VMM3: return create_compound_vmm3_readout();
    case ReadoutType::BM0: return create_compound_bm0_readout();
    case ReadoutType::BM2: return create_compound_bm2_readout();
    case ReadoutType::BMI: return create_compound_bmi_readout();
    default: throw std::runtime_error("Saving this readout type is not implemented yet!");
  }
}

/// \brief Map a canonical scalar type name (e.g. "uint16_t") to its HDF5 datatype.
inline HighFive::DataType hdf5_type_for(const std::string& canonical_type) {
  if (canonical_type == "int8_t") return HighFive::create_datatype<int8_t>();
  if (canonical_type == "int16_t") return HighFive::create_datatype<int16_t>();
  if (canonical_type == "int32_t") return HighFive::create_datatype<int32_t>();
  if (canonical_type == "int64_t") return HighFive::create_datatype<int64_t>();
  if (canonical_type == "uint8_t") return HighFive::create_datatype<uint8_t>();
  if (canonical_type == "uint16_t") return HighFive::create_datatype<uint16_t>();
  if (canonical_type == "uint32_t") return HighFive::create_datatype<uint32_t>();
  if (canonical_type == "uint64_t") return HighFive::create_datatype<uint64_t>();
  if (canonical_type == "float") return HighFive::create_datatype<float>();
  if (canonical_type == "double") return HighFive::create_datatype<double>();
  throw std::runtime_error("No HDF5 type mapping for: " + canonical_type);
}

/// \brief Build an HDF5 compound datatype from a parsed C-struct description
///
/// Array fields are not supported: descriptions used with the collector engine
/// must contain scalar fields only.
inline HighFive::CompoundType build_hdf5_compound_type(const TypeSchema & schema) {
  std::vector<HighFive::CompoundType::member_def> members;
  members.reserve(schema.fields.size());

  for (const auto& field : schema.fields) {
    if (field.array_count > 0) {
      throw std::runtime_error(
        "build_hdf5_compound_type: array fields are not supported (field: " + field.name + ")"
      );
    }
    members.push_back({field.name, hdf5_type_for(field.type), field.offset});
  }

  return HighFive::CompoundType(members, schema.total_size);
}
