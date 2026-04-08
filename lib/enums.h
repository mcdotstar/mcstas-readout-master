#pragma once
#include "hdf_interface.h"

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

enum class Verbosity {
  silent,
  errors,
  warnings,
  info,
  details
};

enum DetectorType {
  Reserved = 0x00,
  TTLMonitor = 0x10,
  LOKI = 0x30,
  TBL3H3 = 0x32,
  BIFROST = 0x34,
  MIRACLES = 0x38,
  CSPEC = 0x3c,
  TREX = 0x40,
  NMX = 0x42,
  FREIA = 0x48,
  TBLVMM = 0x49,
  ESTIA=0x4c,
  BEER=0x50,
  DREAM = 0x60,
  MAGIC = 0x64,
  HEIMDAL = 0x68,
  CBM0 = 0xf0,
  CBM1 = 0xf1,
  CBM2 = 0xf2,
  CBMI = 0xfa,
};

enum class ReadoutType {
  TTLMonitor,
  CAEN,
  VMM3,
  CDT,
  BM0,
  BM2,
  BMI,
};

RL_API DetectorType detectorType_from_int(int);
RL_API ReadoutType readoutType_from_detectorType(DetectorType type);
RL_API ReadoutType readoutType_from_int(int int_type);

RL_API DetectorType detectorType_from_name(const std::string & name);
RL_API ReadoutType readoutType_from_name(const std::string & name);
RL_API std::string detectorType_name(DetectorType);
RL_API std::string readoutType_name(ReadoutType);
