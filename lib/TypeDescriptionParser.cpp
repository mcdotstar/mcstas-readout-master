// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implementation of the C struct type description parser
///
//===----------------------------------------------------------------------===//

#include "TypeDescriptionParser.h"
#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <cctype>
#include <cstdint>

namespace {

struct TypeInfo {
  std::string canonical_name;
  size_t size;
  size_t alignment;
};

// Use sizeof/alignof so sizes match the actual compilation target
const std::unordered_map<std::string, TypeInfo>& type_map() {
  static const std::unordered_map<std::string, TypeInfo> map = {
    // Fixed-width types
    {"int8_t",    {"int8_t",    sizeof(int8_t),    alignof(int8_t)}},
    {"int16_t",   {"int16_t",   sizeof(int16_t),   alignof(int16_t)}},
    {"int32_t",   {"int32_t",   sizeof(int32_t),   alignof(int32_t)}},
    {"int64_t",   {"int64_t",   sizeof(int64_t),   alignof(int64_t)}},
    {"uint8_t",   {"uint8_t",   sizeof(uint8_t),   alignof(uint8_t)}},
    {"uint16_t",  {"uint16_t",  sizeof(uint16_t),  alignof(uint16_t)}},
    {"uint32_t",  {"uint32_t",  sizeof(uint32_t),  alignof(uint32_t)}},
    {"uint64_t",  {"uint64_t",  sizeof(uint64_t),  alignof(uint64_t)}},
    // Standard types — canonical names are the fixed-width equivalents at the same size
    {"char",              {"int8_t",    sizeof(char),               alignof(char)}},
    {"signed char",       {"int8_t",    sizeof(signed char),        alignof(signed char)}},
    {"unsigned char",     {"uint8_t",   sizeof(unsigned char),      alignof(unsigned char)}},
    {"short",             {"int16_t",   sizeof(short),              alignof(short)}},
    {"short int",         {"int16_t",   sizeof(short int),          alignof(short int)}},
    {"signed short",      {"int16_t",   sizeof(signed short),       alignof(signed short)}},
    {"signed short int",  {"int16_t",   sizeof(signed short int),   alignof(signed short int)}},
    {"unsigned short",    {"uint16_t",  sizeof(unsigned short),     alignof(unsigned short)}},
    {"unsigned short int",{"uint16_t",  sizeof(unsigned short int), alignof(unsigned short int)}},
    {"int",               {"int32_t",   sizeof(int),                alignof(int)}},
    {"signed",            {"int32_t",   sizeof(signed),             alignof(signed)}},
    {"signed int",        {"int32_t",   sizeof(signed int),         alignof(signed int)}},
    {"unsigned",          {"uint32_t",  sizeof(unsigned),           alignof(unsigned)}},
    {"unsigned int",      {"uint32_t",  sizeof(unsigned int),       alignof(unsigned int)}},
    {"long",              {"int64_t",   sizeof(long),               alignof(long)}},
    {"long int",          {"int64_t",   sizeof(long int),           alignof(long int)}},
    {"signed long",       {"int64_t",   sizeof(signed long),        alignof(signed long)}},
    {"signed long int",   {"int64_t",   sizeof(signed long int),    alignof(signed long int)}},
    {"unsigned long",     {"uint64_t",  sizeof(unsigned long),      alignof(unsigned long)}},
    {"unsigned long int", {"uint64_t",  sizeof(unsigned long int),  alignof(unsigned long int)}},
    {"long long",              {"int64_t",  sizeof(long long),               alignof(long long)}},
    {"long long int",          {"int64_t",  sizeof(long long int),           alignof(long long int)}},
    {"signed long long",       {"int64_t",  sizeof(signed long long),        alignof(signed long long)}},
    {"signed long long int",   {"int64_t",  sizeof(signed long long int),    alignof(signed long long int)}},
    {"unsigned long long",     {"uint64_t", sizeof(unsigned long long),      alignof(unsigned long long)}},
    {"unsigned long long int", {"uint64_t", sizeof(unsigned long long int),  alignof(unsigned long long int)}},
    // Floating point
    {"float",    {"float",  sizeof(float),  alignof(float)}},
    {"double",   {"double", sizeof(double), alignof(double)}},
    // size_t
    {"size_t",   {"uint64_t", sizeof(size_t), alignof(size_t)}},
  };
  return map;
}

std::string trim(const std::string& s) {
  auto start = s.find_first_not_of(" \t\n\r");
  if (start == std::string::npos) return "";
  auto end = s.find_last_not_of(" \t\n\r");
  return s.substr(start, end - start + 1);
}

/// Strip outer struct keyword and braces, returning just the field declarations
std::string extract_body(const std::string& desc) {
  std::string s = trim(desc);
  // Remove leading "struct" keyword
  if (s.size() >= 6 && s.substr(0, 6) == "struct") {
    s = trim(s.substr(6));
    // Skip optional struct name (identifier before '{')
    if (!s.empty() && s[0] != '{') {
      auto brace = s.find('{');
      if (brace != std::string::npos) {
        s = trim(s.substr(brace));
      }
    }
  }
  // Remove braces if present
  if (!s.empty() && s.front() == '{') {
    auto close = s.rfind('}');
    if (close != std::string::npos) {
      s = s.substr(1, close - 1);
    } else {
      throw TypeDescriptionError("Unmatched opening brace in type description");
    }
  }
  return trim(s);
}

std::vector<std::string> split_declarations(const std::string& body) {
  std::vector<std::string> decls;
  std::istringstream ss(body);
  std::string token;
  while (std::getline(ss, token, ';')) {
    auto t = trim(token);
    if (!t.empty()) {
      decls.push_back(t);
    }
  }
  return decls;
}

bool is_valid_identifier(const std::string& s) {
  if (s.empty()) return false;
  if (!std::isalpha(static_cast<unsigned char>(s[0])) && s[0] != '_') return false;
  for (size_t i = 1; i < s.size(); ++i) {
    if (!std::isalnum(static_cast<unsigned char>(s[i])) && s[i] != '_') return false;
  }
  return true;
}

/// Parse a single field declaration like "uint16_t values[10]" or "unsigned long long x"
SchemaField parse_field(const std::string& decl) {
  // Tokenize on whitespace, preserving bracket expressions
  std::vector<std::string> tokens;
  std::string current;
  for (char c : decl) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!current.empty()) {
        tokens.push_back(current);
        current.clear();
      }
    } else if (c == '[') {
      if (!current.empty()) {
        tokens.push_back(current);
        current.clear();
      }
      current += c;
    } else if (c == ']') {
      current += c;
      tokens.push_back(current);
      current.clear();
    } else {
      current += c;
    }
  }
  if (!current.empty()) tokens.push_back(current);

  if (tokens.size() < 2) {
    throw TypeDescriptionError("Field declaration needs at least a type and name: '" + decl + "'");
  }

  // Detect array specifier — could be last token "[N]" or fused with name "name[N]"
  size_t array_count = 0;
  size_t name_idx = tokens.size() - 1;

  if (tokens.back().front() == '[' && tokens.back().back() == ']') {
    std::string arr = tokens.back().substr(1, tokens.back().size() - 2);
    long long val = 0;
    try { val = std::stoll(arr); } catch (...) {
      throw TypeDescriptionError("Invalid array size '" + arr + "' in: '" + decl + "'");
    }
    if (val <= 0) throw TypeDescriptionError("Array size must be positive: '" + decl + "'");
    array_count = static_cast<size_t>(val);
    name_idx = tokens.size() - 2;
  }

  // Check for array spec fused with identifier, e.g. "values[10]"
  if (array_count == 0) {
    auto& maybe_name = tokens[name_idx];
    auto bracket = maybe_name.find('[');
    if (bracket != std::string::npos) {
      auto close = maybe_name.find(']', bracket);
      if (close != std::string::npos) {
        std::string arr = maybe_name.substr(bracket + 1, close - bracket - 1);
        long long val = 0;
        try { val = std::stoll(arr); } catch (...) {
          throw TypeDescriptionError("Invalid array size '" + arr + "' in: '" + decl + "'");
        }
        if (val <= 0) throw TypeDescriptionError("Array size must be positive: '" + decl + "'");
        array_count = static_cast<size_t>(val);
        maybe_name = maybe_name.substr(0, bracket);
      }
    }
  }

  std::string field_name = tokens[name_idx];
  if (!is_valid_identifier(field_name)) {
    throw TypeDescriptionError("Invalid field name '" + field_name + "' in: '" + decl + "'");
  }

  // Everything before the name is the type specifier
  std::string type_str;
  for (size_t i = 0; i < name_idx; ++i) {
    if (!type_str.empty()) type_str += " ";
    type_str += tokens[i];
  }

  if (type_str.empty()) {
    throw TypeDescriptionError("Missing type in field declaration: '" + decl + "'");
  }

  if (type_str.find('*') != std::string::npos || field_name.find('*') != std::string::npos) {
    throw TypeDescriptionError("Pointer types are not supported: '" + decl + "'");
  }

  auto& tmap = type_map();
  auto it = tmap.find(type_str);
  if (it == tmap.end()) {
    throw TypeDescriptionError("Unrecognized type '" + type_str + "' in: '" + decl + "'");
  }

  SchemaField field;
  field.name = field_name;
  field.type = it->second.canonical_name;
  field.element_size = it->second.size;
  field.alignment = it->second.alignment;
  field.array_count = array_count;
  field.offset = 0;
  return field;
}

/// Compute field offsets using standard C struct layout rules (natural alignment)
void compute_layout(TypeSchema& schema) {
  size_t current_offset = 0;
  size_t max_alignment = 1;

  for (auto& field : schema.fields) {
    size_t remainder = current_offset % field.alignment;
    if (remainder != 0) {
      current_offset += field.alignment - remainder;
    }
    field.offset = current_offset;
    current_offset += field.total_size();
    if (field.alignment > max_alignment) {
      max_alignment = field.alignment;
    }
  }

  // Trailing padding for overall struct alignment
  size_t remainder = current_offset % max_alignment;
  if (remainder != 0) {
    current_offset += max_alignment - remainder;
  }
  schema.total_size = current_offset;
}

} // anonymous namespace


TypeSchema parse_type_description(const std::string& description) {
  if (description.empty()) {
    throw TypeDescriptionError("Empty type description");
  }

  TypeSchema schema;
  schema.description = description;

  std::string body = extract_body(description);
  if (body.empty()) {
    throw TypeDescriptionError("No fields found in type description: '" + description + "'");
  }

  auto declarations = split_declarations(body);
  if (declarations.empty()) {
    throw TypeDescriptionError("No fields found in type description: '" + description + "'");
  }

  for (const auto& decl : declarations) {
    schema.fields.push_back(parse_field(decl));
  }

  compute_layout(schema);
  return schema;
}
