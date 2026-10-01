#include "readout_collector.h"

#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>

#include "CollectorClass.h"
#include "readout_type_descriptions.h"
#include "TypeDescriptionParser.h"
#include "enums.h"

#ifdef __cplusplus
extern "C" {
#endif
  struct collector{
    void *obj;
  };

collector_t* collector_new(const char* filename, const char * dataset, const int type, const uint64_t normalization) {
  const std::string string_filename(filename);
  const std::string dataset_name = (dataset != nullptr && dataset[0] != '\0') ? std::string(dataset) : "events";
  try {
    const auto c_ptr = static_cast<collector_t *>(malloc(sizeof(collector_t)));
    c_ptr->obj = new Collector(string_filename, dataset_name, type, normalization);
    return c_ptr;
  } catch (const std::exception & ex) {
    // as collector_star_new does: a message and a null handle, rather than an exception
    // escaping into the C that called us
    std::cerr << "collector_new failed: " << ex.what() << std::endl;
    return nullptr;
  }
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

int readout_description_fields(const char * description, readout_field_t * fields, const int max_fields) {
  if (description == nullptr || description[0] == '\0') return -1;
  try {
    const auto schema = parse_type_description(std::string(description));
    const int n = static_cast<int>(schema.fields.size());
    for (int i = 0; fields != nullptr && i < n && i < max_fields; ++i) {
      const auto & f = schema.fields[static_cast<size_t>(i)];
      readout_field_t & out = fields[i];
      std::snprintf(out.name, sizeof(out.name), "%s", f.name.c_str());
      std::snprintf(out.type, sizeof(out.type), "%s", f.type.c_str());
      out.offset = f.offset;
      out.element_size = f.element_size;
      out.count = f.array_count > 0 ? f.array_count : 1;
    }
    return n;
  } catch (const std::exception & ex) {
    std::cerr << "readout_description_fields failed: " << ex.what() << std::endl;
    return -1;
  }
}

size_t readout_description_size(const char * description) {
  if (description == nullptr || description[0] == '\0') return 0;
  try {
    return parse_type_description(std::string(description)).total_size;
  } catch (const std::exception &) {
    return 0;
  }
}

const char * readout_detector_name(const int ess_type) {
  static std::map<int, std::string> names;
  static std::mutex names_mutex;
  const std::lock_guard<std::mutex> lock(names_mutex);
  auto it = names.find(ess_type);
  if (it == names.end()) {
    std::string name;
    try {
      name = detectorType_name(detectorType_from_int(ess_type));
    } catch (const std::exception &) {
      name = "";
    }
    it = names.emplace(ess_type, name).first;
  }
  return it->second.c_str();
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
