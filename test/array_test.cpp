#include <catch2/catch_test_macros.hpp>
#include "Array.h"
#include <cstring>

TEST_CASE("Array basic add and get", "[array]") {
  Array arr(sizeof(int));
  CHECK(arr.count() == 0);
  CHECK(arr.object_size() == sizeof(int));

  int val = 42;
  arr.add(&val);
  CHECK(arr.count() == 1);

  int out = 0;
  arr.get(0, &out);
  CHECK(out == 42);
}

TEST_CASE("Array multiple items preserve order", "[array]") {
  Array arr(sizeof(double));
  const int N = 100;
  for (int i = 0; i < N; ++i) {
    double v = static_cast<double>(i) * 1.5;
    arr.add(&v);
  }
  CHECK(arr.count() == static_cast<size_t>(N));

  for (int i = 0; i < N; ++i) {
    double out = 0;
    arr.get(static_cast<size_t>(i), &out);
    CHECK(out == static_cast<double>(i) * 1.5);
  }
}

TEST_CASE("Array get out-of-range throws", "[array]") {
  Array arr(4);
  CHECK_THROWS_AS(arr.get(0, nullptr), std::out_of_range);

  int val = 1;
  arr.add(&val);
  CHECK_THROWS_AS(arr.get(1, nullptr), std::out_of_range);
}

TEST_CASE("Array clear resets count", "[array]") {
  Array arr(sizeof(int));
  int val = 1;
  arr.add(&val);
  arr.add(&val);
  CHECK(arr.count() == 2);

  arr.clear();
  CHECK(arr.count() == 0);
}

TEST_CASE("Array with struct-sized objects", "[array]") {
  struct Point { double x, y, z; };
  Array arr(sizeof(Point));

  Point p1{1.0, 2.0, 3.0};
  Point p2{4.0, 5.0, 6.0};
  arr.add(&p1);
  arr.add(&p2);

  Point out{};
  arr.get(0, &out);
  CHECK(out.x == 1.0);
  CHECK(out.y == 2.0);
  CHECK(out.z == 3.0);

  arr.get(1, &out);
  CHECK(out.x == 4.0);
  CHECK(out.y == 5.0);
  CHECK(out.z == 6.0);
}

TEST_CASE("Array move semantics work", "[array]") {
  Array arr1(sizeof(int));
  int val = 99;
  arr1.add(&val);

  Array arr2(std::move(arr1));
  CHECK(arr2.count() == 1);

  int out = 0;
  arr2.get(0, &out);
  CHECK(out == 99);
}
