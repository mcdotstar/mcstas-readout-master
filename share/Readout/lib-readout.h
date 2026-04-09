#ifndef MCCODE_LIB_READOUT_H
#define MCCODE_LIB_READOUT_H

#include <signal.h>
#include <unistd.h> // for execve
#include <sys/time.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#include <readout_collector.h>

void collector_error(char * named, char * variable);

int collector_particle_getvar_int(_class_particle* p, char * name);

void collector_sink_parameters(char * named);

void collector_merge_mpi(const char * filename, int point, int total_points, const char * dataset, int reset_datasets, int remove_input_files);


#endif //MCCODE_LIB_READOUT_H