// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Generic runtime-typed data collector for HDF5 output
///
/// CollectorStar stores arbitrary C struct data described at runtime via
/// a C struct syntax string. It can write HDF5 compound datasets with
/// proper field types, or store opaque blobs when no schema is provided.
///
//===----------------------------------------------------------------------===//
#pragma once

#include <string>
#include <vector>
#include <memory>

#include "TypeDescriptionParser.h"

#include <highfive/H5File.hpp>
#include <highfive/H5DataSet.hpp>

#ifdef WIN32
#ifdef READOUT_SHARED
#ifdef READOUT_EXPORT
#define RL_API __declspec(dllexport)
#else
#define RL_API __declspec(dllimport)
#endif
#else
#define RL_API
#endif
#else
#define RL_API
#endif


/// Build an HDF5 compound datatype from a TypeSchema
RL_API HighFive::CompoundType build_hdf5_compound_type(const TypeSchema& schema);

class RL_API CollectorStar {
public:
  /// Mode 1: Description-only — size is computed from the parsed schema
  explicit CollectorStar(const std::string& type_description, const std::string& dataset_name);

  /// Mode 2: Opaque — only object size, no schema
  explicit CollectorStar(size_t object_size, const std::string& dataset_name);

  /// Mode 3: Validated — description parsed and size checked for consistency
  CollectorStar(const std::string& type_description, size_t object_size, const std::string& dataset_name);

  ~CollectorStar() = default;

  // Non-copyable, moveable
  CollectorStar(const CollectorStar&) = delete;
  CollectorStar& operator=(const CollectorStar&) = delete;
  CollectorStar(CollectorStar&&) = default;
  CollectorStar& operator=(CollectorStar&&) = default;

  /// Add an object to the collection by copying object_size bytes from src
  void add(const void* src);

  /// Copy the object at the given index into dst (caller must provide object_size bytes)
  void get(size_t index, void* dst) const;

  /// Number of collected objects
  size_t count() const { return count_; }

  /// Size of each object in bytes
  size_t object_size() const { return object_size_; }

  /// Whether a type schema is available (false = opaque mode)
  bool has_schema() const { return schema_ != nullptr; }

  /// Access the schema (may be nullptr in opaque mode)
  const TypeSchema* schema() const { return schema_.get(); }

  /// Dataset name for HDF5 output
  const std::string& dataset_name() const { return dataset_name_; }

  /// Write all collected data to an HDF5 file.
  /// In schema mode, creates a compound dataset. In opaque mode, writes raw bytes.
  void write_hdf5(const std::string& filename) const;

  /// Write all collected data into an existing open HDF5 file
  void write_hdf5(const HighFive::File& file) const;

  /// Write all collected data into an existing open HDF5 file's group
  void write_hdf5(HighFive::Group& group) const;

private:
  void init_with_description(const std::string& type_description);

  std::unique_ptr<TypeSchema> schema_;
  std::vector<uint8_t> data_;
  size_t object_size_;
  size_t count_;
  std::string dataset_name_;
};
