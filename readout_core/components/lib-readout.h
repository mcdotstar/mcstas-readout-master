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
#include <stdlib.h>
#include <stdint.h>

#include <Readout.h>

void lib_readout_error(const char * comp_type, const char* named, const char * message, const char* variable);

void readout_caen_error(const char * named, const char * variable);
void collector_error(const char * named, const char * variable);
void collector_chopper_error(const char * named, const char * message);

void readout_particle_check(const char * comp_type, const char * comp_name, _class_particle* p, int present, char * name);

int readout_particle_getvar_int(_class_particle* p, char * name);

void collector_sink_parameters(char * named);

/* ---------------------------------------------------------------------------
 * Record sinks for the Collector* components
 *
 * A sink receives the records a component collected on every MPI node, gathers
 * them on the master and stores them either
 *   - READOUT_SINK_HDF5:  in a cue-based collector HDF5 file (CollectorSink), or
 *   - READOUT_SINK_NEXUS: in the McStas NeXus output file of the run
 *                         (--format=NeXus), one typed column per record field
 *                         under entryN/instrument/components/<NNNN_name>/<group>.
 * In both cases the component also emits a 0D monitor (N, I, E) so that it
 * appears in mccode.sim and mcplot like any other McStas monitor.
 *
 * Record weights follow the collector convention: weight = p * N_total, where
 * N_total is the total number of simulated rays (all MPI nodes), and the stored
 * normalization is N_total; weight / normalization is the rate in the units of p.
 * ------------------------------------------------------------------------- */
#define READOUT_SINK_HDF5  0
#define READOUT_SINK_NEXUS 1
#define READOUT_SINK_MAX_FIELDS 32

typedef struct readout_sink {
  int kind;                   /* READOUT_SINK_HDF5 or READOUT_SINK_NEXUS */
  collector_t * collector;    /* HDF5 sink only, master node only */
  char * description;         /* record layout */
  size_t record_size;
  int ess_type;
  uint64_t normalization;     /* N_total */
  readout_field_t fields[READOUT_SINK_MAX_FIELDS];
  int nfields;
  int weight_field;           /* index of the "weight" field, -1 if none */
  char group[256];            /* collector group / NeXus group name */
  char component[256];        /* component instance name */
  char nexuscomp[300];        /* NeXus component group name, NNNN_name */
  int index;                  /* component index (INDEX_CURRENT_COMP) */
  Coords position;
  Rotation rotation;
  char * efu_address;
  int efu_port;
  int verbose;
  uint64_t written;           /* records stored so far (master) */
} readout_sink_t;

/* Resolve a sink option ("auto", "nexus", "hdf5") to READOUT_SINK_HDF5 or READOUT_SINK_NEXUS. */
int readout_sink_kind(const char * sink, char * component);

/* Create a sink (all MPI nodes call this, in INITIALIZE).
 *   sink:        "auto" (NeXus when the run uses --format=NeXus, else HDF5), "nexus" or "hdf5"
 *   filename:    collector HDF5 file (HDF5 sink only), already passed through collector_construct_filename
 *   group:       collector group / NeXus group name
 *   description: record layout, e.g. readout_description_for(ess_type)
 *   record_size: sizeof the component's record struct, checked against the description */
readout_sink_t * readout_sink_new(const char * sink, const char * filename, const char * group,
                                  const char * description, int ess_type, uint64_t normalization,
                                  size_t record_size, const char * efu_address, int efu_port,
                                  char * component, int index, Coords position, Rotation rotation,
                                  int verbose);

/* Gather the records of array (all nodes) on the master and store them; clears array.
 * All MPI nodes must call this, in SAVE. */
void readout_sink_save(readout_sink_t * sink, array_t * array);

/* Release the sink (FINALLY). */
void readout_sink_free(readout_sink_t * sink);

#endif //MCCODE_LIB_READOUT_H
