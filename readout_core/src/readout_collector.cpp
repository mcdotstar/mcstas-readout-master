#include "readout_collector.h"

#include <filesystem>

#include "CollectorClass.h"
#include "readout_type_descriptions.h"

#ifdef __cplusplus
extern "C" {
#endif
  struct collector{
    void *obj;
  };

collector_t* collector_new(const char* filename, const char * dataset, const int type, const uint64_t normalization) {
  const std::string string_filename(filename);
  const std::string dataset_name = (dataset != nullptr && dataset[0] != '\0') ? std::string(dataset) : "events";
  const auto c_ptr = static_cast<collector_t *>(malloc(sizeof(collector_t)));
  c_ptr->obj = new Collector(string_filename, dataset_name, type, normalization);
  return c_ptr;
}

void collector_free(collector_t* c_ptr) {
  if (c_ptr == nullptr) return;
  delete static_cast<Collector*>(c_ptr->obj);
  free(c_ptr);
}

void collector_efu(collector_t* c_ptr, const char* address, const int port) {
  if (c_ptr == nullptr) return;
  if (address == nullptr || address[0] == '\0') return;
  if (port <= 0) return;
  static_cast<Collector*>(c_ptr->obj)->setEFU(std::string(address), port);
}

collector_t* collector_star_new(const char* filename, const char * dataset, const char * description, const int ess_type, const uint64_t normalization) {
  if (description == nullptr || description[0] == '\0') {
    std::cerr << "collector_star_new requires a non-empty type description" << std::endl;
    return nullptr;
  }
  const std::string string_filename(filename);
  const std::string dataset_name = (dataset != nullptr && dataset[0] != '\0') ? std::string(dataset) : "events";
  try {
    const auto c_ptr = static_cast<collector_t *>(malloc(sizeof(collector_t)));
    c_ptr->obj = new Collector(string_filename, dataset_name, std::string(description), normalization, ess_type);
    return c_ptr;
  } catch (const std::exception & ex) {
    std::cerr << "collector_star_new failed: " << ex.what() << std::endl;
    return nullptr;
  }
}

void collector_star_add(const collector_t* c_ptr, const double weight, const void* record) {
  if (c_ptr == nullptr || record == nullptr) return;
  static_cast<Collector*>(c_ptr->obj)->addRecord(weight, record);
}

size_t collector_record_size(const collector_t* c_ptr) {
  if (c_ptr == nullptr) return 0;
  return static_cast<Collector*>(c_ptr->obj)->record_size();
}

const char * readout_description_for(const int ess_type) {
  const auto detector = detectorType_from_int(ess_type);
  const auto readout = readoutType_from_detectorType(detector);
  return readout_type_description(readout);
}

int collector_sink_open(const char * filename) {
  const auto sink = CollectorSink::instance();
  if (!sink->is_setup()) return 0;
  if (std::string(filename) == sink->current_filename()) return 1;
  std::cerr << "Warning: collector sink is set up with file " << sink->current_filename() << ", not " << filename << std::endl;
  return -1;
}

int collector_sink_users(const char * filename) {
  const auto sink = CollectorSink::instance();
  if (!sink->is_setup()) return 0;
  if (std::string(filename) == sink->current_filename()) return static_cast<int>(sink->user_count());
  std::cerr << "Warning: collector sink is set up with file " << sink->current_filename() << ", not " << filename << std::endl;
  return -1;
}

void collector_add(const collector_t* c_ptr, const uint8_t ring, const uint8_t fen, const double tof, const double weight, const void* data) {
  if (c_ptr == nullptr) return;
  const auto obj = static_cast<Collector *>(c_ptr->obj);
  obj->addReadout(ring, fen, tof, weight, data);
}

void collector_merge_files(const char * out_filename, const char ** in_filenames, const size_t count, const int reset_datasets) {
  std::vector<std::string> in_files;
  for (size_t i = 0; i < count; ++i) {
    if (in_filenames[i] != nullptr && in_filenames[i][0] != '\0') {
      in_files.emplace_back(in_filenames[i]);
    }
  }
  append_collector_files(out_filename, in_files, reset_datasets != 0);
}

void collector_concatenate_files(const char * out_filename, const char ** in_filenames, const size_t count) {
  std::vector<std::string> in_files;
  for (size_t i = 0; i < count; ++i) {
    if (in_filenames[i] != nullptr && in_filenames[i][0] != '\0') {
      in_files.emplace_back(in_filenames[i]);
    }
  }
  concatenate_collector_files(out_filename, in_files);
}


void collector_sink_int(const char* name, const int value, const char* unit, const char* description) {
  const auto sink = CollectorSink::instance();
  if (!sink->is_setup()) return;
  std::optional<std::string> us, ds;
  if (unit != nullptr && unit[0] != '\0') {
    us = std::string(unit);
  }
  if (description != nullptr && description[0] != '\0') {
    ds = std::string(description);
  }
  sink->addParameter(name, value, us, ds);
}
void collector_sink_double(const char* name, const double value, const char* unit, const char* description) {
  const auto sink = CollectorSink::instance();
  if (!sink->is_setup()) return;
  std::optional<std::string> us, ds;
  if (unit != nullptr && unit[0] != '\0') {
    us = std::string(unit);
  }
  if (description != nullptr && description[0] != '\0') {
    ds = std::string(description);
  }
  sink->addParameter(name, value, us, ds);
}
void collector_sink_string(const char* name, const char* value, const char* unit, const char* description) {
  const auto sink = CollectorSink::instance();
  if (!sink->is_setup()) return;
  std::optional<std::string> us, ds;
  if (unit != nullptr && unit[0] != '\0') {
    us = std::string(unit);
  }
  if (description != nullptr && description[0] != '\0') {
    ds = std::string(description);
  }
  sink->addParameter(name, std::string(value), us, ds);
};

  int collector_construct_filename_size(const char * basepath, const char * basename) {
    const std::string output_str = filename_for_collector(basepath, basename);
    return output_str.size() + 1;
  }

  int collector_construct_filename(const char * basepath, const char * basename, char * filename) {
    const std::string output_str = filename_for_collector(basepath, basename);
    std::strncpy(filename, output_str.c_str(), output_str.size() + 1);
    return 0;
  }

  int collector_mpi_node_filename_size(const char * basepath, const char * basename, const int node_index, const int total_nodes) {
    const std::string output_str = filename_for_collector_node(basepath, basename, node_index, total_nodes);
    return output_str.size() + 1;
  }

  int collector_mpi_node_filename(const char * basepath, const char * basename, char * filename, const int node_index, const int total_nodes) {
    const std::string output_str = filename_for_collector_node(basepath, basename, node_index, total_nodes);
    std::strncpy(filename, output_str.c_str(), output_str.size() + 1);
    return 0;
  }

  int collector_mpi_node_filename_sizes(const char * basepath, const char * basename, const int total_nodes, int * sizes) {
    int count = 0;
    for (int i = 0; i < total_nodes; ++i) {
      const std::string node_filename = filename_for_collector_node(basepath, basename, i, total_nodes);
      sizes[i] = node_filename.size() + 1;
      count += sizes[i];
    }
    return count;
  }

  int collector_mpi_node_filenames(const char * basepath, const char * basename, char ** filenames, const int total_nodes) {
    for (int i = 0; i < total_nodes; ++i) {
      const std::string node_filename = filename_for_collector_node(basepath, basename, i, total_nodes);
      std::strncpy(filenames[i], node_filename.c_str(), node_filename.size() + 1);
    }
    return 0;
  }

#ifdef __cplusplus
}
#endif
