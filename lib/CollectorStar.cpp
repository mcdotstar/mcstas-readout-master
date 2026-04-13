// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implementation of the generic runtime-typed data collector
///
//===----------------------------------------------------------------------===//

#include "CollectorStar.h"
#include <stdexcept>
#include <iostream>
#include <unordered_map>


namespace {

/// Map canonical type names to HighFive DataType creators
HighFive::DataType hdf5_type_for(const std::string& canonical_type) {
  static const std::unordered_map<std::string, HighFive::DataType(*)()> creators = {
    {"int8_t",   [] () -> HighFive::DataType { return HighFive::create_datatype<int8_t>(); }},
    {"int16_t",  [] () -> HighFive::DataType { return HighFive::create_datatype<int16_t>(); }},
    {"int32_t",  [] () -> HighFive::DataType { return HighFive::create_datatype<int32_t>(); }},
    {"int64_t",  [] () -> HighFive::DataType { return HighFive::create_datatype<int64_t>(); }},
    {"uint8_t",  [] () -> HighFive::DataType { return HighFive::create_datatype<uint8_t>(); }},
    {"uint16_t", [] () -> HighFive::DataType { return HighFive::create_datatype<uint16_t>(); }},
    {"uint32_t", [] () -> HighFive::DataType { return HighFive::create_datatype<uint32_t>(); }},
    {"uint64_t", [] () -> HighFive::DataType { return HighFive::create_datatype<uint64_t>(); }},
    {"float",    [] () -> HighFive::DataType { return HighFive::create_datatype<float>(); }},
    {"double",   [] () -> HighFive::DataType { return HighFive::create_datatype<double>(); }},
  };

  auto it = creators.find(canonical_type);
  if (it == creators.end()) {
    throw std::runtime_error("No HDF5 type mapping for: " + canonical_type);
  }
  return it->second();
}

/// Map canonical type names to HDF5 C API type constants
hid_t hdf5_native_type_for(const std::string& canonical_type) {
  static const std::unordered_map<std::string, hid_t> types = {
    {"int8_t",   H5T_NATIVE_INT8},
    {"int16_t",  H5T_NATIVE_INT16},
    {"int32_t",  H5T_NATIVE_INT32},
    {"int64_t",  H5T_NATIVE_INT64},
    {"uint8_t",  H5T_NATIVE_UINT8},
    {"uint16_t", H5T_NATIVE_UINT16},
    {"uint32_t", H5T_NATIVE_UINT32},
    {"uint64_t", H5T_NATIVE_UINT64},
    {"float",    H5T_NATIVE_FLOAT},
    {"double",   H5T_NATIVE_DOUBLE},
  };
  auto it = types.find(canonical_type);
  if (it == types.end()) {
    throw std::runtime_error("No HDF5 native type for: " + canonical_type);
  }
  return it->second;
}

bool schema_has_arrays(const TypeSchema& schema) {
  for (const auto& f : schema.fields) {
    if (f.array_count > 0) return true;
  }
  return false;
}

/// Build an HDF5 compound type id using the C API. Caller must H5Tclose() the result.
hid_t build_compound_hid(const TypeSchema& schema) {
  hid_t compound_tid = H5Tcreate(H5T_COMPOUND, schema.total_size);
  if (compound_tid < 0) {
    throw std::runtime_error("Failed to create HDF5 compound type");
  }
  for (const auto& field : schema.fields) {
    hid_t member_tid = hdf5_native_type_for(field.type);
    if (field.array_count > 0) {
      hsize_t dims = static_cast<hsize_t>(field.array_count);
      hid_t array_tid = H5Tarray_create2(member_tid, 1, &dims);
      if (array_tid < 0) {
        H5Tclose(compound_tid);
        throw std::runtime_error("Failed to create HDF5 array type for field: " + field.name);
      }
      H5Tinsert(compound_tid, field.name.c_str(), field.offset, array_tid);
      H5Tclose(array_tid);
    } else {
      H5Tinsert(compound_tid, field.name.c_str(), field.offset, member_tid);
    }
  }
  return compound_tid;
}

/// Write data to an HDF5 file using the C API directly (needed for array member types)
void write_hdf5_c_api(hid_t file_id, const std::string& dataset_name,
                       const TypeSchema& schema, const uint8_t* data, size_t count) {
  hid_t compound_tid = build_compound_hid(schema);

  hsize_t dims = static_cast<hsize_t>(count);
  hid_t dataspace = H5Screate_simple(1, &dims, nullptr);
  if (dataspace < 0) {
    H5Tclose(compound_tid);
    throw std::runtime_error("Failed to create HDF5 dataspace");
  }

  hid_t dataset = H5Dcreate2(file_id, dataset_name.c_str(), compound_tid, dataspace,
                              H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  if (dataset < 0) {
    H5Sclose(dataspace);
    H5Tclose(compound_tid);
    throw std::runtime_error("Failed to create HDF5 dataset: " + dataset_name);
  }

  herr_t status = H5Dwrite(dataset, compound_tid, H5S_ALL, H5S_ALL, H5P_DEFAULT, data);
  H5Dclose(dataset);
  H5Sclose(dataspace);
  H5Tclose(compound_tid);

  if (status < 0) {
    throw std::runtime_error("Failed to write HDF5 dataset: " + dataset_name);
  }
}

} // anonymous namespace


HighFive::CompoundType build_hdf5_compound_type(const TypeSchema& schema) {
  // This function only supports schemas without array fields.
  // For schemas with arrays, use the C API write path directly.
  std::vector<HighFive::CompoundType::member_def> members;
  members.reserve(schema.fields.size());

  for (const auto& field : schema.fields) {
    if (field.array_count > 0) {
      throw std::runtime_error(
        "build_hdf5_compound_type: array fields require C API path (field: " + field.name + ")"
      );
    }
    members.push_back({field.name, hdf5_type_for(field.type), field.offset});
  }

  return HighFive::CompoundType(members, schema.total_size);
}


// Mode 1: Description-only
CollectorStar::CollectorStar(const std::string& type_description, const std::string& dataset_name)
  : object_size_(0), count_(0), dataset_name_(dataset_name)
{
  init_with_description(type_description);
  object_size_ = schema_->total_size;
}

// Mode 2: Opaque
CollectorStar::CollectorStar(size_t object_size, const std::string& dataset_name)
  : schema_(nullptr), object_size_(object_size), count_(0), dataset_name_(dataset_name)
{
  if (object_size == 0) {
    throw std::invalid_argument("CollectorStar: object_size must be > 0");
  }
}

// Mode 3: Validated
CollectorStar::CollectorStar(const std::string& type_description, size_t object_size, const std::string& dataset_name)
  : object_size_(object_size), count_(0), dataset_name_(dataset_name)
{
  if (object_size == 0) {
    throw std::invalid_argument("CollectorStar: object_size must be > 0");
  }
  init_with_description(type_description);
  if (schema_->total_size != object_size) {
    throw std::invalid_argument(
      "CollectorStar: type description computes to " + std::to_string(schema_->total_size) +
      " bytes, but object_size is " + std::to_string(object_size) + " bytes"
    );
  }
}

void CollectorStar::init_with_description(const std::string& type_description) {
  schema_ = std::make_unique<TypeSchema>(parse_type_description(type_description));
}

void CollectorStar::add(const void* src) {
  if (src == nullptr) return;
  const auto* bytes = static_cast<const uint8_t*>(src);
  data_.insert(data_.end(), bytes, bytes + object_size_);
  ++count_;
}

void CollectorStar::get(size_t index, void* dst) const {
  if (index >= count_) {
    throw std::out_of_range(
      "CollectorStar::get: index " + std::to_string(index) +
      " out of range (count=" + std::to_string(count_) + ")"
    );
  }
  if (dst == nullptr) return;
  std::memcpy(dst, data_.data() + index * object_size_, object_size_);
}

void CollectorStar::write_hdf5(const std::string& filename) const {
  HighFive::File file(filename, HighFive::File::OpenOrCreate);
  write_hdf5(file);
}

void CollectorStar::write_hdf5(HighFive::File& file) const {
  if (count_ == 0) return;

  if (has_schema()) {
    if (schema_has_arrays(*schema_)) {
      // Use HDF5 C API directly for schemas with array member types,
      // since HighFive doesn't expose an array type wrapper.
      write_hdf5_c_api(file.getId(), dataset_name_, *schema_, data_.data(), count_);
    } else {
      auto compound_type = build_hdf5_compound_type(*schema_);
      auto dataspace = HighFive::DataSpace({count_});
      auto dataset = file.createDataSet(dataset_name_, dataspace, compound_type);
      dataset.write_raw(data_.data(), compound_type);
    }
  } else {
    // Opaque mode: write as a 2D array of uint8_t [count_ x object_size_]
    auto dataspace = HighFive::DataSpace({count_, object_size_});
    auto dataset = file.createDataSet(dataset_name_, dataspace, HighFive::create_datatype<uint8_t>());
    dataset.write_raw(data_.data(), HighFive::create_datatype<uint8_t>());
  }
}
