#ifndef COLLECTOR_WRAPPER
#define COLLECTOR_WRAPPER

#include "Readout_structs.h"

#ifdef __cplusplus
extern "C" {
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
   * \param rings The number of rings in the detector -- defines the number of datasets to create.
   *              If 0, this is ignored and all data is written to a single dataset.
   * \param fens The number of FENs in the detector -- defines the number of datasets to create.
   *             If 0, this is ignored and all data is written to a single dataset.
   * \param dataset The name of the dataset to write to.
   *                If rings and fens are both 0, this is the name of the single dataset created.
   *                If rings and fens are both greater than 0, this is the name of the group created to hold
   *                the ring/FEN datasets. If nullptr or empty, defaults to "events".
   * \param type The type of readout data to write, defined as an integer corresponding to the ReadoutType enum
   *             in Readout.h (e.g. 0x34 for BIFROST, 0x41 for He3CSPEC).
   *             This is used to determine the structure of the readout data and the name of the compound datatype
   *             created in the HDF5 file. If an unrecognized type is given, the compound datatype will still be
   *             created but the structure of the readout data will not be validated when adding readouts.
   */
  RL_API collector_t* collector_new(const char* filename, int point, int points, int rings, int fens, const char * dataset, int type);

  ///\brief Destroy an existing Collector object and free its resources
  RL_API void collector_free(collector_t* c_ptr);

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

  // Combine multiple files into one for the Collector object -- each should have come from an equivalent object
  RL_API void collector_merge_files(const char * out_filename, const char ** in_filenames, size_t count, int point, int total_points);

  /** \brief Add a parameter value to the HDF5 file as a dataset.
   * \param c_ptr Pointer to the Collector object
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
  RL_API void collector_int_parameter(const collector_t* c_ptr, const char* name, int value, const char* unit, const char* description);
  RL_API void collector_double_parameter(const collector_t* c_ptr, const char* name, double value, const char* unit, const char* description);
  RL_API void collector_string_parameter(const collector_t* c_ptr, const char* name, const char* value, const char* unit, const char* description);

#ifdef __cplusplus
}
#endif

#endif