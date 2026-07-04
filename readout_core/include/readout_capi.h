/** \file readout_capi.h
 * \brief C API for driving replay and combine operations from other languages.
 *
 * A minimal `extern "C"` surface over replay() (replay.h) and the combine
 * functions (CollectorClass.h), designed for foreign-function interfaces such
 * as Python ctypes. Replay configuration is held in an opaque handle mutated
 * through setters, so new ReplayConfig fields become additive functions rather
 * than struct-layout (ABI) breaks. No exception ever crosses this boundary:
 * failures return READOUT_ERROR and describe themselves via readout_last_error().
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

/// \cond EXPORT_MACRO
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
/// \endcond

#ifdef __cplusplus
extern "C" {
#endif

/** Incremented whenever any signature in this header changes; foreign wrappers
 *  compare it against the value they were written for before calling anything else. */
#define READOUT_CAPI_ABI_VERSION 1

/// The READOUT_CAPI_ABI_VERSION the library was compiled with.
RL_API int readout_capi_abi_version(void);

/// The library version string, e.g. "0.4.0".
RL_API const char * readout_version(void);

/** \brief Message describing the most recent failure on the calling thread; never NULL.
 *
 * Cleared on entry to every C API function, so it is only meaningful
 * immediately after a call returned READOUT_ERROR.
 */
RL_API const char * readout_last_error(void);

/// Status codes returned by the C API entry points.
enum readout_status {
  READOUT_OK = 0,       ///< completed successfully
  READOUT_STOPPED = 1,  ///< replay stopped early via request_stop or a nonzero callback return
  READOUT_ERROR = -1    ///< failed; see readout_last_error()
};

/* ---- replay --------------------------------------------------------------- */

/// Opaque replay handle: a ReplayConfig plus a thread-safe stop flag.
typedef struct readout_replay readout_replay_t;

/// Create a replay handle with default configuration; NULL on allocation failure.
RL_API readout_replay_t * readout_replay_create(void);
/// Destroy a replay handle; must not be called while readout_replay_run is blocking on it.
RL_API void readout_replay_destroy(readout_replay_t * handle);

/* Configuration setters mirroring ReplayConfig (replay.h). Each returns
 * READOUT_OK or READOUT_ERROR (NULL handle, invalid value, or bad JSON). */

/// Counting time in seconds: a stored readout with rate-weight w is sent n ~ Poisson(w * seconds) times.
RL_API int readout_replay_set_counting_time(readout_replay_t * handle, double seconds);
/// Unset the counting time so every stored readout is sent exactly once (the default).
RL_API int readout_replay_clear_counting_time(readout_replay_t * handle);
/// Sampling/shuffling generator seed; 0 (the default) selects a non-deterministic seed.
RL_API int readout_replay_set_seed(readout_replay_t * handle, uint32_t seed);
/// Nonzero shuffles events within each (point, collector group) before sending.
RL_API int readout_replay_set_random_order(readout_replay_t * handle, int enable);
/// EFU address and UDP port for collector groups without explicit or file-embedded routing.
RL_API int readout_replay_set_default_endpoint(readout_replay_t * handle, const char * address, int port);
/// Number of stored readouts per HDF5 read (default 65536); also the mid-point cancellation granularity.
RL_API int readout_replay_set_chunk_size(readout_replay_t * handle, uint64_t chunk);
/// Restrict replay to stored readouts with global index first + k * every, for k in [0, number).
RL_API int readout_replay_set_subset(readout_replay_t * handle, uint64_t first, uint64_t number, uint64_t every);
/// Remove a previously set subset restriction.
RL_API int readout_replay_clear_subset(readout_replay_t * handle);
/// Pulse (reference time) repetition rate in Hz (default 14, the ESS source frequency).
RL_API int readout_replay_set_pulse_rate(readout_replay_t * handle, double hz);
/// Nonzero stamps events at pulse + (tof %% period), wrapping long-flight events into their detection frame.
RL_API int readout_replay_set_fold_tof(readout_replay_t * handle, int enable);
/// Explicit EFU routing as a JSON document (see SenderConfigs); READOUT_ERROR with a parse message on invalid input.
RL_API int readout_replay_set_senders_json(readout_replay_t * handle, const char * json_text);

/** \brief Per-parameter publisher callback (ParameterPublisher::publish).
 *
 * unit_or_null is NULL when the parameter has no unit.
 * Return 0 to continue; any nonzero value stops the replay cleanly
 * (readout_replay_run returns READOUT_STOPPED and none of the current
 * point's events are sent).
 */
typedef int (*readout_publish_cb)(void * user_data, uint64_t point,
                                  const char * name, const char * value,
                                  const char * unit_or_null);
/// Per-point publisher callback (ParameterPublisher::point_ready); same return convention.
typedef int (*readout_point_ready_cb)(void * user_data, uint64_t point);

/** \brief Replay a collector file; blocking.
 *
 * Callbacks may be NULL (the corresponding notification is skipped); they run
 * on the calling thread, before the point's events are sent, and may block.
 * Returns READOUT_OK, READOUT_STOPPED, or READOUT_ERROR.
 */
RL_API int readout_replay_run(readout_replay_t * handle, const char * filename,
                              readout_publish_cb publish,
                              readout_point_ready_cb point_ready,
                              void * user_data);

/** \brief Request that a running replay stop at the next point or chunk boundary.
 *
 * Thread-safe: callable from any thread while readout_replay_run blocks.
 * Sticky — subsequent runs on the handle return READOUT_STOPPED immediately
 * until readout_replay_reset_stop is called.
 */
RL_API void readout_replay_request_stop(readout_replay_t * handle);
/// 1 when a stop has been requested on the handle, else 0.
RL_API int readout_replay_stop_requested(const readout_replay_t * handle);
/// Clear a previous stop request so the handle can be reused.
RL_API void readout_replay_reset_stop(readout_replay_t * handle);

/* ---- combine --------------------------------------------------------------- */

/** \brief Validate a collector file.
 * \return the number of scan points (>= 0), or READOUT_ERROR when the file is
 *         missing or not a valid collector file (see readout_last_error()).
 */
RL_API int64_t readout_validate_collector_file(const char * filename);

/** \brief Combine same-point collector files (identical parameters) by appending readouts.
 * \param out_filename the output file to create (must not already exist)
 * \param in_filenames array of input collector filenames
 * \param n_in the number of input filenames
 * \param reset_datasets nonzero zeroes the readout/cue/weight datasets in the input files after combining
 * \return READOUT_OK or READOUT_ERROR (diagnostic detail may also appear on stderr).
 */
RL_API int readout_append_collector_files(const char * out_filename,
                                          const char * const * in_filenames, size_t n_in,
                                          int reset_datasets);

/** \brief Combine different-point collector files into one multi-point cue-based file.
 * \param out_filename the output file to create (must not already exist)
 * \param in_filenames array of input collector filenames
 * \param n_in the number of input filenames
 * \return READOUT_OK or READOUT_ERROR (diagnostic detail may also appear on stderr).
 */
RL_API int readout_concatenate_collector_files(const char * out_filename,
                                               const char * const * in_filenames, size_t n_in);

/** \brief Combine collector files, choosing append or concatenate per their parameters.
 * \param out_filename the output file to create (must not already exist)
 * \param in_filenames array of input collector filenames
 * \param n_in the number of input filenames
 * \return READOUT_OK or READOUT_ERROR (diagnostic detail may also appear on stderr).
 */
RL_API int readout_combine_collector_files(const char * out_filename,
                                           const char * const * in_filenames, size_t n_in);

#ifdef __cplusplus
} // extern "C"
#endif
