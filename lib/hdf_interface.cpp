#include "hdf_interface.h"
#include <unordered_map>


HighFive::CompoundType create_compound_caen_readout(){
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

HighFive::CompoundType create_compound_ttlmonitor_readout(){
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

HighFive::CompoundType create_compound_dream_readout(){
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

HighFive::CompoundType create_compound_vmm3_readout(){
  return {
    {"ring",   HighFive::create_datatype<uint8_t>()},
    {"FEN",    HighFive::create_datatype<uint8_t>()},
    {"time",   HighFive::create_datatype<double>()},
    {"weight", HighFive::create_datatype<double>()},
    {"bc", HighFive::create_datatype<uint16_t>()},
    {"otadc", HighFive::create_datatype<uint16_t>()},
    {"geo", HighFive::create_datatype<uint8_t>()},
    {"tdc", HighFive::create_datatype<uint8_t>()},
    {"vmm", HighFive::create_datatype<uint8_t>()},
    {"channel", HighFive::create_datatype<uint8_t>()},
  };
}

HighFive::CompoundType create_compound_bm0_readout(){
  return {
    {"ring",    HighFive::create_datatype<uint8_t>()},
    {"FEN",     HighFive::create_datatype<uint8_t>()},
    {"time",    HighFive::create_datatype<double>()},
    {"weight",  HighFive::create_datatype<double>()},
    {"channel", HighFive::create_datatype<uint8_t>()},
  };
}

HighFive::CompoundType create_compound_bm2_readout(){
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

HighFive::CompoundType create_compound_bmi_readout(){
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
  template<> DataType create_datatype<CAEN_event>(){return create_compound_caen_readout();}
  template<> DataType create_datatype<TTLMonitor_event>(){return create_compound_ttlmonitor_readout();}
  template<> DataType create_datatype<CDT_event>(){return create_compound_dream_readout();}
  template<> DataType create_datatype<VMM3_event>(){return create_compound_vmm3_readout();}
  template<> DataType create_datatype<BM0_event>(){return create_compound_bm0_readout();}
  template<> DataType create_datatype<BM2_event>(){return create_compound_bm2_readout();}
  template<> DataType create_datatype<BMI_event>(){return create_compound_bmi_readout();}
}

namespace {
/// Map canonical type names to HighFive DataType creators
HighFive::DataType hdf5_type_for(const std::string& canonical_type) {
  static const std::unordered_map<std::string, HighFive::DataType(*)()> creators = {
    {"int8_t",   [] () -> HighFive::DataType { return HighFive::create_datatype<int8_t>(); }},
    {"int16_t",  [] () -> HighFive::DataType { return HighFive::create_datatype<int16_t>(); }},
    {"int32_t",  [] () -> HighFive::DataType { return HighFive::create_datatype<int32_t>(); }},
    {"int64_t",  [] () -> HighFive::DataType { return HighFive::create_datatype<int64_t>(); }},
    {"uint8_t",  [] () -> HighFive::DataType { return HighFive::create_datatype<uint8_t>(); }},
    {"uint16_t", [] () -> HighFive::DataType { return HighFive::create_datatype<uint16_t>(); }},
    {"uint32_t", [] () -> HighFive::DataType { return HighFive::create_datatype<uint32_t>(); }},
    {"uint64_t", [] () -> HighFive::DataType { return HighFive::create_datatype<uint64_t>(); }},
    {"float",    [] () -> HighFive::DataType { return HighFive::create_datatype<float>(); }},
    {"double",   [] () -> HighFive::DataType { return HighFive::create_datatype<double>(); }},
  };

  const auto it = creators.find(canonical_type);
  if (it == creators.end()) {
    throw std::runtime_error("No HDF5 type mapping for: " + canonical_type);
  }
  return it->second();
}
} // namespace

HighFive::CompoundType build_hdf5_compound_type(const TypeSchema& schema) {
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
