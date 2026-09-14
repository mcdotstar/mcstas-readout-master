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
void readout_ttlmonitor_error(const char * named, const char * variable);
void collector_error(const char * named, const char * variable);
void collector_chopper_error(const char * named, const char * message);

void readout_particle_check(const char * comp_type, const char * comp_name, _class_particle* p, int present, char * name);

int readout_particle_getvar_int(_class_particle* p, char * name);

void collector_sink_parameters(char * named);

#endif //MCCODE_LIB_READOUT_H
