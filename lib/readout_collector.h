#ifndef COLLECTOR_WRAPPER
#define COLLECTOR_WRAPPER

#include "readout_structs.h"

#ifdef __cplusplus
extern "C" {
#include <cstddef>
#include <cstdint>
#endif

  struct collector;
  typedef struct collector collector_t;

  /** \brief Create a new Collector object for writing readout data to an HDF5 file.
   *
   * \param filename The name of the HDF5 file to write to. If the file already exists, it will be updated.
   * \param point The current scan point number -- defines the group name for the datasets.
   *              If total_points is 0, this is ignored and all data is written to the root group.
   * \param points The total number of scan points -- defines the group name for the datasets.
   *               If 0, this is ignored and all data is written to the root group.
   * \param dataset The name of the dataset to write to. If nullptr or empty, defaults to "events".
   * \param type The type of readout data to write, defined as an integer corresponding to the ReadoutType enum
   *             in Readout.h (e.g. 0x34 for BIFROST, 0x41 for He3CSPEC).
   *             This is used to determine the structure of the readout data and the name of the compound datatype
   *             created in the HDF5 file. If an unrecognized type is given, the compound datatype will still be
   *             created but the structure of the readout data will not be validated when adding readouts.
   */
  RL_API collector_t* collector_new(const char* filename, int point, int points, const char * dataset, int type);

  ///\brief Destroy an existing Collector object and free its resources
  RL_API void collector_free(collector_t* c_ptr);

  RL_API int collector_sink_open(const char * filename);
  RL_API int collector_sink_users(const char * filename);

  /** \brief Add a readout with time and weight information to the collector's storage.
   * \param c_ptr Pointer to the Collector object
   * \param ring The ring number of the readout (0-255)
   * \param fen The FEN number of the readout (0-255)
   * \param tof The time of flight value for the readout
   * \param weight The weight value for the readout (e.g. for weighted events)
   * \param data Pointer to the readout data structure, which should match the type specified when creating
   *             the Collector object. The structure of the data will be determined by the ReadoutType enum value given
   *             at creation, and should correspond to one of the defined readout structures
   *             (e.g. CAEN_readout_t, TTLMonitor_readout_t, CDT_readout_t, VMM3_readout_t).
   *             If the type of the data does not match the expected structure for the Collector's readout type,
   *             the behavior is undefined (it may cause a crash or it may write incorrect data to the file).
   *             It is the responsibility of the caller to ensure that the data pointer is valid and points to a
   *             structure of the correct type. The Collector will attempt to save the readout data to the appropriate
   *             dataset in the HDF5 file, and will accumulate the weight for the corresponding ring/FEN combination
   *             if separate datasets are being used. If no separate datasets are used, all weights will be accumulated
   *             in a default weight attribute for the single dataset. The Collector does not perform any validation on
   *             the readout data structure, so it is important to ensure that the data is correctly formatted according
   *             to the  ReadoutType specified at creation.
   **/
  RL_API void collector_add(const collector_t* c_ptr, uint8_t ring, uint8_t fen, double tof, double weight, const void* data);

  /** \brief Combine multiple files into one for the Collector object -- each should have come from an equivalent object
   *
   * \param out_filename The name of the output HDF5 file to open or create.
   * \param in_filenames An array of strings containing the names of the input HDF5 files to merge.
   *                     Each file should have been produced by a Collector object with the same dataset structure.
   * \param count The number of input filenames
   * \param point The current scan point number -- defines the group name for the datasets to be merged
   * \param total_points The total number of scan points -- defines the group name for the datasets to be merged
   *
   * This function will merge all equivalent datasets in the collector group specified by (point, total_points)
   * across the input files into a single dataset in the output file.
   * The datasets are merged by concatenating the events along the first dimension, and the weights are accumulated
   * as attributes for each ring/FEN combination if separate datasets are used.
   * The function will validate that the input files have compatible structures and will skip any files that
   * do not match the expected format.
   * If no valid input files are found, the function will print an error message and return without creating an output file.
   *
   * Since this merges all datasets, it may be dangerous to call this function from within a component
   * runtime; as every component would merge their own and other component's datasets you may duplicate data.
   * Instead, you should use the dataset-specific merge.
   */
  RL_API void collector_merge_files(const char * out_filename, const char ** in_filenames, size_t count, int point, int total_points);

  RL_API void collector_merge(const char * out_filename, const char ** in_filenames, size_t count, int point, int total_points, const char * dataset, int reset_datasets);

  /** \brief Add a parameter value to the HDF5 file as a dataset.
   * \param name The name of the parameter to add (e.g. "temperature", "pressure", "scan_angle")
   * \param value The value of the parameter to add. This can be an integer, double, or string value, and will be saved with the appropriate datatype in the HDF5 file.
   * \param unit An optional string specifying the unit of the parameter (e.g. "K" for temperature, "Pa" for pressure, "degrees" for scan angle). If nullptr or empty, no unit attribute will be added.
   * \param description An optional string providing a description of the parameter. If nullptr or empty, no description attribute will be added.
   *
   * This function allows adding arbitrary parameters to the HDF5 file as datasets in a group.
   * The parameters can be used to store additional information about the experimental conditions, scan settings,
   * or any other relevant metadata that should be associated with the readout data.
   * The parameters are stored as attributes in the HDF5 file, and can be easily accessed when analyzing the data
   * later on. It is important to ensure that the name of the parameter is unique and descriptive enough to avoid
   * confusion when analyzing the data. The unit and description fields are optional but can provide valuable context
   * for understanding the meaning of the parameter values when analyzing the data.
   **/
  RL_API void collector_sink_int(const char* name, int value, const char* unit, const char* description);
  RL_API void collector_sink_double(const char* name, double value, const char* unit, const char* description);
  RL_API void collector_sink_string(const char* name, const char* value, const char* unit, const char* description);

	RL_API int collector_construct_filename_size(const char * basepath, const char * basename);
  RL_API int collector_construct_filename(const char * basepath, const char * basename, char * filename);
	RL_API int collector_mpi_node_filename_size(const char * basepath, const char * basename, int node_index, int total_nodes);
  RL_API int collector_mpi_node_filename(const char * basepath, const char * basename, char * filename, int node_index, int total_nodes);
	RL_API int collector_mpi_node_filename_sizes(const char * basepath, const char * basename, int total_nodes, int * sizes);
  RL_API int collector_mpi_node_filenames(const char * basepath, const char * basename, char ** filenames, int total_nodes);

  // ---- CollectorStar: generic runtime-typed collector ----

  struct collector_star;
  typedef struct collector_star collector_star_t;

  /** \brief Create a CollectorStar from a C struct type description string.
   *
   * The type description should be valid C struct syntax, e.g.:
   *   "struct { int count; double energy; uint16_t values[10]; }"
   * The object size is computed automatically from the parsed description.
   *
   * \param type_description  C struct syntax string describing the data type
   * \param dataset_name      Name for the HDF5 dataset (must not be NULL or empty)
   * \returns Pointer to the new collector, or NULL on parse error (message printed to stderr)
   */
  RL_API collector_star_t* collector_star_new_description(const char* type_description, const char* dataset_name);

  /** \brief Create a CollectorStar in opaque mode (no type schema).
   *
   * Data is stored as raw bytes. HDF5 output will be a 2D uint8 dataset.
   *
   * \param object_size  Size of each object in bytes (e.g. sizeof(struct my_struct))
   * \param dataset_name Name for the HDF5 dataset
   * \returns Pointer to the new collector, or NULL on error
   */
  RL_API collector_star_t* collector_star_new_opaque(size_t object_size, const char* dataset_name);

  /** \brief Create a CollectorStar with both description and size validation.
   *
   * Parses the type description and verifies that the computed size matches
   * the user-provided object_size. Returns NULL and prints an error if they disagree.
   *
   * \param type_description  C struct syntax string
   * \param object_size       Expected size in bytes (e.g. sizeof(struct my_struct))
   * \param dataset_name      Name for the HDF5 dataset
   * \returns Pointer to the new collector, or NULL on error
   */
  RL_API collector_star_t* collector_star_new_validated(const char* type_description, size_t object_size, const char* dataset_name);

  /** \brief Destroy a CollectorStar and free its resources */
  RL_API void collector_star_free(collector_star_t* cs);

  /** \brief Add an object to the collector by copying object_size bytes from src */
  RL_API void collector_star_add(collector_star_t* cs, const void* src);

  /** \brief Copy the object at the given index into dst (caller provides buffer of object_size bytes) */
  RL_API int collector_star_get(const collector_star_t* cs, size_t index, void* dst);

  /** \brief Return the number of collected objects */
  RL_API size_t collector_star_count(const collector_star_t* cs);

  /** \brief Return the size of each object in bytes */
  RL_API size_t collector_star_object_size(const collector_star_t* cs);

  /** \brief Write all collected data to an HDF5 file */
  RL_API int collector_star_write_hdf5(const collector_star_t* cs, const char* filename);

#ifdef __cplusplus
}
#endif

#endif
