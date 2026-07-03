#ifndef READOUT_WRAPPER
#define READOUT_WRAPPER

#include <stddef.h>
#include "readout_structs.h"

#ifdef WIN32
// Export symbols if compile flags "READOUT_SHARED" and "READOUT_EXPORT" are set on Windows.
    #ifdef READOUT_SHARED
        #ifdef READOUT_EXPORT
            #define RL_API __declspec(dllexport)
        #else
            #define RL_API __declspec(dllimport)
        #endif
    #else
        // Disable definition if linking statically.
        #define RL_API
    #endif
#else
// Disable definition for non-Win32 systems.
#define RL_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct readout;
typedef struct readout readout_t;

// Create a new Readout object
// type == 0x34 for BIFROST, 0x41 for He3CSPEC
RL_API readout_t * readout_create(const char* address, int port, int command_port, double source_frequency, int type);

// Destroy an existing Readout object
RL_API void readout_destroy(readout_t* r_ptr);

// Add a readout value to the transmission buffer of the Readout object
// Automatically transmits the packet if it is full.
RL_API void readout_add(readout_t* r_ptr, uint8_t ring, uint8_t fen, double time_of_flight, double weight, const void* data);
// Send the current data buffer for the Readout object
RL_API void readout_send(readout_t* r_ptr);
// Update the pulse and previous pulse times for the Readout object
RL_API void readout_setPulseTime(readout_t* r_ptr);

// Initialize a new packet with no readouts for the Readout object
RL_API void readout_newPacket(readout_t* r_ptr);

// Send the command-port of the Event Formation Unit the exit signal
RL_API int readout_shutdown(readout_t* r_ptr);

// Set the verbose level of the readout sender to emit nothing to standard output
RL_API int readout_silent(readout_t* r_ptr);
// Set the verbose level of the readout sender to emit extra error messages to standard output
RL_API int readout_print_errors(readout_t* r_ptr);
// Set the verbose level of the readout sender to emit warnings and extra error messages to standard output
RL_API int readout_print_warnings(readout_t* r_ptr);
// Set the verbose level of the readout sender to emit info, warnings and extra error messages to standard output
RL_API int readout_print_info(readout_t* r_ptr);
// Set the verbose level of the readout sender to emit extra detail messages to standard output
RL_API int readout_print_details(readout_t* r_ptr);
// Set the verbose level from an integer -- look at ReadoutClass.h
RL_API int readout_verbose(readout_t* r_ptr, int);

// Control file output for the Readout object
RL_API void readout_dump_to(readout_t * r_ptr, const char * filename);

// Combine multiple files into one for the Readout object -- each should have come from an equivalent Readout object
RL_API void readout_merge_files(const char * out_filename, const char ** in_filenames, size_t count);

// Allow disabling and enabling network communication (on by default)
RL_API void readout_disable_network(readout_t * r_ptr);
RL_API void readout_enable_network(readout_t * r_ptr);

// Set the random seed for the readout random object
RL_API void readout_rand_seed01(readout_t * r_ptr, double seed);
RL_API void readout_rand_seed(readout_t * r_ptr, uint32_t seed);
// Convert a probability to a discrete number of events
RL_API int readout_rand_poisson(readout_t * r_ptr, double mean);

#ifdef __cplusplus
}
#endif

#endif
