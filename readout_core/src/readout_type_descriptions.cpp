#include "readout_type_descriptions.h"

const char * readout_type_description(const ReadoutType readout) {
  // Field names and order must match the create_compound_* definitions in hdf_interface.cpp;
  // the anti-drift unit test compares the parsed compound types for exact equality.
  switch (readout) {
    case ReadoutType::CAEN:
      return "uint8_t ring; uint8_t FEN; double time; double weight; uint8_t channel; uint16_t a; uint16_t b; uint16_t c; uint16_t d;";
    case ReadoutType::TTLMonitor:
      return "uint8_t ring; uint8_t FEN; double time; double weight; uint8_t channel; uint8_t pos; uint16_t adc;";
    case ReadoutType::CDT:
      return "uint8_t ring; uint8_t FEN; double time; double weight; uint8_t om; uint8_t cathode; uint8_t anode;";
    case ReadoutType::VMM3:
      return "uint8_t ring; uint8_t FEN; double time; double weight; uint16_t bc; uint16_t otadc; uint8_t geo; uint8_t tdc; uint8_t vmm; uint8_t channel;";
    case ReadoutType::BM0:
      return "uint8_t ring; uint8_t FEN; double time; double weight; uint8_t channel;";
    case ReadoutType::BM2:
      return "uint8_t ring; uint8_t FEN; double time; double weight; uint8_t channel; uint16_t pos_x; uint16_t pos_y;";
    case ReadoutType::BMI:
      return "uint8_t ring; uint8_t FEN; double time; double weight; uint8_t channel; uint8_t sum; uint32_t adc;";
    default:
      return nullptr;
  }
}
