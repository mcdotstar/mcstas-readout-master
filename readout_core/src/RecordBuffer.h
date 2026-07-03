// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Minimal in-memory buffer of fixed-size records
///
/// Internal helper for the Discrete samplers, which must keep every collected
/// record in memory until sampling selects the survivors. The record size comes
/// either from an explicit byte count or from a parsed C-struct description.
///
//===----------------------------------------------------------------------===//
#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "TypeDescriptionParser.h"

class RecordBuffer {
  std::vector<uint8_t> data_;
  size_t object_size_;
  size_t count_{0};

public:
  explicit RecordBuffer(const size_t object_size): object_size_(object_size) {
    if (object_size == 0) {
      throw std::invalid_argument("RecordBuffer: object_size must be > 0");
    }
  }

  explicit RecordBuffer(const std::string & type_description)
    : object_size_(parse_type_description(type_description).total_size) {}

  RecordBuffer(const std::string & type_description, const size_t object_size): object_size_(object_size) {
    if (object_size == 0) {
      throw std::invalid_argument("RecordBuffer: object_size must be > 0");
    }
    if (const auto schema = parse_type_description(type_description); schema.total_size != object_size) {
      throw std::invalid_argument(
        "RecordBuffer: type description computes to " + std::to_string(schema.total_size) +
        " bytes, but object_size is " + std::to_string(object_size) + " bytes"
      );
    }
  }

  void add(const void * src) {
    if (src == nullptr) return;
    const auto * bytes = static_cast<const uint8_t *>(src);
    data_.insert(data_.end(), bytes, bytes + object_size_);
    ++count_;
  }

  void get(const size_t index, void * dst) const {
    if (index >= count_) {
      throw std::out_of_range(
        "RecordBuffer::get: index " + std::to_string(index) +
        " out of range (count=" + std::to_string(count_) + ")"
      );
    }
    if (dst == nullptr) return;
    std::memcpy(dst, data_.data() + index * object_size_, object_size_);
  }

  [[nodiscard]] size_t count() const { return count_; }
  [[nodiscard]] size_t object_size() const { return object_size_; }
};
