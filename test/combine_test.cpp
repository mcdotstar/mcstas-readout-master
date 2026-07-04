// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//
// Round-trip tests for append_collector_files and concatenate_collector_files.
//
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <highfive/H5File.hpp>
#include <filesystem>
#include <string>
#include <vector>

#include <Readout.h>
#include <Structs.h>
#include <CollectorClass.h>
#include <reader.h>

#if defined(_MSC_VER) || defined(__MINGW32__)
#include <process.h>
static int combine_test_pid() { return _getpid(); }
#else
#include <unistd.h>
static int combine_test_pid() { return static_cast<int>(getpid()); }
#endif

static std::string combine_temp_h5(const std::string & tag) {
  namespace fs = std::filesystem;
  auto path = fs::temp_directory_path() / (tag + std::to_string(combine_test_pid()) + ".h5");
  return path.string();
}

/// Write a single-point CAEN collector file with parameter scan_val=val and `count` readouts
/// each with weight `weight`. Returns the filename.
static std::string write_scan_point(const std::string & tag, double scan_val,
                                    uint16_t count, double weight) {
  auto filename = combine_temp_h5(tag);
  std::filesystem::remove(filename);
  {
    Collector col(filename, "events", 0x34 /* BIFROST/CAEN */, 1u);
    col.addParameter("scan_val", scan_val,
                     std::optional<std::string>("arb"), std::nullopt);
    CAEN_readout_t data{};
    for (uint16_t i = 0; i < count; ++i) {
      data.channel = 3;
      data.a = i;
      col.addReadout(0, 0, static_cast<double>(i) * 0.001, weight, &data);
    }
  }
  return filename;
}

// ---- Test 1: concatenate three different-point files -------------------

TEST_CASE("concatenate_collector_files produces 3-point file in input order",
          "[combine][concatenate]") {
  const uint16_t N = 10;
  const double w = 2.5;

  auto file_a = write_scan_point("comb_cat_a_", 1.0, N, w);
  auto file_b = write_scan_point("comb_cat_b_", 2.0, N + 5, w * 2);
  auto file_c = write_scan_point("comb_cat_c_", 3.0, N + 10, w * 3);
  auto file_out = combine_temp_h5("comb_cat_out_");
  std::filesystem::remove(file_out);

  REQUIRE(concatenate_collector_files(file_out, {file_a, file_b, file_c}));

  {
    const ReaderSource source(file_out);
    REQUIRE(source.points() == 3);
    REQUIRE(source.readers().size() == 1);

    const auto & reader = source.reader("events");
    CHECK(reader.point_size(0) == N);
    CHECK(reader.point_size(1) == N + 5);
    CHECK(reader.point_size(2) == N + 10);

    REQUIRE(source.has_parameters());
    REQUIRE(source.parameter_is_double("scan_val"));
    CHECK(source.parameter_double_value("scan_val", 0) == Catch::Approx(1.0));
    CHECK(source.parameter_double_value("scan_val", 1) == Catch::Approx(2.0));
    CHECK(source.parameter_double_value("scan_val", 2) == Catch::Approx(3.0));

    // Verify per-point weights via HighFive (Reader has no weight accessor)
    auto hfile = HighFive::File(file_out, HighFive::File::ReadOnly);
    auto wds = hfile.getGroup("events")
                    .getDataSet(CollectorSink::weight_dataset_name());
    auto weights = wds.read<std::vector<double>>();
    REQUIRE(weights.size() == 3);
    CHECK(weights[0] == Catch::Approx(static_cast<double>(N) * w));
    CHECK(weights[1] == Catch::Approx(static_cast<double>(N + 5) * w * 2));
    CHECK(weights[2] == Catch::Approx(static_cast<double>(N + 10) * w * 3));
  }

  std::filesystem::remove(file_a);
  std::filesystem::remove(file_b);
  std::filesystem::remove(file_c);
  std::filesystem::remove(file_out);
}

// ---- Test 2: append two same-point files -------------------------------

TEST_CASE("append_collector_files combines identical-parameter files into one point",
          "[combine][append]") {
  const uint16_t N_a = 12;
  const uint16_t N_b = 8;
  const double w_a = 1.5;
  const double w_b = 2.0;

  // Both files must have IDENTICAL parameter values for append to succeed.
  auto file_a = write_scan_point("comb_app_a_", 5.0, N_a, w_a);
  auto file_b = write_scan_point("comb_app_b_", 5.0, N_b, w_b);
  auto file_out = combine_temp_h5("comb_app_out_");
  std::filesystem::remove(file_out);

  REQUIRE(append_collector_files(file_out, {file_a, file_b}, false));

  {
    const ReaderSource source(file_out);
    REQUIRE(source.points() == 1);
    REQUIRE(source.readers().size() == 1);

    const auto & reader = source.reader("events");
    CHECK(reader.point_size(0) == static_cast<size_t>(N_a + N_b));

    // Verify that the combined point weight equals the sum of both input weights
    auto hfile = HighFive::File(file_out, HighFive::File::ReadOnly);
    auto wds = hfile.getGroup("events")
                    .getDataSet(CollectorSink::weight_dataset_name());
    auto weights = wds.read<std::vector<double>>();
    REQUIRE(weights.size() == 1);
    const double expected = static_cast<double>(N_a) * w_a
                          + static_cast<double>(N_b) * w_b;
    CHECK(weights[0] == Catch::Approx(expected));
  }

  std::filesystem::remove(file_a);
  std::filesystem::remove(file_b);
  std::filesystem::remove(file_out);
}

// ---- Test 3: failure propagation ----------------------------------------

TEST_CASE("concatenate_collector_files returns false for nonexistent input",
          "[combine][failure]") {
  auto file_a = write_scan_point("comb_fail_a_", 1.0, 5, 1.0);
  auto file_out = combine_temp_h5("comb_fail_out_");
  std::filesystem::remove(file_out);

  const bool ok = concatenate_collector_files(
    file_out, {file_a, "/nonexistent_file_combine_test.h5"});
  CHECK(!ok);

  std::filesystem::remove(file_a);
  std::filesystem::remove(file_out);
}

TEST_CASE("append_collector_files returns false when parameters differ",
          "[combine][failure]") {
  // scan_val differs → identical-parameter check fails
  auto file_a = write_scan_point("comb_diff_a_", 1.0, 5, 1.0);
  auto file_b = write_scan_point("comb_diff_b_", 2.0, 5, 1.0);
  auto file_out = combine_temp_h5("comb_diff_out_");
  std::filesystem::remove(file_out);

  const bool ok = append_collector_files(file_out, {file_a, file_b}, false);
  CHECK(!ok);

  std::filesystem::remove(file_a);
  std::filesystem::remove(file_b);
  std::filesystem::remove(file_out);
}
