#include <iostream>
#include "args.hxx"
#include "reader.h"
#include "replay.h"


int main(int argc, char * argv[]){
  args::ArgumentParser parser("Replay saved weighted events",
                              "Destination Event Formation Unit(s) should be running.");
  args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"});
  args::Flag verbose(parser, "verbose", "Print additional information, including per-point parameters", {'v', "verbose"});

  args::Group playback_type(parser, "Playback type (exclusive)", args::Group::Validators::Xor);
  args::Flag sequential_flag(playback_type, "sequential", "Replay events in order", {'s', "sequential"});
  args::Flag random_flag(playback_type, "random", "Replay events in random order within each point", {'r', "random"});

  args::Group sampling_group(parser, "Statistical sampling", args::Group::Validators::DontCare);
  args::ValueFlag<double> time_flag(sampling_group, "TIME", "Counting time in seconds: each stored readout with rate-weight w is sent n ~ Poisson(w * TIME) times; without TIME every stored readout is sent exactly once", {'t', "time"});
  args::ValueFlag<uint32_t> seed_flag(sampling_group, "SEED", "Random seed for sampling and shuffling (0 or absent: non-deterministic)", {"seed"});

  args::Group number_group(parser, "Replay subset, FIRST and EVERY ignored if COUNT is not present", args::Group::Validators::DontCare);
  args::ValueFlag<size_t> count_flag(number_group, "COUNT", "Number of stored readouts to replay", {'n', "count"});
  args::ValueFlag<size_t> first_flag(number_group, "FIRST", "First stored readout to replay", {'f', "first"});
  args::ValueFlag<size_t> every_flag(number_group, "EVERY", "Replay every EVERYth stored readout", {'e', "every"});

  args::Group timing_group(parser, "Reference time behavior", args::Group::Validators::DontCare);
  args::ValueFlag<double> rate_flag(timing_group, "RATE", "Pulse (reference time) repetition rate in Hz; packet pulse times march forward on this grid (default 14, the ESS source frequency)", {"pulse-rate"});
  args::Flag fold_flag(timing_group, "fold-tof", "Stamp each event at pulse + (tof mod pulse period) instead of pulse + tof, wrapping long-time-of-flight events into the frame they would be detected in", {"fold-tof"});

  args::Group efu_group(parser, "Event Formation Unit connection", args::Group::Validators::DontCare);
  args::ValueFlag<std::string> address_flag(efu_group, "ADDR", "Default EFU IP address", {'a', "addr"});
  args::ValueFlag<int> port_flag(efu_group, "PORT", "Default EFU UDP port for accepting data", {'p', "port"});
  args::ValueFlag<std::string> config_flag(efu_group, "CONFIG", "JSON file with per-(detector, readout) EFU endpoints", {'c', "config"});

  args::Positional<std::string> filename_positional(parser, "filename", "Filename to replay");

  try
  {
    parser.ParseCLI(argc, argv);
  }
  catch (const args::Help&)
  {
    std::cout << parser;
    return 0;
  }
  catch (const args::ParseError& e)
  {
    std::cerr << e.what() << std::endl;
    std::cerr << parser;
    return 1;
  }

  const auto filename = args::get(filename_positional);

  ReplayConfig config;
  if (time_flag) config.counting_time = args::get(time_flag);
  if (seed_flag) config.seed = args::get(seed_flag);
  config.random_order = static_cast<bool>(random_flag);
  if (rate_flag) config.pulse_rate = args::get(rate_flag);
  config.fold_tof = static_cast<bool>(fold_flag);
  if (address_flag) config.default_address = args::get(address_flag);
  if (port_flag) config.default_port = args::get(port_flag);
  if (config_flag) {
    try {
      config.senders = SenderConfigs::from_file(args::get(config_flag));
    } catch (const std::exception & e) {
      std::cerr << "Error reading sender configuration " << args::get(config_flag) << ":\n" << e.what() << std::endl;
      return 1;
    }
  }
  if (count_flag) {
    const auto first = first_flag ? args::get(first_flag) : 0u;
    const auto every = every_flag ? args::get(every_flag) : 1u;
    config.subset = ReplaySubset{first, args::get(count_flag), every};
  }

  if (verbose){
    std::cout << "Replaying " << (count_flag ? "a subset of" : "all") << " events from " << filename;
    std::cout << " to " << config.default_address << ":" << config.default_port;
    if (time_flag) std::cout << " with counting time " << config.counting_time.value() << " s";
    std::cout << " at pulse rate " << config.pulse_rate << " Hz";
    std::cout << std::endl;
  }

  try {
    if (verbose) {
      StreamParameterPublisher publisher(std::cout);
      replay(filename, config, publisher);
    } else {
      replay(filename, config);
    }
  } catch (const std::exception & e) {
    std::cerr << "Replay failed:\n" << e.what() << std::endl;
    return 1;
  }
  return EXIT_SUCCESS;
}
