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

void readout_ttlmonitor_error(const char * named, const char * variable){
  lib_readout_error("ReadoutTTLMonitor", named, "Unknown particle variable", variable);
}


void collector_error(const char * named, const char * variable){
  lib_readout_error("CollectorSink", named, "Unknown particle variable", variable);
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

void collector_merge_mpi(const char * filename, int point, int total_points, const char * dataset, int reset_datasets, int remove_input_files) {
#ifdef USE_MPI
  if (mpi_node_rank != mpi_node_root) {
    fprintf(stderr, "Error: collector_merge_mpi should only be called on the MPI root node.\n");
    return;
  }
  if (mpi_node_count > 1){
    char * basepath = mcfull_file("", NULL);
    char * output = (char *) malloc(collector_construct_filename_size(basepath, filename) * sizeof(char));
    collector_construct_filename(basepath, filename, output);

    char ** inputs = (char **) malloc(mpi_node_count * sizeof(char *));
    for (int i=0; i<mpi_node_count; i++){
      inputs[i] = (char *) malloc(collector_mpi_node_filename_size(basepath, filename, i, mpi_node_count) * sizeof(char));
    }
    collector_mpi_node_filenames(basepath, filename, mpi_node_count, inputs);

    collector_merge((const char *) output, (const char **) inputs, mpi_node_count, point, total_points, dataset, reset_dataset);

    if (remove_input_files){
      for (int i=0; i<mpi_node_count; i++){
        if (inputs[i] != NULL){
          if (unlink(inputs[i]) != 0){
            fprintf(stderr, "Warning: failed to remove input file %s after merging.\n", inputs[i]);
          }
        }
      }
    }

    free(basepath);
    free(output_filename);
    for (int i=0; i<mpi_node_count; i++){
      free(merge_filenames[i]);
    }
    free(merge_filenames);
  }
#endif
}
