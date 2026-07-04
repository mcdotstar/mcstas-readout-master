#include <catch2/catch_test_macros.hpp>
#include "TypeDescriptionParser.h"
#include <cstring>
#include <cstdint>

// ---- TypeDescriptionParser tests ----

TEST_CASE("Parse basic struct with simple types", "[parser]") {
  auto schema = parse_type_description("struct { int x; double y; }");
  REQUIRE(schema.fields.size() == 2);

  CHECK(schema.fields[0].name == "x");
  CHECK(schema.fields[0].type == "int32_t");
  CHECK(schema.fields[0].element_size == sizeof(int));
  CHECK(schema.fields[0].array_count == 0);
  CHECK(schema.fields[0].offset == 0);

  CHECK(schema.fields[1].name == "y");
  CHECK(schema.fields[1].type == "double");
  CHECK(schema.fields[1].element_size == sizeof(double));
  CHECK(schema.fields[1].array_count == 0);
}

TEST_CASE("Parse struct with fixed-width types", "[parser]") {
  auto schema = parse_type_description("struct { uint8_t ring; uint16_t a; uint32_t b; uint64_t c; }");
  REQUIRE(schema.fields.size() == 4);
  CHECK(schema.fields[0].element_size == 1);
  CHECK(schema.fields[1].element_size == 2);
  CHECK(schema.fields[2].element_size == 4);
  CHECK(schema.fields[3].element_size == 8);
}

TEST_CASE("Parse struct with static arrays", "[parser]") {
  auto schema = parse_type_description("struct { int counts[10]; double energy; }");
  REQUIRE(schema.fields.size() == 2);

  CHECK(schema.fields[0].name == "counts");
  CHECK(schema.fields[0].array_count == 10);
  CHECK(schema.fields[0].element_size == sizeof(int));
  CHECK(schema.fields[0].total_size() == 10 * sizeof(int));

  CHECK(schema.fields[1].name == "energy");
  CHECK(schema.fields[1].array_count == 0);
}

TEST_CASE("Array count 0 vs 1 distinction", "[parser]") {
  auto schema = parse_type_description("struct { int scalar; int arr[1]; }");
  REQUIRE(schema.fields.size() == 2);
  CHECK(schema.fields[0].array_count == 0);  // scalar
  CHECK(schema.fields[1].array_count == 1);  // explicit 1-element array
}

TEST_CASE("Parse struct with array bracket attached to name", "[parser]") {
  auto schema = parse_type_description("struct { float values[5]; }");
  REQUIRE(schema.fields.size() == 1);
  CHECK(schema.fields[0].name == "values");
  CHECK(schema.fields[0].array_count == 5);
}

TEST_CASE("Parse struct with separated array bracket", "[parser]") {
  auto schema = parse_type_description("struct { float values [5]; }");
  REQUIRE(schema.fields.size() == 1);
  CHECK(schema.fields[0].name == "values");
  CHECK(schema.fields[0].array_count == 5);
}

TEST_CASE("Parse compound type specifiers", "[parser]") {
  auto schema = parse_type_description("struct { unsigned int x; unsigned char y; }");
  REQUIRE(schema.fields.size() == 2);
  CHECK(schema.fields[0].type == "uint32_t");
  CHECK(schema.fields[1].type == "uint8_t");
}

TEST_CASE("Parse without struct keyword", "[parser]") {
  auto schema = parse_type_description("{ int x; double y; }");
  REQUIRE(schema.fields.size() == 2);
  CHECK(schema.fields[0].name == "x");
  CHECK(schema.fields[1].name == "y");
}

TEST_CASE("Parse without braces", "[parser]") {
  auto schema = parse_type_description("int x; double y;");
  REQUIRE(schema.fields.size() == 2);
}

TEST_CASE("Parse named struct", "[parser]") {
  auto schema = parse_type_description("struct my_data { int x; float y; }");
  REQUIRE(schema.fields.size() == 2);
}

TEST_CASE("Struct layout: alignment and padding", "[parser]") {
  // struct { uint8_t a; double b; } should have padding between a and b
  auto schema = parse_type_description("struct { uint8_t a; double b; }");
  REQUIRE(schema.fields.size() == 2);
  CHECK(schema.fields[0].offset == 0);
  // b should be aligned to alignof(double)
  CHECK(schema.fields[1].offset == alignof(double));
  // Total size includes trailing padding
  struct ref { uint8_t a; double b; };
  CHECK(schema.total_size == sizeof(ref));
}

TEST_CASE("Struct layout matches real CAEN_readout", "[parser]") {
  // Match the existing CAEN_readout struct
  auto schema = parse_type_description(
    "struct { uint8_t channel; uint16_t a; uint16_t b; uint16_t c; uint16_t d; }"
  );
  struct CAEN_ref { uint8_t channel; uint16_t a, b, c, d; };
  CHECK(schema.total_size == sizeof(CAEN_ref));
}

TEST_CASE("Parser rejects pointers", "[parser]") {
  CHECK_THROWS_AS(
    parse_type_description("struct { int* p; }"),
    TypeDescriptionError
  );
}

TEST_CASE("Parser rejects empty description", "[parser]") {
  CHECK_THROWS_AS(parse_type_description(""), TypeDescriptionError);
}

TEST_CASE("Parser rejects unknown types", "[parser]") {
  CHECK_THROWS_AS(
    parse_type_description("struct { my_custom_type x; }"),
    TypeDescriptionError
  );
}

TEST_CASE("Parser rejects zero-size arrays", "[parser]") {
  CHECK_THROWS_AS(
    parse_type_description("struct { int arr[0]; }"),
    TypeDescriptionError
  );
}

TEST_CASE("Parser rejects negative array size", "[parser]") {
  CHECK_THROWS_AS(
    parse_type_description("struct { int arr[-1]; }"),
    TypeDescriptionError
  );
}
