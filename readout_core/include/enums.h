#pragma once
#include <string>

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

/// Console output level shared by the library classes; each level includes the previous ones.
enum class Verbosity {
  silent,   ///< no output at all
  errors,   ///< errors only
  warnings, ///< errors and warnings
  info,     ///< progress information
  details   ///< per-event details
};

/** \brief ESS detector (instrument) identifiers.
 *
 * The value is the packet-type byte of the ESS readout header: EFUs filter
 * incoming packets on it, and collector files store it as the group's
 * detector identity. Each DetectorType maps to exactly one ReadoutType
 * record layout via readoutType_from_detectorType().
 */
enum DetectorType {
  Reserved = 0x00,
  TTLMonitor = 0x10,
  LOKI = 0x30,
  TBL3H3 = 0x32,
  BIFROST = 0x34,
  MIRACLES = 0x38,
  CSPEC = 0x3c,
  TREX = 0x40,
  NMX = 0x44,
  FREIA = 0x48,
  TBLVMM = 0x49,
  ESTIA=0x4c,
  BEER=0x50,
  DREAM = 0x60,
  MAGIC = 0x64,
  HEIMDAL = 0x68,
  // Common Beam Monitor types don't (yet) have defined identifiers
  CBM0 = 0xf0,
  CBM1 = 0xf1,
  CBM2 = 0xf2,
  CBMI = 0xfa,
};

/** \brief Readout record layouts understood by the library.
 *
 * Each value names one canonical C-struct record layout (see
 * readout_type_descriptions.h) shared by every DetectorType that uses the
 * same front-end electronics.
 */
enum class ReadoutType {
  TTLMonitor, ///< TTL beam monitor: channel, position, ADC
  CAEN,       ///< CAEN digitizer: group channel and amplitudes A-D
  VMM3,       ///< VMM3 ASIC: BC, OTADC, GEO, TDC, VMM, channel
  CDT,        ///< CDT (DREAM family): output module, cathode, anode
  BM0,        ///< minimal beam monitor: channel only
  BM2,        ///< position-resolving beam monitor: channel, x, y
  BMI,        ///< integrating beam monitor: channel, sum, 32-bit ADC
};

/// Checked conversion from the ESS packet-type byte; throws for unknown values.
RL_API DetectorType detectorType_from_int(int);
/// The unique record layout used by a given detector.
RL_API ReadoutType readoutType_from_detectorType(DetectorType type);
/// Shorthand for readoutType_from_detectorType(detectorType_from_int(int_type)).
RL_API ReadoutType readoutType_from_int(int int_type);

/// Checked conversion from a detector name such as "DetectorType::BIFROST"; throws for unknown names.
RL_API DetectorType detectorType_from_name(const std::string & name);
/// Checked conversion from a readout name such as "ReadoutType::CAEN"; throws for unknown names.
RL_API ReadoutType readoutType_from_name(const std::string & name);
/// Qualified name of a detector, e.g. "DetectorType::BIFROST".
RL_API std::string detectorType_name(DetectorType);
/// Qualified name of a readout layout, e.g. "ReadoutType::CAEN".
RL_API std::string readoutType_name(ReadoutType);
