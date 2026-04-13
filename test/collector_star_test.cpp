#include <catch2/catch_test_macros.hpp>
#include "TypeDescriptionParser.h"
#include "CollectorStar.h"
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

// ---- CollectorStar tests ----

TEST_CASE("CollectorStar description-only mode", "[collector_star]") {
  CollectorStar cs("struct { int x; double y; }", "test_dataset");
  struct TestStruct { int x; double y; };

  CHECK(cs.has_schema());
  CHECK(cs.object_size() == sizeof(TestStruct));
  CHECK(cs.count() == 0);

  TestStruct obj{42, 3.14};
  cs.add(&obj);
  CHECK(cs.count() == 1);

  TestStruct out{};
  cs.get(0, &out);
  CHECK(out.x == 42);
  CHECK(out.y == 3.14);
}

TEST_CASE("CollectorStar opaque mode", "[collector_star]") {
  struct Blob { char data[16]; };
  CollectorStar cs(sizeof(Blob), "opaque_dataset");

  CHECK_FALSE(cs.has_schema());
  CHECK(cs.object_size() == 16);

  Blob b;
  std::memset(&b, 0xAB, sizeof(b));
  cs.add(&b);

  Blob out{};
  cs.get(0, &out);
  CHECK(std::memcmp(&b, &out, sizeof(Blob)) == 0);
}

TEST_CASE("CollectorStar validated mode succeeds on match", "[collector_star]") {
  struct TestStruct { uint8_t a; uint16_t b; };
  REQUIRE_NOTHROW(
    CollectorStar("struct { uint8_t a; uint16_t b; }", sizeof(TestStruct), "validated")
  );
}

TEST_CASE("CollectorStar validated mode fails on mismatch", "[collector_star]") {
  CHECK_THROWS_AS(
    CollectorStar("struct { uint8_t a; uint16_t b; }", 999, "bad"),
    std::invalid_argument
  );
}

TEST_CASE("CollectorStar get out of range", "[collector_star]") {
  CollectorStar cs(4, "test");
  CHECK_THROWS_AS(cs.get(0, nullptr), std::out_of_range);
}

TEST_CASE("CollectorStar multiple adds and retrieval", "[collector_star]") {
  CollectorStar cs("struct { int32_t id; double val; }", "multi");
  struct Item { int32_t id; double val; };

  for (int i = 0; i < 100; ++i) {
    Item item{i, static_cast<double>(i) * 1.5};
    cs.add(&item);
  }
  CHECK(cs.count() == 100);

  Item out{};
  cs.get(50, &out);
  CHECK(out.id == 50);
  CHECK(out.val == 75.0);

  cs.get(99, &out);
  CHECK(out.id == 99);
}

TEST_CASE("CollectorStar write HDF5 with schema", "[collector_star][hdf5]") {
  struct Event { uint8_t ring; double time; float weight; };
  const std::string filename = "test_collector_star_schema.h5";
  {
    CollectorStar cs("struct { uint8_t ring; double time; float weight; }", sizeof(Event), "events");
    Event e1{1, 0.001, 1.0f};
    Event e2{2, 0.002, 0.5f};
    cs.add(&e1);
    cs.add(&e2);
    cs.write_hdf5(filename);
  }
  // Verify the file was created and is readable
  {
    HighFive::File file(filename, HighFive::File::ReadOnly);
    CHECK(file.exist("events"));
    auto dataset = file.getDataSet("events");
    auto dims = dataset.getDimensions();
    CHECK(dims.size() == 1);
    CHECK(dims[0] == 2);
  }
  std::remove(filename.c_str());
}

TEST_CASE("CollectorStar write HDF5 opaque", "[collector_star][hdf5]") {
  const std::string filename = "test_collector_star_opaque.h5";
  {
    CollectorStar cs(8, "raw_data");
    uint64_t val = 0xDEADBEEF;
    cs.add(&val);
    cs.write_hdf5(filename);
  }
  {
    HighFive::File file(filename, HighFive::File::ReadOnly);
    CHECK(file.exist("raw_data"));
  }
  std::remove(filename.c_str());
}
