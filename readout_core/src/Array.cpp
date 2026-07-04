// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implementation of the generic runtime-typed data collector
///
//===----------------------------------------------------------------------===//

#include "Array.h"

#include <cstring>
#include <stdexcept>
#include <iostream>

Array::Array(const size_t byte_count) : bytes_(byte_count) {
  if (byte_count == 0) {
    throw std::invalid_argument("Array: byte_count must be > 0");
  }
}

void Array::add(const void* src) {
  if (src == nullptr) return;
  const auto* bytes = static_cast<const uint8_t*>(src);
  data_.insert(data_.end(), bytes, bytes + bytes_);
  ++count_;
}

void Array::get(const size_t index, void* dst) const {
  if (index >= count_) {
    throw std::out_of_range(
      "Array::get: index " + std::to_string(index) + " out of range (count=" + std::to_string(count_) + ")"
    );
  }
  if (dst == nullptr) return;
  std::memcpy(dst, data_.data() + index * bytes_, bytes_);
}
