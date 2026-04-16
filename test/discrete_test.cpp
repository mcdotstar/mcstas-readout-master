#include <catch2/catch_test_macros.hpp>
#include "DiscreteWhile.h"
#include "DiscreteAfter.h"
#include "IndexSampler.h"
#include <cstring>
#include <numeric>
#include <algorithm>

// ---- DiscreteWhile tests ----

TEST_CASE("DiscreteWhile basic operation", "[discrete][while]") {
  const size_t sample_count = 5;
  DiscreteWhile dw(sizeof(int), "test", sample_count, 42);
  CHECK(dw.object_size() == sizeof(int));
  CHECK(dw.filling() == 1);

  // Feed more items than samples
  for (int i = 0; i < 20; ++i) {
    dw.fit(&i, 1.0);
  }
  CHECK(dw.filling() == 0);

  auto result = dw.value();
  CHECK(result.size() == sample_count * sizeof(int));
}

TEST_CASE("DiscreteWhile value throws when filling", "[discrete][while]") {
  DiscreteWhile dw(sizeof(int), "test", 10, 42);
  // No data added — still filling
  CHECK_THROWS_AS(dw.value(), std::logic_error);

  // Add fewer items than requested samples
  int val = 1;
  dw.fit(&val, 1.0);
  CHECK(dw.filling() == 1);
  CHECK_THROWS_AS(dw.value(), std::logic_error);
}

TEST_CASE("DiscreteWhile with struct description", "[discrete][while]") {
  struct Item { int id; double val; };
  DiscreteWhile dw("struct { int id; double val; }", sizeof(Item), "items", 3, 42);
  CHECK(dw.object_size() == sizeof(Item));

  for (int i = 0; i < 10; ++i) {
    Item item{i, static_cast<double>(i) * 0.5};
    dw.fit(&item, 1.0);
  }
  CHECK(dw.filling() == 0);

  Item out{};
  dw.value(0, &out);
  // Just verify it returns valid data (reservoir sampling is stochastic)
  CHECK(out.id >= 0);
  CHECK(out.id < 10);
}

TEST_CASE("DiscreteWhile per-index retrieval", "[discrete][while]") {
  DiscreteWhile dw(sizeof(int), "test", 3, 42);
  for (int i = 0; i < 10; ++i) {
    dw.fit(&i, 1.0);
  }

  int out = 0;
  dw.value(0, &out);
  CHECK(out >= 0);
  CHECK(out < 10);

  dw.value(1, &out);
  CHECK(out >= 0);
  CHECK(out < 10);

  dw.value(2, &out);
  CHECK(out >= 0);
  CHECK(out < 10);
}

// ---- DiscreteAfter tests ----

TEST_CASE("DiscreteAfter requires sample() before value()", "[discrete][after]") {
  DiscreteAfter da(sizeof(int), "test");

  int val = 1;
  da.fit(&val, 1.0);

  CHECK_THROWS_AS(da.value(), std::logic_error);
}

TEST_CASE("DiscreteAfter basic collect-then-sample workflow", "[discrete][after]") {
  DiscreteAfter da(sizeof(int), "test");

  // Collect data
  for (int i = 0; i < 20; ++i) {
    da.fit(&i, 1.0);
  }

  // Sample a fraction
  size_t n = da.sample(0.5, 42);
  CHECK(n == 10);  // 20 items * weight 1.0 * fraction 0.5 = 10

  da.oversample();
  CHECK(da.filling() == 0);

  auto result = da.value();
  CHECK(result.size() == n * sizeof(int));
}

TEST_CASE("DiscreteAfter per-index retrieval", "[discrete][after]") {
  DiscreteAfter da(sizeof(int), "test");

  for (int i = 0; i < 100; ++i) {
    da.fit(&i, 1.0);
  }

  da.sample(0.1, 42);
  da.oversample();

  int out = 0;
  da.value(0, &out);
  CHECK(out >= 0);
  CHECK(out < 100);
}

TEST_CASE("DiscreteAfter with struct description", "[discrete][after]") {
  struct Event { uint8_t ring; double time; };
  DiscreteAfter da("struct { uint8_t ring; double time; }", sizeof(Event), "events");

  for (int i = 0; i < 50; ++i) {
    Event e{static_cast<uint8_t>(i % 4), static_cast<double>(i) * 0.01};
    da.fit(&e, 1.0);
  }

  da.sample(0.2, 42);
  da.oversample();

  Event out{};
  da.value(0, &out);
  CHECK(out.ring < 4);
  CHECK(out.time >= 0.0);
  CHECK(out.time < 0.50);
}

TEST_CASE("DiscreteAfter sample with no data returns 0", "[discrete][after]") {
  DiscreteAfter da(sizeof(int), "empty");
  CHECK(da.sample(1.0, 42) == 0u);
}

TEST_CASE("DiscreteAfter oversample requires sample first", "[discrete][after]") {
  DiscreteAfter da(sizeof(int), "test");
  CHECK_THROWS_AS(da.oversample(), std::logic_error);
}

// ---- IndexSampler tests ----

TEST_CASE("IndexSampler basic operation", "[discrete][sampler]") {
  IndexSampler sampler(5, 42);
  CHECK(sampler.filling() == 1);

  for (size_t i = 0; i < 20; ++i) {
    sampler.fit(i, 1.0);
  }
  CHECK(sampler.filling() == 0);

  auto values = sampler.value();
  CHECK(values.size() == 5);
  for (auto v : values) {
    CHECK(v < 20);
  }
}

TEST_CASE("IndexSampler per-index retrieval", "[discrete][sampler]") {
  IndexSampler sampler(3, 42);
  for (size_t i = 0; i < 10; ++i) {
    sampler.fit(i, 1.0);
  }

  CHECK(sampler.value(0) < 10);
  CHECK(sampler.value(1) < 10);
  CHECK(sampler.value(2) < 10);
}

TEST_CASE("IndexSampler throws when filling", "[discrete][sampler]") {
  IndexSampler sampler(10, 42);
  CHECK_THROWS_AS(sampler.value(), std::logic_error);
  CHECK_THROWS_AS(sampler.value(0), std::logic_error);
}

TEST_CASE("IndexSampler weighted items bias selection", "[discrete][sampler]") {
  // One very heavy item should dominate the reservoir
  IndexSampler sampler(5, 42);
  sampler.fit(0, 1000.0);  // very heavy
  for (size_t i = 1; i < 20; ++i) {
    sampler.fit(i, 0.001);  // very light
  }

  auto values = sampler.value();
  size_t zero_count = 0;
  for (auto v : values) {
    if (v == 0) ++zero_count;
  }
  // With such extreme weights, item 0 should appear in most slots
  CHECK(zero_count >= 3);
}
