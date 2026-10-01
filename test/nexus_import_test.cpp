// Tests of the record-description field API used by the McStas components' record
// sinks, and of importing readout records from a McStas NeXus output file.
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include <catch2/catch_test_macros.hpp>

#include <highfive/H5File.hpp>
#include <hdf5.h>

#include <readout_collector.h>
#include <TypeDescriptionParser.h>
#include <nexus_import.h>
#include <reader.h>

#if defined(_MSC_VER) || defined(__MINGW32__)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

std::filesystem::path temp_path(const std::string & base) {
#if defined(_MSC_VER) || defined(__MINGW32__)
  const int pid = _getpid();
#else
  const int pid = static_cast<int>(getpid());
#endif
  return std::filesystem::temp_directory_path() / (base + std::to_string(pid) + ".h5");
}

/// A fixed-length, null-terminated string attribute of exactly strlen characters, as NAPI writes NX_CHAR
void napi_string_attribute(hid_t object, const char * name, const std::string & value) {
  const hid_t type = H5Tcopy(H5T_C_S1);
  H5Tset_size(type, value.size());
  H5Tset_strpad(type, H5T_STR_NULLTERM);
  const hid_t space = H5Screate(H5S_SCALAR);
  const hid_t attr = H5Acreate2(object, name, type, space, H5P_DEFAULT, H5P_DEFAULT);
  H5Awrite(attr, type, value.data());
  H5Aclose(attr);
  H5Sclose(space);
  H5Tclose(type);
}

/// A one-element fixed-length string dataset, as NAPI writes nxprintf values
void napi_string_dataset(hid_t group, const char * name, const std::string & value) {
  const hid_t type = H5Tcopy(H5T_C_S1);
  H5Tset_size(type, value.size());
  H5Tset_strpad(type, H5T_STR_NULLTERM);
  const hsize_t dims[1] = {1};
  const hid_t space = H5Screate_simple(1, dims, nullptr);
  const hid_t ds = H5Dcreate2(group, name, type, space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  H5Dwrite(ds, type, H5S_ALL, H5S_ALL, H5P_DEFAULT, value.data());
  H5Dclose(ds);
  H5Sclose(space);
  H5Tclose(type);
}

} // namespace

TEST_CASE("Record description fields match the parser", "[description_fields]") {
  for (const int ess_type : {0x34, 0xf0}) {
    const char * description = readout_description_for(ess_type);
    REQUIRE(description != nullptr);
    const auto schema = parse_type_description(description);
    std::vector<readout_field_t> fields(32);
    const int n = readout_description_fields(description, fields.data(), static_cast<int>(fields.size()));
    REQUIRE(n == static_cast<int>(schema.fields.size()));
    REQUIRE(readout_description_size(description) == schema.total_size);
    bool has_weight = false;
    for (int i = 0; i < n; ++i) {
      const auto & f = schema.fields[static_cast<size_t>(i)];
      REQUIRE(std::string(fields[static_cast<size_t>(i)].name) == f.name);
      REQUIRE(std::string(fields[static_cast<size_t>(i)].type) == f.type);
      REQUIRE(fields[static_cast<size_t>(i)].offset == f.offset);
      REQUIRE(fields[static_cast<size_t>(i)].element_size == f.element_size);
      REQUIRE(fields[static_cast<size_t>(i)].count == 1);
      if (f.name == "weight" && f.type == "double") has_weight = true;
    }
    REQUIRE(has_weight);
  }
  // arrays, truncation and errors
  readout_field_t two[2];
  REQUIRE(readout_description_fields("uint8_t a; double b[3]; int32_t c;", two, 2) == 3);
  REQUIRE(std::string(two[1].name) == "b");
  REQUIRE(two[1].count == 3);
  REQUIRE(two[1].offset == 8);
  REQUIRE(readout_description_fields("not a description", two, 2) == -1);
  REQUIRE(readout_description_fields(nullptr, nullptr, 0) == -1);
  REQUIRE(readout_description_size("double x; uint8_t y;") == 16);
  REQUIRE(std::string(readout_detector_name(0x34)) == "DetectorType::BIFROST");
}

TEST_CASE("Import readout records from a McStas NeXus file", "[nexus_import]") {
  const auto nexus = temp_path("nexus_import_in_");
  const auto out = temp_path("nexus_import_out_");
  std::filesystem::remove(nexus);
  std::filesystem::remove(out);

  // The CAEN record layout, written column-wise as the component sink does
  const int ess_type = 0x34;
  const std::string description = readout_description_for(ess_type);
  const uint64_t normalization = 1000;
  const std::vector<uint8_t> ring{1, 1, 2}, fen{0, 0, 1}, channel{3, 4, 5};
  const std::vector<double> time{0.001, 0.002, 0.003}, weight{0.5, 1.5, 2.5};
  const std::vector<uint16_t> a{10, 20, 30}, b{11, 21, 31}, c{0, 0, 0}, d{0, 0, 0};
  {
    HighFive::File file(nexus.string(), HighFive::File::Create);
    auto entry = file.createGroup("entry1");
    auto instrument = entry.createGroup("instrument");
    napi_string_attribute(instrument.getId(), "Parameters", " lambda(double) bins(int) tag(string)");
    auto param = entry.createGroup("simulation").createGroup("Param");
    napi_string_dataset(param.getId(), "lambda", "4.5");
    napi_string_dataset(param.getId(), "bins", "7");
    napi_string_dataset(param.getId(), "tag", "test run");
    auto comp = instrument.createGroup("components").createGroup("0003_collect");
    napi_string_dataset(comp.getId(), "Component_type", "CollectorCAEN");
    auto g = comp.createGroup("bank");
    napi_string_attribute(g.getId(), "type", "Readouts");
    napi_string_attribute(g.getId(), "description", description);
    g.createAttribute<int32_t>("ess_type", ess_type);
    g.createAttribute<uint64_t>("normalization", normalization);
    napi_string_attribute(g.getId(), "efu_address", "127.0.0.1");
    g.createAttribute<int32_t>("efu_port", 9001);
    g.createDataSet("ring", ring);
    g.createDataSet("FEN", fen);
    g.createDataSet("time", time);
    g.createDataSet("weight", weight);
    g.createDataSet("channel", channel);
    g.createDataSet("a", a);
    g.createDataSet("b", b);
    g.createDataSet("c", c);
    g.createDataSet("d", d);
  }

  REQUIRE(import_mccode_nexus(out.string(), nexus.string(), false) == 1);
  REQUIRE(validate_collector_file(out.string()) == 1);
  {
    const ReaderSource source(out.string(), false);
    REQUIRE(source.readers().size() == 1);
    const auto & reader = source.readers().front();
    REQUIRE(reader.collector_name() == "bank");
    REQUIRE(source.has_parameters());
    const auto names = source.parameter_names();
    REQUIRE(std::find(names.begin(), names.end(), "lambda") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "bins") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "tag") != names.end());
  }
  {
    HighFive::File file(out.string(), HighFive::File::ReadOnly);
    const auto group = file.getGroup("bank");
    REQUIRE(group.getDataSet("normalizations").read<std::vector<uint64_t>>().front() == normalization);
    REQUIRE(group.getDataSet("weights").read<std::vector<double>>().front() == 4.5);
    REQUIRE(group.getAttribute("efu_port").read<int>() == 9001);
    const auto readouts = group.getDataSet("readouts");
    REQUIRE(readouts.getDimensions().front() == 3);
    const auto schema = parse_type_description(description);
    std::vector<uint8_t> raw(3 * schema.total_size);
    readouts.read_raw(raw.data(), readouts.getDataType());
    for (size_t i = 0; i < 3; ++i) {
      for (const auto & f : schema.fields) {
        const uint8_t * p = raw.data() + i * schema.total_size + f.offset;
        if (f.name == "ring") REQUIRE(*p == ring[i]);
        if (f.name == "channel") REQUIRE(*p == channel[i]);
        if (f.name == "a") { uint16_t v; std::memcpy(&v, p, 2); REQUIRE(v == a[i]); }
        if (f.name == "time") { double v; std::memcpy(&v, p, 8); REQUIRE(v == time[i]); }
        if (f.name == "weight") { double v; std::memcpy(&v, p, 8); REQUIRE(v == weight[i]); }
      }
    }
    const auto lambda = file.getGroup("parameters").getDataSet("lambda");
    REQUIRE(lambda.getDataType().getClass() == HighFive::DataTypeClass::Float);
    const auto bins = file.getGroup("parameters").getDataSet("bins");
    REQUIRE(bins.getDataType().getClass() == HighFive::DataTypeClass::Integer);
  }
  // refuses to overwrite
  REQUIRE(import_mccode_nexus(out.string(), nexus.string(), false) == -1);
  std::filesystem::remove(nexus);
  std::filesystem::remove(out);
}
