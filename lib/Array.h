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

/// An array of un-evaluated data items with `byte_count` bytes each
class RL_API Array {
public:
  explicit Array(size_t byte_count);

  ~Array() = default;

  // Non-copyable, moveable
  Array(const Array&) = delete;
  Array& operator=(const Array&) = delete;
  Array(Array&&) = default;
  Array& operator=(Array&&) = default;

  /// Add an object to the collection by copying object_size bytes from src
  void add(const void* src);

  /// Copy the object at the given index into dst (caller must provide object_size bytes)
  void get(size_t index, void* dst) const;

  /// Number of collected objects
  size_t count() const { return count_; }

  /// Size of each object in bytes
  size_t object_size() const { return bytes_; }

  /// Clear all collected data but keep the allocated memory for reuse
  void clear() {count_ = 0;}

  ///  Return a pointer to the first element
 [[nodiscard]] const uint8_t * data() const { return &data_.front(); }

private:
  std::vector<uint8_t> data_;
  size_t bytes_;
  size_t count_{0};
};
