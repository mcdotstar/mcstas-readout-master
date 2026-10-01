// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
#include "nexus_import.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <highfive/H5File.hpp>
#include <hdf5.h>

#include "readout_collector.h"
#include "TypeDescriptionParser.h"

namespace {

using namespace HighFive;

/// Read a fixed-length string (NAPI writes NX_CHAR as fixed-length, null-terminated
/// strings of exactly strlen characters) into a memory type one byte longer, so the
/// last character is not replaced by the terminator. Only the first element is kept.
template <class Reader>
std::optional<std::string> read_fixed_string(hid_t file_type, size_t elements, Reader reader) {
  if (H5Tget_class(file_type) != H5T_STRING || H5Tis_variable_str(file_type) > 0) return std::nullopt;
  const size_t size = H5Tget_size(file_type) + 1;
  const hid_t mem = H5Tcopy(H5T_C_S1);
  H5Tset_size(mem, size);
  H5Tset_strpad(mem, H5T_STR_NULLTERM);
  std::vector<char> buffer(size * std::max<size_t>(1, elements) + 1, '\0');
  const auto status = reader(mem, buffer.data());
  H5Tclose(mem);
  if (status < 0) return std::nullopt;
  return std::string(buffer.data());
}

template <class O>
std::optional<std::string> string_attribute(const O & obj, const std::string & name) {
  if (!obj.hasAttribute(name)) return std::nullopt;
  try {
    const auto attr = obj.getAttribute(name);
    // NAPI writes fixed-length strings: read those raw (HighFive drops their last character)
    if (auto fixed = read_fixed_string(attr.getDataType().getId(), attr.getSpace().getElementCount(),
          [&](hid_t mem, void * buf) { return H5Aread(attr.getId(), mem, buf); }); fixed.has_value()) {
      return fixed;
    }
    return attr.template read<std::string>();
  } catch (const std::exception &) {
    return std::nullopt;
  }
}

template <class T, class O>
std::optional<T> numeric_attribute(const O & obj, const std::string & name) {
  if (!obj.hasAttribute(name)) return std::nullopt;
  try {
    const auto attr = obj.getAttribute(name);
    if (attr.getSpace().getElementCount() == 1 && attr.getSpace().getNumberDimensions() > 0) {
      return attr.template read<std::vector<T>>().front();
    }
    return attr.template read<T>();
  } catch (const std::exception &) {
    return std::nullopt;
  }
}

/// A string dataset as NAPI writes them (char array of length n, or a scalar string)
std::optional<std::string> string_dataset(const Group & group, const std::string & name) {
  if (!group.exist(name) || group.getObjectType(name) != ObjectType::Dataset) return std::nullopt;
  const auto ds = group.getDataSet(name);
  try {
    return read_fixed_string(ds.getDataType().getId(), ds.getSpace().getElementCount(),
      [&](hid_t mem, void * buf) { return H5Dread(ds.getId(), mem, H5S_ALL, H5S_ALL, H5P_DEFAULT, buf); });
  } catch (const std::exception &) {
    return std::nullopt;
  }
}

std::string strip(std::string s) {
  const auto first = s.find_first_not_of(" \t\n\r\"");
  const auto last = s.find_last_not_of(" \t\n\r\"");
  if (first == std::string::npos) return "";
  return s.substr(first, last - first + 1);
}

/// The memory datatype matching a canonical type name of the description parser
std::optional<DataType> native_type(const std::string & type) {
  if (type == "uint8_t" || type == "unsigned char") return create_datatype<uint8_t>();
  if (type == "int8_t" || type == "char" || type == "signed char") return create_datatype<int8_t>();
  if (type == "uint16_t" || type == "unsigned short") return create_datatype<uint16_t>();
  if (type == "int16_t" || type == "short") return create_datatype<int16_t>();
  if (type == "uint32_t" || type == "unsigned int") return create_datatype<uint32_t>();
  if (type == "int32_t" || type == "int") return create_datatype<int32_t>();
  if (type == "uint64_t" || type == "unsigned long long" || type == "size_t") return create_datatype<uint64_t>();
  if (type == "int64_t" || type == "long long") return create_datatype<int64_t>();
  if (type == "unsigned long") return sizeof(unsigned long) == 8 ? create_datatype<uint64_t>() : create_datatype<uint32_t>();
  if (type == "long") return sizeof(long) == 8 ? create_datatype<int64_t>() : create_datatype<int32_t>();
  if (type == "float") return create_datatype<float>();
  if (type == "double") return create_datatype<double>();
  return std::nullopt;
}

struct ReadoutGroup {
  std::string component;
  std::string name;
  Group group;
};

struct Parameter {
  std::string name;
  std::string type;   // int, double, string (others are stored as strings)
  std::string value;
};

/// Instrument parameters: names and types from instrument/@Parameters (" a(double) b(string)"),
/// values from simulation/Param
std::vector<Parameter> instrument_parameters(const Group & entry) {
  std::vector<Parameter> parameters;
  if (!entry.exist("instrument")) return parameters;
  const auto instrument = entry.getGroup("instrument");
  const auto declared = string_attribute(instrument, "Parameters");
  if (!declared.has_value()) return parameters;
  std::optional<Group> values;
  if (entry.exist("simulation") && entry.getGroup("simulation").exist("Param")) {
    values = entry.getGroup("simulation").getGroup("Param");
  }
  std::istringstream tokens(declared.value());
  std::string token;
  while (tokens >> token) {
    const auto open = token.find('(');
    const auto close = token.rfind(')');
    Parameter p;
    p.name = token.substr(0, open);
    p.type = (open != std::string::npos && close != std::string::npos && close > open)
             ? token.substr(open + 1, close - open - 1) : "string";
    if (values.has_value()) {
      if (auto v = string_dataset(values.value(), p.name); v.has_value()) p.value = strip(v.value());
      else if (auto a = string_attribute(values.value(), p.name); a.has_value()) p.value = strip(a.value());
    }
    parameters.push_back(p);
  }
  return parameters;
}

void add_parameter(const Parameter & p) {
  try {
    if (p.type == "int") {
      collector_sink_int(p.name.c_str(), std::stoi(p.value), "", "");
      return;
    }
    if (p.type == "double") {
      collector_sink_double(p.name.c_str(), std::stod(p.value), "", "");
      return;
    }
  } catch (const std::exception &) {
    // fall through: keep the text
  }
  collector_sink_string(p.name.c_str(), p.value.c_str(), "", "");
}

} // namespace

int import_mccode_nexus(const std::string & out_filename, const std::string & nexus_filename, const bool verbose) {
  if (std::filesystem::exists(out_filename)) {
    std::cerr << "import: output file already exists: " << out_filename << std::endl;
    return -1;
  }
  try {
    File file(nexus_filename, File::ReadOnly);

    // The NXentry holding readout groups (McStas writes entry1; with --append there may be more)
    std::optional<Group> entry;
    std::vector<ReadoutGroup> groups;
    std::map<std::string, std::string> choppers; // component name -> tdc_pv
    for (const auto & entry_name : file.listObjectNames()) {
      if (file.getObjectType(entry_name) != ObjectType::Group) continue;
      const auto candidate = file.getGroup(entry_name);
      if (!candidate.exist("instrument") || !candidate.getGroup("instrument").exist("components")) continue;
      const auto components = candidate.getGroup("instrument").getGroup("components");
      std::vector<ReadoutGroup> found;
      std::map<std::string, std::string> found_choppers;
      for (const auto & comp_name : components.listObjectNames()) {
        if (components.getObjectType(comp_name) != ObjectType::Group) continue;
        const auto comp = components.getGroup(comp_name);
        // NNNN_name -> name
        const auto underscore = comp_name.find('_');
        const std::string instance = underscore == std::string::npos ? comp_name : comp_name.substr(underscore + 1);
        for (const auto & child : comp.listObjectNames()) {
          if (comp.getObjectType(child) != ObjectType::Group) continue;
          const auto g = comp.getGroup(child);
          if (string_attribute(g, "type").value_or("") == "Readouts" && g.hasAttribute("description")) {
            found.push_back({instance, child, g});
          }
        }
        if (strip(string_dataset(comp, "Component_type").value_or("")) == "CollectorDiskChopper"
            && comp.exist("parameters") && comp.getGroup("parameters").exist("tdc_pv")) {
          const auto tdc = strip(string_attribute(comp.getGroup("parameters").getGroup("tdc_pv"), "value").value_or(""));
          if (!tdc.empty() && tdc != "0" && tdc != "NULL") found_choppers[instance] = tdc;
        }
      }
      if (!found.empty() || !found_choppers.empty()) {
        if (entry.has_value()) {
          std::cerr << "import: " << nexus_filename << " has readouts in several entries, using " << entry->getPath() << std::endl;
          continue;
        }
        entry = candidate;
        groups = std::move(found);
        choppers = std::move(found_choppers);
      }
    }
    if (!entry.has_value() || groups.empty()) {
      std::cerr << "import: no readout groups found in " << nexus_filename << std::endl;
      return -1;
    }

    // unique collector names
    std::map<std::string, int> name_count;
    for (const auto & g : groups) name_count[g.name]++;

    std::vector<collector_t *> collectors;
    bool parameters_written = false;
    int imported = 0;
    for (const auto & rg : groups) {
      const auto & g = rg.group;
      const auto description = string_attribute(g, "description").value();
      const auto ess_type = numeric_attribute<int32_t>(g, "ess_type").value_or(0);
      const auto normalization = numeric_attribute<uint64_t>(g, "normalization").value_or(0);
      const std::string name = name_count[rg.name] > 1 ? rg.component + "_" + rg.name : rg.name;
      const auto schema = parse_type_description(description);

      size_t rows = 0;
      bool first_field = true, consistent = true;
      for (const auto & f : schema.fields) {
        if (!g.exist(f.name)) {
          std::cerr << "import: " << g.getPath() << " has no dataset for field " << f.name << std::endl;
          consistent = false;
          break;
        }
        const auto n = g.getDataSet(f.name).getDimensions().front();
        if (first_field) rows = n;
        else if (n != rows) consistent = false;
        first_field = false;
      }
      if (!consistent) {
        std::cerr << "import: inconsistent field datasets in " << g.getPath() << ", skipped" << std::endl;
        continue;
      }

      collector_t * collector = collector_star_new(out_filename.c_str(), name.c_str(), description.c_str(), ess_type, normalization);
      if (collector == nullptr) {
        std::cerr << "import: could not create collector " << name << std::endl;
        for (auto * c : collectors) collector_free(c);
        return -1;
      }
      collectors.push_back(collector);
      if (const auto address = string_attribute(g, "efu_address"); address.has_value()) {
        const auto port = numeric_attribute<int32_t>(g, "efu_port").value_or(0);
        if (port > 0) collector_efu(collector, address->c_str(), port);
      }
      if (!parameters_written) {
        for (const auto & p : instrument_parameters(entry.value())) add_parameter(p);
        for (const auto & [component, tdc] : choppers) {
          const auto key = component + "_chopper_tdc";
          collector_sink_string(key.c_str(), tdc.c_str(), nullptr, "top-dead-centre channel of this disc");
        }
        parameters_written = true;
      }

      // copy in blocks, field by field into packed records
      const size_t block = 1 << 16;
      const auto weight_field = std::find_if(schema.fields.begin(), schema.fields.end(),
        [](const SchemaField & f) { return f.name == "weight" && f.type == "double" && f.array_count == 0; });
      std::vector<uint8_t> records;
      std::vector<uint8_t> column;
      for (size_t start = 0; start < rows; start += block) {
        const size_t n = std::min(block, rows - start);
        records.assign(n * schema.total_size, 0);
        for (const auto & f : schema.fields) {
          const auto type = native_type(f.type);
          if (!type.has_value()) throw std::runtime_error("unsupported field type " + f.type);
          const auto ds = g.getDataSet(f.name);
          const size_t count = f.array_count > 0 ? f.array_count : 1;
          column.resize(n * f.total_size());
          if (ds.getDimensions().size() == 1) {
            ds.select({start}, {n}).read_raw(column.data(), type.value());
          } else {
            ds.select({start, 0}, {n, count}).read_raw(column.data(), type.value());
          }
          for (size_t r = 0; r < n; ++r) {
            std::memcpy(records.data() + r * schema.total_size + f.offset, column.data() + r * f.total_size(), f.total_size());
          }
        }
        for (size_t r = 0; r < n; ++r) {
          double weight = 0;
          if (weight_field != schema.fields.end()) {
            std::memcpy(&weight, records.data() + r * schema.total_size + weight_field->offset, sizeof(double));
          }
          collector_star_add(collector, weight, records.data() + r * schema.total_size);
        }
      }
      if (verbose) {
        std::cout << "imported " << rows << " records from " << g.getPath() << " as " << name
                  << " (ess_type " << ess_type << ", normalization " << normalization << ")" << std::endl;
      }
      ++imported;
    }
    for (auto * c : collectors) collector_free(c);
    return imported;
  } catch (const std::exception & e) {
    std::cerr << "import: " << e.what() << std::endl;
    return -1;
  }
}
