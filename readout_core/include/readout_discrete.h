#ifndef DISCRETER_DISCRETER_C_H
#define DISCRETER_DISCRETER_C_H

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

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

  struct discreter_object;
  typedef struct discreter_object discreter_t;

struct array_object;
typedef struct array_object array_t;

struct index_sampler_object;
typedef struct index_sampler_object index_sampler_t;



/// Return the library version string (e.g. "0.1.0")
RL_API const char *discreter_version(void);

/// Return the library version as a packed integer (major*10000 + minor*100 + patch)
RL_API int discreter_version_int(void);

/// Create a new DiscreteWhile object with the given description, name, number of samples, and random seed
RL_API discreter_t * dr_new_desc(const char * description, const char * name, size_t samples, uint32_t seed);
/// Create a new DiscreteWhile object with the given object size, name, number of samples, and random seed
RL_API discreter_t * dr_new_size(size_t object_bytes, const char * name, size_t samples, uint32_t seed);
/// Create a new DiscreteWhile object with the given description, object size, name, number of samples, and random seed
RL_API discreter_t * dr_new(const char * description, size_t object_bytes, const char * name, size_t samples, uint32_t seed);

/// Destroy a DiscreteWhile or DiscreteAfter object
RL_API void dr_free(discreter_t *obj);

/// Add an object to the DiscreteWhile or DiscreteAfter collection by copying object_size bytes from src with the given weight
RL_API void dr_fit(const discreter_t *obj, const void * src, double weight);

/// Check if the sampler is still in the filling phase (i.e. has not yet collected enough samples to produce a valid sample). Returns 1 if still filling, 0 if enough samples have been collected.
RL_API int dr_filling(const discreter_t *obj);

/// Sample a fraction of the collected data from a DiscreteAfter object. Must be called before dr_value() is called. Returns the number of samples drawn.
RL_API size_t dr_sample(const discreter_t *obj, double fraction, uint32_t seed);

/// Oversample from a DiscreteAfter object. Only useful for avoiding default-constructed samples when the sampler is still filling. Should be called after dr_fit() and before dr_value(). Does nothing for DiscreteWhile objects.
RL_API void dr_oversample(const discreter_t *obj);

/// Get the sampled values from a DiscreteWhile or DiscreteAfter object. Caller must provide a buffer of at least object_size * sample_count bytes, where sample_count is the number of samples drawn (for DiscreteAfter) or the number of samples specified at creation (for DiscreteWhile).
/// Returns the number of bytes written to the buffer (object_size * sample_count).
RL_API size_t dr_value(const discreter_t *obj, void * buffer, size_t buffer_size);

/// Get a specified sampled value from a DiscreteWhile or DiscreteAfter object by index. Caller must provide a buffer of at least object_size bytes. Returns the number of bytes written to the buffer (object_size).
RL_API size_t dr_value_index(const discreter_t *obj, size_t index, void * buffer, size_t buffer_size);
  
  
  // // CollectorStar functions
  //
  // /** \brief Create a CollectorStar from a C struct type description string.
  //  *
  //  * The type description should be valid C struct syntax, e.g.:
  //  *   "struct { int count; double energy; uint16_t values[10]; }"
  //  * The object size is computed automatically from the parsed description.
  //  *
  //  * \param type_description  C struct syntax string describing the data type
  //  * \param dataset_name      Name for the HDF5 dataset (must not be NULL or empty)
  //  * \returns Pointer to the new collector, or NULL on parse error (message printed to stderr)
  //  */
  // RL_API collector_t* cs_new_desc(const char* type_description, const char* dataset_name);
  //
  // /** \brief Create a CollectorStar in opaque mode (no type schema).
  //  *
  //  * Data is stored as raw bytes. HDF5 output will be a 2D uint8 dataset.
  //  *
  //  * \param object_size  Size of each object in bytes (e.g. sizeof(struct my_struct))
  //  * \param dataset_name Name for the HDF5 dataset
  //  * \returns Pointer to the new collector, or NULL on error
  //  */
  // RL_API collector_t* cs_new_size(size_t object_size, const char* dataset_name);
  //
  // /** \brief Create a CollectorStar with both description and size validation.
  //  *
  //  * Parses the type description and verifies that the computed size matches
  //  * the user-provided object_size. Returns NULL and prints an error if they disagree.
  //  *
  //  * \param type_description  C struct syntax string
  //  * \param object_size       Expected size in bytes (e.g. sizeof(struct my_struct))
  //  * \param dataset_name      Name for the HDF5 dataset
  //  * \returns Pointer to the new collector, or NULL on error
  //  */
  // RL_API collector_t* cs_new(const char* type_description, size_t object_size, const char* dataset_name);
  //
  // /** \brief Destroy a CollectorStar and free its resources */
  // RL_API void cs_free(collector_t* cs);
  //
  // /** \brief Add an object to the collector by copying object_size bytes from src */
  // RL_API void cs_add(const collector_t* cs, const void* src);
  //
  // /** \brief Copy the object at the given index into dst (caller provides buffer of object_size bytes) */
  // RL_API int cs_get(const collector_t* cs, size_t index, void* dst);
  //
  // /** \brief Return the number of collected objects */
  // RL_API size_t cs_count(const collector_t* cs);
  //
  // /** \brief Return the size of each object in bytes */
  // RL_API size_t cs_object_size(const collector_t* cs);
  //
  // /** \brief Write all collected data to an HDF5 file */
  // RL_API int cs_write_hdf5(const collector_t* cs, const char* filename);


RL_API array_t * new_array(size_t bytes_per_object);
RL_API void delete_array(array_t * array);
RL_API size_t array_size(const array_t * array);
RL_API void array_add(const array_t * array, const void * data);
RL_API void array_get(const array_t * array, size_t index, void * dst);
RL_API void array_clear(const array_t * array);
RL_API const uint8_t * array_data(const array_t * array);

RL_API index_sampler_t * new_index_sampler(size_t samples, uint32_t seed);
RL_API void delete_index_sampler(index_sampler_t * sampler);
RL_API void index_sampler_fit(const index_sampler_t * sampler, size_t index, double weight);
RL_API int index_sampler_filling(const index_sampler_t * sampler);
RL_API void index_sampler_values(const index_sampler_t * sampler, size_t * values);
RL_API size_t index_sampler_value(const index_sampler_t * sampler, size_t index);

#ifdef __cplusplus
}
#endif

#endif // DISCRETER_DISCRETER_C_H
