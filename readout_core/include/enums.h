#pragma once
#include <cstdint>
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
 * Collector files store the name as the group's detector identity, and each
 * DetectorType maps to exactly one ReadoutType record layout via
 * readoutType_from_detectorType(). For instruments the value is also the
 * packet-type byte of the ESS readout header, which EFUs filter on; see
 * packetType_from_detectorType() for the beam monitors, where it is not.
 */
enum DetectorType {
  Reserved = 0x00,
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
  // Common Beam Monitors. The EFU's cbm module takes every beam-monitor format in
  // packets of one type, CBM_PACKET_TYPE, and tells them apart by each readout's own
  // type byte. These values are the library's identities for the three formats, so a
  // collector group records which one it holds; they never go on the wire.
  CBM0 = 0xf0,
  CBM1 = 0xf1,
  CBM2 = 0xf2,
  CBMI = 0xfa,
};

/// Packet-type byte of every Common Beam Monitor packet: DetectorType::CBM in the EFU's
/// src/common/types/DetectorType.h.
constexpr int CBM_PACKET_TYPE = 0x10;

/** \brief Readout record layouts understood by the library.
 *
 * Each value names one canonical C-struct record layout (see
 * readout_type_descriptions.h) shared by every DetectorType that uses the
 * same front-end electronics.
 */
enum class ReadoutType {
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
/// The packet-type byte an EFU expects in the ESS readout header for this detector:
/// the DetectorType's own value, except CBM_PACKET_TYPE for every Common Beam Monitor.
RL_API uint8_t packetType_from_detectorType(DetectorType type);
/// The per-readout type byte of a Common Beam Monitor readout, from the EFU's
/// src/modules/cbm/CbmTypes.h: EVENT_0D (1) for BM0, EVENT_2D (2) for BM2, IBM (3) for BMI.
/// Throws for layouts that are not beam monitors.
RL_API uint8_t cbmType_from_readoutType(ReadoutType type);
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
