#include <string>

#include "readout_orig.h"
#include "ReadoutClass.h"
#include "efu_time.h"

#ifdef __cplusplus
extern "C" {
#endif
  struct readout{
    void *obj;
//    void *rep;
    void *time;
  };

  // Create a new Readout object
  readout_t * readout_create(const char* address, const int port, const int command_port, const double source_frequency, int type){
    const std::string string_address(address);
    const auto r_ptr = static_cast<readout_t *>(malloc(sizeof(readout_t)));
    r_ptr->obj = new Readout(string_address, port, command_port, type, efu_time(1/source_frequency), efu_time());
    return r_ptr;
  }

  // Destroy an existing Readout object
  void readout_destroy(readout_t* r_ptr){
    if (r_ptr == nullptr) return;
    delete static_cast<Readout*>(r_ptr->obj);
    free(r_ptr);
  }

  // Add a readout value to the transmission buffer of the Readout object
  // Automatically transmits the packet if it is full.
  void readout_add(readout_t * r_ptr, const uint8_t ring, const uint8_t fen, const double time_of_flight, const double weight, const void * data){
    readout_setPulseTime(r_ptr);
    if (r_ptr == nullptr) return;
    const auto obj = static_cast<Readout *>(r_ptr->obj);
    obj->addReadout(ring, fen, time_of_flight, weight, data);
  }

  // Send the current data buffer for the Readout object
  void readout_send(readout_t* r_ptr)
  {
    Readout* obj;
    if (r_ptr == nullptr) return;
    obj = static_cast<Readout*>(r_ptr->obj);
    obj->send();
  }
  // Update the pulse and previous pulse times for the Readout object
  void readout_setPulseTime(readout_t* r_ptr)
  {
    if (r_ptr == nullptr) return;
    const auto obj = static_cast<Readout*>(r_ptr->obj);
    obj->update_time();
  }

  // Initialize a new packet with no readouts for the Readout object
  void readout_newPacket(readout_t* r_ptr)
  {
    Readout* obj;
    if (r_ptr == nullptr) return;
    obj = static_cast<Readout*>(r_ptr->obj);
    obj->newPacket();
  }

  int readout_shutdown(readout_t* r_ptr)
  {
    Readout* obj;
    if (r_ptr == nullptr) return 0;
    obj = static_cast<Readout*>(r_ptr->obj);
    return obj->command_shutdown();
  }

  // Set the verbose level of the readout sender to emit nothing to standard output
  int readout_silent(readout_t* r_ptr){
    Readout* obj;
    if (r_ptr == nullptr) return 0;
    obj = static_cast<Readout*>(r_ptr->obj);
    return obj->verbose(Verbosity::silent);
  }
  // Set the verbose level of the readout sender to emit extra error messages to standard output
  int readout_print_errors(readout_t* r_ptr){
    Readout* obj;
    if (r_ptr == nullptr) return 0;
    obj = static_cast<Readout*>(r_ptr->obj);
    return obj->verbose(Verbosity::errors);
  }
  // Set the verbose level of the readout sender to emit warnings and extra error messages to standard output
  int readout_print_warnings(readout_t* r_ptr){
    Readout* obj;
    if (r_ptr == nullptr) return 0;
    obj = static_cast<Readout*>(r_ptr->obj);
    return obj->verbose(Verbosity::warnings);
  }
  // Set the verbose level of the readout sender to emit info, warnings and extra error messages to standard output
  int readout_print_info(readout_t* r_ptr){
    Readout* obj;
    if (r_ptr == nullptr) return 0;
    obj = static_cast<Readout*>(r_ptr->obj);
    return obj->verbose(Verbosity::info);
  }
  // Set the verbose level of the readout sender to emit extra detail messages to standard output
  int readout_print_details(readout_t* r_ptr){
    Readout* obj;
    if (r_ptr == nullptr) return 0;
    obj = static_cast<Readout*>(r_ptr->obj);
    return obj->verbose(Verbosity::details);
  }

  // Set the verbose level from an integer -- look at ReadoutClass.h
  int readout_verbose(readout_t* r_ptr, int v){
    Readout* obj;
    if (r_ptr == nullptr) return 0;
    obj = static_cast<Readout*>(r_ptr->obj);
    return obj->verbose(v);
  }

  // Control file output for the Readout object
  void readout_dump_to(readout_t * r_ptr, const char * filename){
    Readout * obj;
    if (r_ptr == nullptr || filename == nullptr || filename[0] == '\0') return;
    obj = static_cast<Readout*>(r_ptr->obj);
    return obj->dump_to(filename);
  }

  // Allow disabling network communication
  void readout_disable_network(readout_t * r_ptr){
    Readout * obj;
    if (r_ptr == nullptr) return;
    obj = static_cast<Readout*>(r_ptr->obj);
    return obj->disable_network();
  }
void readout_enable_network(readout_t * r_ptr){
  Readout * obj;
  if (r_ptr == nullptr) return;
  obj = static_cast<Readout*>(r_ptr->obj);
  return obj->enable_network();
}

void readout_rand_seed01(readout_t * r_ptr, const double seed){
  Readout * obj;
  if (r_ptr == nullptr) return;
  obj = static_cast<Readout*>(r_ptr->obj);
  return obj->set_random_seed(static_cast<uint32_t>(seed * UINT32_MAX));
}
void readout_rand_seed(readout_t * r_ptr, const uint32_t seed){
  Readout * obj;
  if (r_ptr == nullptr) return;
  obj = static_cast<Readout*>(r_ptr->obj);
  return obj->set_random_seed(seed);
}
int readout_rand_poisson(readout_t * r_ptr, const double mean){
  Readout * obj;
  if (r_ptr == nullptr) return 0;
  obj = static_cast<Readout*>(r_ptr->obj);
  return obj->random_poisson(mean);
}


  // 
#ifdef __cplusplus
}
#endif
