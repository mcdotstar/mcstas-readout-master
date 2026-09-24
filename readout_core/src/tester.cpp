#include "Readout.h"

int main(){
  // readout_t * bifrost_readout_create(char* address, int port, double source_frequency, int type);
  char addr[] = "127.0.0.1";
  auto detector_efu = readout_create(addr, 9000, 8888, 1 / 14., 0x34);
  
  readout_newPacket(detector_efu);

  CAEN_readout_t caen_data;

  uint16_t max = 1000;
  for (uint16_t i=0; i<max; ++i){
    uint8_t ring = 1;
    uint8_t fen = 0;
    uint8_t tube = 3;
    double tof = static_cast<double>(i)/static_cast<double>(max);

    caen_data.caen_readout_channel = tube;
    caen_data.caen_readout_a = i;
    caen_data.caen_readout_b = max - i;
    caen_data.caen_readout_c = 0;
    caen_data.caen_readout_d = 0;

    readout_add(detector_efu, ring, fen, tof, static_cast<const void *>(&caen_data));

  }
  readout_send(detector_efu);

  readout_destroy(detector_efu);

  return 0;
}
