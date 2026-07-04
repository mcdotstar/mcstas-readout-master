// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Parser for C struct type descriptions into a runtime type schema
///
/// Accepts valid C struct syntax, e.g.:
///   "struct { int count; double energy; uint16_t values[10]; }"
/// and produces a TypeSchema describing field names, types, offsets, and sizes.
///
//===----------------------------------------------------------------------===//
#pragma once

#include <string>
#include <vector>
#include <stdexcept>

#ifdef WIN32
// Export symbols if compile flags "READOUT_SHARED" and "READOUT_EXPORT" are set on Windows.
    #ifdef READOUT_SHARED
        #ifdef READOUT_EXPORT
            #define RL_API __declspec(dllexport)
        #else
            #define RL_API __declspec(dllimport)
        #endif
    #else
        // Disable definition if linking statically.
        #define RL_API
    #endif
#else
// Disable definition for non-Win32 systems.
#define RL_API
#endif

/// A single field in a parsed struct description
struct SchemaField {
  std::string name;
  std::string type;        // Canonical type name, e.g. "int32_t", "double"
  size_t offset;           // Byte offset within the struct
  size_t element_size;     // Size of one element in bytes
  size_t array_count;      // 0 = scalar, N = array of N elements
  size_t alignment;        // Required alignment for this type

  /// Total size of this field (element_size * max(array_count, 1))
  size_t total_size() const { return element_size * (array_count > 0 ? array_count : 1); }
};

/// Complete description of a parsed struct
class TypeSchema {
public:
  std::vector<SchemaField> fields;
  size_t total_size;       // Computed total size including padding
  std::string description; // Original description string

  TypeSchema() : total_size(0) {}
};

/// Exception thrown for parse errors
class TypeDescriptionError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

/// Parse a C struct type description string into a TypeSchema.
///
/// Accepted formats:
///   "struct { int x; double y; }"
///   "struct name { int x; double y; }"
///   "{ int x; double y; }"          (struct keyword optional)
///   "int x; double y;"              (braces optional)
///
/// Supported types:
///   char, short, int, long, long long (and unsigned variants)
///   float, double
///   int8_t, int16_t, int32_t, int64_t, uint8_t, uint16_t, uint32_t, uint64_t
///   size_t
///
/// Static arrays supported: "int arr[10];"
/// Pointers and nested structs are NOT supported.
///
/// \throws TypeDescriptionError on parse failure
RL_API TypeSchema parse_type_description(const std::string& description);
