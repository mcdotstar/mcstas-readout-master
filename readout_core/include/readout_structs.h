/// \file Readout_structs.h
/// \brief C-compatible definitions of readout data structures for different detector types, used in the
///        Readout and Collector C APIs. These structures should match the expected format of the readout data for each
///        detector type, and are used to ensure that the data is correctly interpreted when added to the Collector or
///        sent by the Readout.
///
///  The fields of each structure are named slightly differently from the EFU/ICD defined structures in order to
///  avoid macro name clashes from within McStas' runtime when using the C API from McStas.

#ifndef READOUT_STRUCTS_H
#define READOUT_STRUCTS_H

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

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

  struct CAEN_readout {
    uint8_t channel;
    uint16_t a;
    uint16_t b;
    uint16_t c;
    uint16_t d;
  };

  struct TTLMonitor_readout {
    uint8_t channel;
    uint8_t pos;
    uint16_t adc;
  };

  struct CDT_readout {
    uint8_t om;
    uint8_t cathode;
    uint8_t anode;
  };

  struct VMM3_readout {
    uint16_t bc;
    uint16_t otadc;
    uint8_t geo;
    uint8_t tdc;
    uint8_t vmm;
    uint8_t channel;
  };

  struct BM0_readout {
    uint8_t channel;
  };

  struct BM2_readout {
    uint8_t channel;
    uint16_t pos_x;
    uint16_t pos_y;
  };

  struct BMI_readout {
    uint8_t channel;
    uint8_t sum;
    uint32_t adc; // 24 bit ADC value, but stored in a 32 bit integer for alignment reasons
  };

  typedef struct CAEN_readout CAEN_readout_t;
  typedef struct TTLMonitor_readout TTLMonitor_readout_t;
  typedef struct CDT_readout CDT_readout_t;
  typedef struct VMM3_readout VMM3_readout_t;
  typedef struct BM0_readout BM0_readout_t;
  typedef struct BM2_readout BM2_readout_t;
  typedef struct BMI_readout BMI_readout_t;


#ifdef __cplusplus
}
#endif

#endif