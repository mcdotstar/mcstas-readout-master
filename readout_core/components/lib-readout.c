// Seemingly non-sensical header guard needed for McStas inclusion
#ifndef MCCODE_LIB_READOUT_H
#include "lib-readout.h"
#endif

void lib_readout_error(const char * comp_type, const char* named, const char * message, const char* variable){
  printf("%s(%s): %s %s, exiting.\n", comp_type, named, message, variable);
  exit(-1);
}

void readout_caen_error(const char * named, const char * variable){
  lib_readout_error("ReadoutCAEN", named, "Unknown particle variable", variable);
}

void collector_error(const char * named, const char * variable){
  lib_readout_error("CollectorSink", named, "Unknown particle variable", variable);
}

void collector_chopper_error(const char * named, const char * message){
  lib_readout_error("Collector*Chopper", named, message, "");
}


void readout_particle_check(const char * comp_type, const char * comp_name, _class_particle * p, const int present, char * name) {
  int failure=0;
  if (present){
    particle_getvar(p, name, &failure);
    if (failure) lib_readout_error(comp_type, comp_name, "No particle variable named", name);
  }
}


int readout_particle_getvar_int(_class_particle* p, char * name) {
  void * vval = particle_getvar_void(p, name, 0);
  return *(int*)vval;
}

void collector_sink_parameters(char * named) {
  for (int i=0; i<numipar; ++i){
    switch (mcinputtable[i].type){
      case instr_type_int:
        collector_sink_int(mcinputtable[i].name, *(int *)(mcinputtable[i].par), mcinputtable[i].unit, "");
        break;
      case instr_type_double:
        collector_sink_double(mcinputtable[i].name, *(double *)(mcinputtable[i].par), mcinputtable[i].unit, "");
        break;
      case instr_type_string:
        collector_sink_string(mcinputtable[i].name, *(char **)(mcinputtable[i].par), mcinputtable[i].unit, "");
        break;
      default:
        fprintf(stderr, "Warning(%s): Instrument parameter %s has unsupported type, not sending to CollectorSink.\n", named, mcinputtable[i].name);
    }
  }
}

collector_t * collector_shared_new(char * component, const char * filename, const char * dataset,
                                   int ess_type, uint64_t normalization, size_t record_size,
                                   const char * efu_address, int efu_port) {
  collector_t * collector = NULL;
  const char * description = readout_description_for(ess_type);
  if (description == NULL) {
    collector_error(component, "No canonical record description for the given ess_type.");
  }
#ifdef USE_MPI
  if (mpi_node_rank != mpi_node_root) return NULL;
#endif
  collector = collector_star_new(filename, dataset, description, ess_type, normalization);
  if (collector == NULL) {
    collector_error(component, "Could not create the description-based Collector instance.");
  }
  if (collector_record_size(collector) != record_size) {
    collector_error(component, "Record layout mismatch between component struct and canonical description.");
  }
  if (efu_address != NULL && efu_address[0] != '\0' && efu_port > 0) {
    collector_efu(collector, efu_address, efu_port);
  }
  /* several collectors may share one file: the first to open it records the parameters */
  if (collector_sink_users(filename) == 1) {
    collector_sink_parameters(component);
  }
  return collector;
}

void collector_shared_save(const collector_t * collector, array_t * array, size_t record_size,
                           size_t weight_offset) {
  const uint8_t * records = array_data(array);
  size_t count = array_size(array);
  uint8_t * gathered = NULL;
  int store = 1;
#ifdef USE_MPI
  MPI_Datatype record_type;
  int local_count = (int) count, global_count = 0;
  int * counts = NULL;
  int * displacements = NULL;
  store = mpi_node_rank == mpi_node_root;
  MPI_Type_contiguous((int) record_size, MPI_BYTE, &record_type);
  MPI_Type_commit(&record_type);
  if (store) {
    counts = (int *) calloc(mpi_node_count, sizeof(int));
    displacements = (int *) calloc(mpi_node_count, sizeof(int));
  }
  MPI_Gather(&local_count, 1, MPI_INT, counts, 1, MPI_INT, mpi_node_root, MPI_COMM_WORLD);
  if (store) {
    for (int i = 0; i < mpi_node_count; ++i) {
      displacements[i] = global_count;
      global_count += counts[i];
    }
    gathered = (uint8_t *) malloc((global_count > 0 ? global_count : 1) * record_size);
  }
  MPI_Gatherv((void *) records, local_count, record_type,
              gathered, counts, displacements, record_type, mpi_node_root, MPI_COMM_WORLD);
  MPI_Type_free(&record_type);
  free(counts);
  free(displacements);
  records = gathered;
  count = (size_t) global_count;
#endif
  if (store) {
    for (size_t i = 0; i < count; ++i) {
      const uint8_t * record = records + i * record_size;
      double weight;
      memcpy(&weight, record + weight_offset, sizeof weight);
      collector_star_add(collector, weight, (const void *) record);
    }
  }
  free(gathered);
  array_clear(array);
}
