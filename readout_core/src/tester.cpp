#include "Readout.h"

int main(){
  // readout_t * bifrost_readout_create(char* address, int port, double source_frequency, int type);
  char addr[] = "127.0.0.1";
  auto detector_efu = readout_create(addr, 9000, 8888, 1 / 14., 0x34);
  auto monitor_efu = readout_create(addr, 9001, 8889, 1 / 14., 0x10);
  
  readout_newPacket(detector_efu);

  TTLMonitor_readout_t ttl_data;
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

    ttl_data.ttlmonitor_readout_pos = tube;
    ttl_data.ttlmonitor_readout_channel = 0;
    ttl_data.ttlmonitor_readout_adc = i;
    readout_add(monitor_efu, 0, 100, tof, static_cast<const void *>(&ttl_data));
    ttl_data.ttlmonitor_readout_channel = 1;
    ttl_data.ttlmonitor_readout_adc = max - i;
    readout_add(monitor_efu, 0, 100, tof, static_cast<const void *>(&ttl_data));
  }
  readout_send(detector_efu);
  readout_send(monitor_efu);

  readout_destroy(detector_efu);
  readout_destroy(monitor_efu);

  return 0;
}
