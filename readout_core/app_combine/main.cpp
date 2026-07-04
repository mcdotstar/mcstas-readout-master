// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
#include <iostream>
#include <filesystem>
#include "args.hxx"
#include "CollectorClass.h"
#include "reader.h"

namespace {

/// Print validate information for one file to stdout.
/// Returns false if the file is not a valid collector file.
bool print_file_info(const std::string & filename) {
  const int points = validate_collector_file(filename);
  if (points < 0) {
    std::cout << filename << ": INVALID" << std::endl;
    return false;
  }
  std::cout << filename << ": valid, " << points << " point(s)";
  // Attempt to open via ReaderSource to get group/parameter names.
  try {
    const ReaderSource source(filename);
    std::cout << ", collectors: [";
    bool first = true;
    for (const auto & reader : source.readers()) {
      if (!first) std::cout << ", ";
      std::cout << reader.collector_name();
      first = false;
    }
    std::cout << "]";
    if (source.has_parameters()) {
      std::cout << ", parameters: [";
      bool pfirst = true;
      for (const auto & name : source.parameter_names()) {
        if (!pfirst) std::cout << ", ";
        std::cout << name;
        pfirst = false;
      }
      std::cout << "]";
    }
  } catch (const std::exception & e) {
    std::cout << " (reader error: " << e.what() << ")";
  }
  std::cout << std::endl;
  return true;
}

} // namespace

int main(int argc, char * argv[]) {
  args::ArgumentParser parser(
    "Combine or validate libreadout collector HDF5 files",
    "Use 'readout-combine COMMAND --help' for per-subcommand usage."
  );
  args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"});

  int exit_code = 0;

  // ---- validate subcommand -----------------------------------------------
  args::Command validate_cmd(parser, "validate",
    "Validate one or more collector files and print their contents summary",
    [&exit_code](args::Subparser & sub) {
      args::HelpFlag sub_help(sub, "help", "Display this help menu", {'h', "help"});
      args::PositionalList<std::string> files(sub, "files", "Collector files to validate");
      sub.Parse();
      if (args::get(files).empty()) {
        std::cerr << "validate: at least one file must be specified" << std::endl;
        exit_code = 1;
        return;
      }
      for (const auto & f : args::get(files)) {
        if (!print_file_info(f)) {
          exit_code = 1;
        }
      }
    });

  // ---- append subcommand -------------------------------------------------
  args::Command append_cmd(parser, "append",
    "Combine same-point collector files (identical parameters) by appending readouts",
    [&exit_code](args::Subparser & sub) {
      args::HelpFlag sub_help(sub, "help", "Display this help menu", {'h', "help"});
      args::ValueFlag<std::string> output(sub, "OUTPUT", "Output file to create", {'o', "output"});
      args::PositionalList<std::string> files(sub, "files", "Input collector files to append (must have identical parameters)");
      sub.Parse();
      if (!output) {
        std::cerr << "append: --output is required" << std::endl;
        exit_code = 1;
        return;
      }
      const auto & out = args::get(output);
      const auto & in = args::get(files);
      if (in.size() < 2) {
        std::cerr << "append: at least 2 input files are required" << std::endl;
        exit_code = 1;
        return;
      }
      if (std::filesystem::exists(out)) {
        std::cerr << "append: output file already exists: " << out << std::endl;
        exit_code = 1;
        return;
      }
      if (!append_collector_files(out, in, false)) {
        std::cerr << "append: combination failed" << std::endl;
        exit_code = 1;
      }
    });

  // ---- concatenate subcommand --------------------------------------------
  args::Command concatenate_cmd(parser, "concatenate",
    "Combine different-point collector files into a multi-point cue-based file",
    [&exit_code](args::Subparser & sub) {
      args::HelpFlag sub_help(sub, "help", "Display this help menu", {'h', "help"});
      args::ValueFlag<std::string> output(sub, "OUTPUT", "Output file to create", {'o', "output"});
      args::PositionalList<std::string> files(sub, "files", "Input collector files to concatenate (consistent but not identical parameters)");
      sub.Parse();
      if (!output) {
        std::cerr << "concatenate: --output is required" << std::endl;
        exit_code = 1;
        return;
      }
      const auto & out = args::get(output);
      const auto & in = args::get(files);
      if (in.size() < 2) {
        std::cerr << "concatenate: at least 2 input files are required" << std::endl;
        exit_code = 1;
        return;
      }
      if (std::filesystem::exists(out)) {
        std::cerr << "concatenate: output file already exists: " << out << std::endl;
        exit_code = 1;
        return;
      }
      if (!concatenate_collector_files(out, in)) {
        std::cerr << "concatenate: combination failed" << std::endl;
        exit_code = 1;
      }
    });

  try {
    parser.ParseCLI(argc, argv);
  } catch (const args::Help &) {
    std::cout << parser;
    return 0;
  } catch (const args::ParseError & e) {
    std::cerr << e.what() << std::endl;
    std::cerr << parser;
    return 1;
  } catch (const args::ValidationError & e) {
    std::cerr << e.what() << std::endl;
    std::cerr << parser;
    return 1;
  }

  return exit_code;
}
