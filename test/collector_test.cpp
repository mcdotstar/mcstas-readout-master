#include <catch2/catch_test_macros.hpp>
#include <Readout.h>
#include <Structs.h>
#include <reader.h>
#include <filesystem>
#include <cstring>

#if defined(_MSC_VER) || defined(__MINGW32__)
#include <process.h>
static int test_pid() { return _getpid(); }
#else
#include <unistd.h>
static int test_pid() { return static_cast<int>(getpid()); }
#endif

static std::string temp_h5(const std::string& tag) {
  namespace fs = std::filesystem;
  auto path = fs::temp_directory_path() / (tag + std::to_string(test_pid()) + ".h5");
  return path.string();
}

// ---- Collector create/add/read for all detector types ----

TEST_CASE("Collector CAEN create, add, and read back", "[collector][CAEN]") {
  auto filename = temp_h5("col_caen_");
  const int type = 0x34; // BIFROST -> CAEN
  {
    auto col = collector_new(filename.c_str(), 0, 0, "events", type);
    REQUIRE(col != nullptr);
    CAEN_readout_t data{3, 100, 200, 0, 0};
    collector_add(col, 1, 0, 0.5, 1.0, &data);
    collector_add(col, 1, 0, 0.6, 2.0, &data);
    collector_free(col);
  }
  {
    Reader reader(filename);
    CHECK(reader.readout_type() == ReadoutType::CAEN);
    CHECK(reader.size() == 2);
    auto events = reader.get_CAEN(0, 2);
    CHECK(events.size() == 2);
    CHECK(events[0].channel == 3);
    CHECK(events[0].a == 100);
    CHECK(events[0].b == 200);
  }
  std::remove(filename.c_str());
}

TEST_CASE("Collector TTLMonitor create, add, and read back", "[collector][TTLMonitor]") {
  auto filename = temp_h5("col_ttl_");
  const int type = 0x10; // TTLMonitor
  {
    auto col = collector_new(filename.c_str(), 0, 0, "events", type);
    REQUIRE(col != nullptr);
    TTLMonitor_readout_t data{1, 5, 300};
    collector_add(col, 0, 10, 0.1, 1.0, &data);
    collector_free(col);
  }
  {
    Reader reader(filename);
    CHECK(reader.readout_type() == ReadoutType::TTLMonitor);
    CHECK(reader.size() == 1);
  }
  std::remove(filename.c_str());
}

TEST_CASE("Collector CDT create, add, and read back", "[collector][CDT]") {
  auto filename = temp_h5("col_cdt_");
  const int type = 0x60; // DREAM -> CDT
  {
    auto col = collector_new(filename.c_str(), 0, 0, "events", type);
    REQUIRE(col != nullptr);
    CDT_readout_t data{2, 10, 20};
    collector_add(col, 1, 2, 0.3, 1.0, &data);
    collector_add(col, 1, 2, 0.4, 1.0, &data);
    collector_add(col, 1, 2, 0.5, 1.0, &data);
    collector_free(col);
  }
  {
    Reader reader(filename);
    CHECK(reader.readout_type() == ReadoutType::CDT);
    CHECK(reader.size() == 3);
  }
  std::remove(filename.c_str());
}

TEST_CASE("Collector VMM3 create, add, and read back", "[collector][VMM3]") {
  auto filename = temp_h5("col_vmm3_");
  const int type = 0x40; // TREX -> VMM3
  {
    auto col = collector_new(filename.c_str(), 0, 0, "events", type);
    REQUIRE(col != nullptr);
    VMM3_readout_t data{42, 100, 3, 7, 1, 12};
    collector_add(col, 0, 1, 0.2, 1.0, &data);
    collector_free(col);
  }
  {
    Reader reader(filename);
    CHECK(reader.readout_type() == ReadoutType::VMM3);
    CHECK(reader.size() == 1);
  }
  std::remove(filename.c_str());
}

TEST_CASE("Collector BM0 create, add, and read back", "[collector][BM0]") {
  auto filename = temp_h5("col_bm0_");
  const int type = 0xf0; // CBM0 -> BM0
  {
    auto col = collector_new(filename.c_str(), 0, 0, "events", type);
    REQUIRE(col != nullptr);
    BM0_readout_t data{5};
    collector_add(col, 0, 0, 0.7, 1.0, &data);
    collector_free(col);
  }
  {
    Reader reader(filename);
    CHECK(reader.readout_type() == ReadoutType::BM0);
    CHECK(reader.size() == 1);
  }
  std::remove(filename.c_str());
}

TEST_CASE("Collector BM2 create, add, and read back", "[collector][BM2]") {
  auto filename = temp_h5("col_bm2_");
  const int type = 0x50; // BEER -> BM2
  {
    auto col = collector_new(filename.c_str(), 0, 0, "events", type);
    REQUIRE(col != nullptr);
    BM2_readout_t data{3, 150, 250};
    collector_add(col, 1, 1, 0.8, 1.0, &data);
    collector_free(col);
  }
  {
    Reader reader(filename);
    CHECK(reader.readout_type() == ReadoutType::BM2);
    CHECK(reader.size() == 1);
  }
  std::remove(filename.c_str());
}

TEST_CASE("Collector BMI create, add, and read back", "[collector][BMI]") {
  auto filename = temp_h5("col_bmi_");
  const int type = 0xfa; // CBMI -> BMI
  {
    auto col = collector_new(filename.c_str(), 0, 0, "events", type);
    REQUIRE(col != nullptr);
    BMI_readout_t data{2, 7, 0x00ABCD};
    collector_add(col, 0, 0, 0.9, 1.0, &data);
    collector_free(col);
  }
  {
    Reader reader(filename);
    CHECK(reader.readout_type() == ReadoutType::BMI);
    CHECK(reader.size() == 1);
  }
  std::remove(filename.c_str());
}

// ---- Filename construction ----

TEST_CASE("collector_construct_filename builds expected path", "[collector][filename]") {
  int size = collector_construct_filename_size("/tmp", "test_output");
  REQUIRE(size > 0);

  std::vector<char> buf(static_cast<size_t>(size));
  collector_construct_filename("/tmp", "test_output", buf.data());

  std::string result(buf.data());
  CHECK(result.find("/tmp") != std::string::npos);
  CHECK(result.find("test_output") != std::string::npos);
  CHECK(result.find(".h5") != std::string::npos);
}

TEST_CASE("collector_mpi_node_filename builds node-specific paths", "[collector][filename][mpi]") {
  const int total_nodes = 4;
  for (int i = 0; i < total_nodes; ++i) {
    int size = collector_mpi_node_filename_size("/tmp", "mpi_test", i, total_nodes);
    REQUIRE(size > 0);

    std::vector<char> buf(static_cast<size_t>(size));
    collector_mpi_node_filename("/tmp", "mpi_test", buf.data(), i, total_nodes);

    std::string result(buf.data());
    CHECK(result.find("/tmp") != std::string::npos);
    CHECK(result.find("mpi_test") != std::string::npos);
  }
}

TEST_CASE("collector_mpi_node_filenames builds all node paths", "[collector][filename][mpi]") {
  const int total_nodes = 3;
  std::vector<int> sizes(static_cast<size_t>(total_nodes));
  int total = collector_mpi_node_filename_sizes("/tmp", "batch", total_nodes, sizes.data());
  REQUIRE(total > 0);

  // Allocate and fill all filenames
  std::vector<std::vector<char>> bufs;
  std::vector<char*> ptrs;
  for (int i = 0; i < total_nodes; ++i) {
    bufs.emplace_back(static_cast<size_t>(sizes[i]));
    ptrs.push_back(bufs.back().data());
  }
  collector_mpi_node_filenames("/tmp", "batch", ptrs.data(), total_nodes);

  // Each filename should be unique
  std::set<std::string> unique_names;
  for (int i = 0; i < total_nodes; ++i) {
    unique_names.insert(std::string(ptrs[i]));
  }
  CHECK(unique_names.size() == static_cast<size_t>(total_nodes));
}

// ---- Point-based collector ----

TEST_CASE("Collector with scan points creates grouped output", "[collector][points]") {
  auto filename = temp_h5("col_points_");
  const int type = 0x34;
  {
    // Write two scan points
    auto col0 = collector_new(filename.c_str(), 0, 3, "events", type);
    CAEN_readout_t data{1, 10, 20, 0, 0};
    collector_add(col0, 0, 0, 0.1, 1.0, &data);
    collector_free(col0);
  }
  {
    auto col1 = collector_new(filename.c_str(), 1, 3, "events", type);
    CAEN_readout_t data{2, 30, 40, 0, 0};
    collector_add(col1, 0, 0, 0.2, 1.0, &data);
    collector_add(col1, 0, 0, 0.3, 1.0, &data);
    collector_free(col1);
  }
  // Verify the HDF5 file has both point groups
  {
    HighFive::File file(filename, HighFive::File::ReadOnly);
    CHECK(file.exist("point_0"));
    CHECK(file.exist("point_1"));
    auto g0 = file.getGroup("point_0");
    auto g1 = file.getGroup("point_1");
    CHECK(g0.getDataSet("events").getDimensions()[0] == 1);
    CHECK(g1.getDataSet("events").getDimensions()[0] == 2);
  }
  std::remove(filename.c_str());
}
