#include "collector.h"

#include "CollectorClass.h"

#ifdef __cplusplus
extern "C" {
#endif
  struct collector{
    void *obj;
  };

collector_t* collector_new(const char* filename, const int point, const int points, const int rings, const int fens, const char * dataset, const int type) {
  const std::string string_filename(filename);
  std::optional<std::string> string_dataset;
  if (dataset != nullptr && dataset[0] != '\0') {
    string_dataset = std::string(dataset);
  }
  const auto c_ptr = static_cast<collector_t *>(malloc(sizeof(collector_t)));
  c_ptr->obj = new Collector(string_filename, point, points, rings, fens, string_dataset, type);
  return c_ptr;
}

void collector_free(collector_t* c_ptr) {
  if (c_ptr == nullptr) return;
  delete static_cast<Collector*>(c_ptr->obj);
  free(c_ptr);
}

void collector_add(const collector_t* c_ptr, const uint8_t ring, const uint8_t fen, const double tof, const double weight, const void* data) {
  if (c_ptr == nullptr) return;
  const auto obj = static_cast<Collector *>(c_ptr->obj);
  obj->addReadout(ring, fen, tof, weight, data);
}

void collector_merge_files(const char * out_filename, const char ** in_filenames, size_t count, int point, int total_points) {
  std::vector<std::string> in_files;
  for (size_t i = 0; i < count; ++i) {
    if (in_filenames[i] != nullptr && in_filenames[i][0] != '\0') {
      in_files.emplace_back(in_filenames[i]);
    }
  }
  Collector::merge_files(out_filename, in_files, point, total_points);
}

void collector_int_parameter(const collector_t* c_ptr, const char* name, const int value, const char* unit, const char* description) {
  if (c_ptr == nullptr) return;
  const auto obj = static_cast<Collector *>(c_ptr->obj);
  std::optional<std::string> us, ds;
  if (unit != nullptr && unit[0] != '\0') {
    us = std::string(unit);
  }
  if (description != nullptr && description[0] != '\0') {
    ds = std::string(description);
  }
  obj->addParameter(name, value, us, ds);
}
void collector_double_parameter(const collector_t* c_ptr, const char* name, const double value, const char* unit, const char* description) {
  if (c_ptr == nullptr) return;
  const auto obj = static_cast<Collector *>(c_ptr->obj);
  std::optional<std::string> us, ds;
  if (unit != nullptr && unit[0] != '\0') {
    us = std::string(unit);
  }
  if (description != nullptr && description[0] != '\0') {
    ds = std::string(description);
  }
  obj->addParameter(name, value, us, ds);
}
void collector_string_parameter(const collector_t* c_ptr, const char* name, const char* value, const char* unit, const char* description) {
  if (c_ptr == nullptr) return;
  const auto obj = static_cast<Collector *>(c_ptr->obj);
  std::optional<std::string> us, ds;
  if (unit != nullptr && unit[0] != '\0') {
    us = std::string(unit);
  }
  if (description != nullptr && description[0] != '\0') {
    ds = std::string(description);
  }
  obj->addParameter(name, std::string(value), us, ds);
};

#ifdef __cplusplus
}
#endif