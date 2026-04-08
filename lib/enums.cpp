#include "enums.h"

DetectorType detectorType_from_int(const int type){
  switch(type){
    case 0x00: return Reserved;
    case 0x10: return TTLMonitor;
    case 0x30: return LOKI;
    case 0x32: return TBL3H3;
    case 0x34: return BIFROST;
    case 0x38: return MIRACLES;
    case 0x3c: return CSPEC;
    case 0x40: return TREX;
    case 0x44: return NMX;
    case 0x48: return FREIA;
    case 0x49: return TBLVMM;
    case 0x4c: return ESTIA;
    case 0x50: return BEER;
    case 0x60: return DREAM;
    case 0x64: return MAGIC;
    case 0x68: return HEIMDAL;
    case 0xf0: return CBM0;
    case 0xf1: return CBM1;
    case 0xf2: return CBM2;
    case 0xfa: return CBMI;
    default: throw std::runtime_error("Undefined DetectorType");
  }
}

ReadoutType readoutType_from_detectorType(const DetectorType type){
  switch(type){
    case TTLMonitor: return ReadoutType::TTLMonitor;
    case LOKI:
    case TBL3H3:
    case BIFROST:
    case MIRACLES:
    case CSPEC: return ReadoutType::CAEN;
    case TREX:
    case NMX:
    case FREIA:
    case TBLVMM:
    case ESTIA: return ReadoutType::VMM3;
    case DREAM:
    case MAGIC:
    case HEIMDAL: return ReadoutType::CDT;
    case CBM1:
    case CBM2:
    case BEER: return ReadoutType::BM2;
    case CBM0: return ReadoutType::BM0;
    case CBMI: return ReadoutType::BMI;
    default: throw std::runtime_error("No ReadoutType for provided DetectorType");
  }
}

ReadoutType readoutType_from_int(const int int_type) {
  return readoutType_from_detectorType(detectorType_from_int(int_type));
}

std::string detectorType_name(DetectorType dt){
  switch (dt) {
    case DetectorType::TTLMonitor: return "DetectorType::TTLMonitor";
    case DetectorType::LOKI: return "DetectorType::LOKI";
    case DetectorType::TBL3H3: return "DetectorType::TBL3H3";
    case DetectorType::BIFROST: return "DetectorType::BIFROST";
    case DetectorType::MIRACLES: return "DetectorType::MIRACLES";
    case DetectorType::CSPEC: return "DetectorType::CSPEC";
    case DetectorType::TREX: return "DetectorType::TREX";
    case DetectorType::NMX: return "DetectorType::NMX";
    case DetectorType::FREIA: return "DetectorType::FREIA";
    case DetectorType::TBLVMM: return "DetectorType::TBLVMM";
    case DetectorType::ESTIA: return "DetectorType::ESTIA";
    case DetectorType::BEER: return "DetectorType::BEER";
    case DetectorType::DREAM: return "DetectorType::DREAM";
    case DetectorType::MAGIC: return "DetectorType::MAGIC";
    case DetectorType::HEIMDAL: return "DetectorType::HEIMDAL";
    case DetectorType::CBM0: return "DetectorType::CBM0";
    case DetectorType::CBM1: return "DetectorType::CBM1";
    case DetectorType::CBM2: return "DetectorType::CBM2";
    case DetectorType::CBMI: return "DetectorType::CBMI";
    default: return "DetectorType::Reserved";
  }
}
std::string readoutType_name(ReadoutType rt){
  switch (rt){
    case ReadoutType::TTLMonitor: return "ReadoutType::TTLMonitor";
    case ReadoutType::CAEN: return "ReadoutType::CAEN";
    case ReadoutType::VMM3: return "ReadoutType::VMM3";
    case ReadoutType::CDT: return "ReadoutType::CDT";
    case ReadoutType::BM0: return "ReadoutType::BM0";
    case ReadoutType::BM2: return "ReadoutType::BM2";
    case ReadoutType::BMI: return "ReadoutType::BMI";
    default: return "";
  }
}
DetectorType detectorType_from_name(const std::string & name){
  if (name == "DetectorType::TTLMonitor") return DetectorType::TTLMonitor;
  if (name == "DetectorType::LOKI") return DetectorType::LOKI;
  if (name == "DetectorType::TBL3H3") return DetectorType::TBL3H3;
  if (name == "DetectorType::BIFROST") return DetectorType::BIFROST;
  if (name == "DetectorType::MIRACLES") return DetectorType::MIRACLES;
  if (name == "DetectorType::CSPEC") return DetectorType::CSPEC;
  if (name == "DetectorType::TREX") return DetectorType::TREX;
  if (name == "DetectorType::NMX") return DetectorType::NMX;
  if (name == "DetectorType::FREIA") return DetectorType::FREIA;
  if (name == "DetectorType::TBLVMM") return DetectorType::TBLVMM;
  if (name == "DetectorType::ESTIA") return DetectorType::ESTIA;
  if (name == "DetectorType::BEER") return DetectorType::BEER;
  if (name == "DetectorType::DREAM") return DetectorType::DREAM;
  if (name == "DetectorType::MAGIC") return DetectorType::MAGIC;
  if (name == "DetectorType::HEIMDAL") return DetectorType::HEIMDAL;
  if (name == "DetectorType::CBM0") return DetectorType::CBM0;
  if (name == "DetectorType::CBM1") return DetectorType::CBM1;
  if (name == "DetectorType::CBM2") return DetectorType::CBM2;
  if (name == "DetectorType::CBMI") return DetectorType::CBMI;
  if (name == "DetectorType::Reserved") return DetectorType::Reserved;
  std::stringstream s;
  s << "Provided name=" << name << " is not a known DetectorType";
  throw std::runtime_error(s.str());
}
ReadoutType readoutType_from_name(const std::string & name){
  if (name == "ReadoutType::TTLMonitor") return ReadoutType::TTLMonitor;
  if (name == "ReadoutType::CAEN") return ReadoutType::CAEN;
  if (name == "ReadoutType::VMM3") return ReadoutType::VMM3;
  if (name == "ReadoutType::CDT") return ReadoutType::CDT;
  if (name == "ReadoutType::BM0") return ReadoutType::BM0;
  if (name == "ReadoutType::BM2") return ReadoutType::BM2;
  if (name == "ReadoutType::BMI") return ReadoutType::BMI;
  std::stringstream s;
  s << "Provided name=" << name << " is not a known ReadoutType";
  throw std::runtime_error(s.str());
}
