#ifndef COLLECTOR_WRAPPER
#define COLLECTOR_WRAPPER

#include <stddef.h>
#include <stdint.h>
#include "readout_structs.h"

#ifdef __cplusplus
extern "C" {
#endif

  struct collector;
  typedef struct collector collector_t;

  /** \brief Create a new Collector object for writing readout data to an HDF5 file.
   *
   * \param filename The name of the HDF5 file to write to. If the file already exists, it will be updated.
   * \param dataset The name of the dataset to write to. If nullptr or empty, defaults to "events".
   * \param type The type of readout data to write, defined as an integer corresponding to the ReadoutType enum
   *             in Readout.h (e.g. 0x34 for BIFROST, 0x41 for He3CSPEC).
   *             This is used to determine the structure of the readout data and the name of the compound datatype
   *             created in the HDF5 file. If an unrecognized type is given, the compound datatype will still be
   *             created but the structure of the readout data will not be validated when adding readouts.
   * \param normalization The normalization constant associated with the to-be-provided readouts
   */
  RL_API collector_t* collector_new(const char* filename, const char * dataset, int type, uint64_t normalization);

  ///\brief Destroy an existing Collector object and free its resources
  RL_API void collector_free(collector_t* c_ptr);

  /** \brief Attach optional EFU destination attributes to a Collector group.
   *
   * Writes `efu_address` (string) and `efu_port` (int) attributes onto the HDF5 group
   * for the given collector so that replay can discover the EFU endpoint from the file
   * without requiring external configuration.  The call is a no-op if \p address is
   * NULL/empty or \p port is 0, and it never overwrites attributes that are already set.
   *
   * \param c_ptr  Pointer to the Collector object returned by collector_new()
   * \param address  IP address (or FQDN) of the EFU
   * \param port     UDP port of the EFU (must be > 0)
   */
  RL_API void collector_efu(collector_t* c_ptr, const char* address, int port);

  /** \brief Create a Collector for records of a user-described C struct layout.
   *
   * The description is a C struct field list, e.g. "double time; uint32_t pixel;",
   * parsed at runtime into an HDF5 compound datatype. Records get the same cue-based
   * group layout as typed collectors. When the description equals the canonical
   * description of an EFU readout type (see readout_description_for()) the file is
   * EFU-sendable; otherwise it is readable and combinable but skipped by replay.
   *
   * \param filename The name of the HDF5 file to write to
   * \param dataset The name of the collector group. If nullptr or empty, defaults to "events".
   * \param description The C struct field list describing one record
   * \param normalization The normalization constant associated with the to-be-provided records
   * \returns a new collector, or NULL if the description cannot be parsed
   */
  RL_API collector_t* collector_star_new(const char* filename, const char * dataset, const char * description, uint64_t normalization);

  /** \brief Store one record in a description-based Collector.
   * \param c_ptr Pointer to the Collector object returned by collector_star_new()
   * \param weight The rate-weight of this record, accumulated into the point weight
   * \param record Pointer to collector_record_size() bytes laid out per the description
   */
  RL_API void collector_star_add(const collector_t* c_ptr, double weight, const void* record);

  ///\brief The size in bytes of one record for this collector (0 for NULL input)
  RL_API size_t collector_record_size(const collector_t* c_ptr);

  /** \brief The canonical record description for the readout type implied by an ESS detector type int.
   * \param ess_type the detector type integer used by the components (e.g. 0x34 == 52 for BIFROST)
   * \returns a parseable C struct field list, or NULL for unknown types
   */
  RL_API const char * readout_description_for(int ess_type);

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
   * \param reset_datasets Non-zero value will cause the input files' datasets to be reset to zero-length
   *
   * This function will merge all equivalent datasets in the collector groups in the input files.
   *
   * Since this merges all datasets, it may be dangerous to call this function from within a component
   * runtime; as every component would merge their own and other component's datasets you may duplicate data.
   */
  RL_API void collector_merge_files(const char * out_filename, const char ** in_filenames, size_t count, int reset_datasets);
  RL_API void collector_concatenate_files(const char * out_filename, const char ** in_filenames, size_t count);

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

#ifdef __cplusplus
}
#endif

#endif
