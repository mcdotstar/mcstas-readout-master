#include "readout_collector.h"

#include <filesystem>

#include "CollectorClass.h"
#include "CollectorStar.h"

#ifdef __cplusplus
extern "C" {
#endif
  struct collector{
    void *obj;
  };

collector_t* collector_new(const char* filename, const int point, const int points, const char * dataset, const int type) {
  const std::string string_filename(filename);
  const std::string dataset_name = (dataset != nullptr && dataset[0] != '\0') ? std::string(dataset) : "events";
  const auto c_ptr = static_cast<collector_t *>(malloc(sizeof(collector_t)));
  c_ptr->obj = new Collector(string_filename, dataset_name, point, points, type);
  return c_ptr;
}

void collector_free(collector_t* c_ptr) {
  if (c_ptr == nullptr) return;
  delete static_cast<Collector*>(c_ptr->obj);
  free(c_ptr);
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

void collector_merge_files(const char * out_filename, const char ** in_filenames, const size_t count, const int point, const int total_points) {
  std::vector<std::string> in_files;
  for (size_t i = 0; i < count; ++i) {
    if (in_filenames[i] != nullptr && in_filenames[i][0] != '\0') {
      in_files.emplace_back(in_filenames[i]);
    }
  }
  merge_collector_files(out_filename, in_files, point, total_points);
}

void collector_merge(const char * out_filename, const char ** in_filenames, size_t count, int point, int total_points, const char * dataset, const int reset_datasets) {
  std::vector<std::string> in_files;
  for (size_t i = 0; i < count; ++i) {
    if (in_filenames[i] != nullptr && in_filenames[i][0] != '\0') {
      in_files.emplace_back(in_filenames[i]);
    }
  }
  merge_collector_datasets(out_filename, in_files, point, total_points, std::string(dataset), reset_datasets != 0);
  copy_collector_parameters(out_filename, in_files, point, total_points);
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

// ---- CollectorStar C interface ----

struct collector_star {
  void* obj;
};

collector_star_t* collector_star_new_description(const char* type_description, const char* dataset_name) {
  try {
    auto cs = static_cast<collector_star_t*>(malloc(sizeof(collector_star_t)));
    cs->obj = new CollectorStar(std::string(type_description), std::string(dataset_name));
    return cs;
  } catch (const std::exception& e) {
    std::cerr << "collector_star_new_description error: " << e.what() << std::endl;
    return nullptr;
  }
}

collector_star_t* collector_star_new_opaque(size_t object_size, const char* dataset_name) {
  try {
    auto cs = static_cast<collector_star_t*>(malloc(sizeof(collector_star_t)));
    cs->obj = new CollectorStar(object_size, std::string(dataset_name));
    return cs;
  } catch (const std::exception& e) {
    std::cerr << "collector_star_new_opaque error: " << e.what() << std::endl;
    return nullptr;
  }
}

collector_star_t* collector_star_new_validated(const char* type_description, size_t object_size, const char* dataset_name) {
  try {
    auto cs = static_cast<collector_star_t*>(malloc(sizeof(collector_star_t)));
    cs->obj = new CollectorStar(std::string(type_description), object_size, std::string(dataset_name));
    return cs;
  } catch (const std::exception& e) {
    std::cerr << "collector_star_new_validated error: " << e.what() << std::endl;
    return nullptr;
  }
}

void collector_star_free(collector_star_t* cs) {
  if (cs == nullptr) return;
  delete static_cast<CollectorStar*>(cs->obj);
  free(cs);
}

void collector_star_add(collector_star_t* cs, const void* src) {
  if (cs == nullptr) return;
  static_cast<CollectorStar*>(cs->obj)->add(src);
}

int collector_star_get(const collector_star_t* cs, size_t index, void* dst) {
  if (cs == nullptr) return -1;
  try {
    static_cast<const CollectorStar*>(cs->obj)->get(index, dst);
    return 0;
  } catch (const std::out_of_range&) {
    return -1;
  }
}

size_t collector_star_count(const collector_star_t* cs) {
  if (cs == nullptr) return 0;
  return static_cast<const CollectorStar*>(cs->obj)->count();
}

size_t collector_star_object_size(const collector_star_t* cs) {
  if (cs == nullptr) return 0;
  return static_cast<const CollectorStar*>(cs->obj)->object_size();
}

int collector_star_write_hdf5(const collector_star_t* cs, const char* filename) {
  if (cs == nullptr || filename == nullptr) return -1;
  try {
    static_cast<const CollectorStar*>(cs->obj)->write_hdf5(std::string(filename));
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "collector_star_write_hdf5 error: " << e.what() << std::endl;
    return -1;
  }
}

#ifdef __cplusplus
}
#endif
