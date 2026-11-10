#ifndef MCCODE_LIB_READOUT_H
#define MCCODE_LIB_READOUT_H

#ifndef MCSTAS
struct _struct_particle;
typedef struct _struct_particle _class_particle;

double particle_getvar(_class_particle* p, const char* name, int* signal);
void * particle_getvar_void(_class_particle* p, const char* name, int* signal);
#endif

/* Only the C standard library is needed here; keep this header free of POSIX
 * headers (unistd.h, sys/time.h, ...) so the generated instrument compiles
 * with MSVC on Windows as well as with GCC/Clang. */
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <Readout.h>

void lib_readout_error(const char * comp_type, const char* named, const char * message, const char* variable);

void readout_caen_error(const char * named, const char * variable);
void collector_error(const char * named, const char * variable);
void collector_chopper_error(const char * named, const char * message);

void readout_particle_check(const char * comp_type, const char * comp_name, _class_particle* p, int present, char * name);

int readout_particle_getvar_int(_class_particle* p, char * name);

void collector_sink_parameters(char * named);

/* Shared by the Collector* components.
 *
 * collector_shared_new (all MPI nodes, INITIALIZE) creates the description-based Collector
 * for ess_type on the master node, checks it against the component's record_size, attaches
 * the optional EFU routing and, for the first collector to open filename, records the
 * instrument parameters. It returns NULL on the other nodes.
 *
 * collector_shared_save (all MPI nodes, SAVE) gathers every node's records on the master,
 * adds them to the Collector with the weight found weight_offset bytes into each record,
 * and clears array. */
collector_t * collector_shared_new(char * component, const char * filename, const char * dataset,
                                   int ess_type, uint64_t normalization, size_t record_size,
                                   const char * efu_address, int efu_port);
void collector_shared_save(const collector_t * collector, array_t * array, size_t record_size,
                           size_t weight_offset);

#endif //MCCODE_LIB_READOUT_H
