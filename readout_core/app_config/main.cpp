#include <iostream>
#include <cstring>
#include "args.hxx"
#include "readout_config.h"
#include "helper.h"


enum class ConfigShow {
  LIBDIR,
  INCLUDEDIR,
  COMPDIR,
  LIBNAME,
  VERSION,
  BINDIR,
  LDFLAGS,
  CFLAGS
};

long long version_integer(const char * version){
  long long result = 0;
  for (int i=0; i<3; ++i){
    result *= 100;
    result += std::stoi(version);
    version = strchr(version, '.');
    if (version == nullptr) break;
    version++;
  }
  return result;
}


const char * installation_info(const ConfigShow choice) {
  switch (choice) {
    case ConfigShow::LIBDIR: return libreadout::config::libdir;
    case ConfigShow::INCLUDEDIR: return libreadout::config::includedir;
    case ConfigShow::COMPDIR: return libreadout::config::compdir;
    case ConfigShow::LIBNAME: return libreadout::config::libname;
    case ConfigShow::VERSION: return libreadout::config::version;
    case ConfigShow::BINDIR: return libreadout::config::bindir;
    case ConfigShow::LDFLAGS: return libreadout::config::ldflags;
    case ConfigShow::CFLAGS: return libreadout::config::cflags;
  }
  return nullptr;
}


std::string lookup_choice(const ConfigShow choice) {
  auto info = installation_info(choice);
  if (info == nullptr) return "";
  if (ConfigShow::LIBNAME == choice || ConfigShow::VERSION == choice) return info;
  if (ConfigShow::LDFLAGS == choice) {
    std::string result = info;
    auto replacement = lookup_choice(ConfigShow::LIBDIR);
    while (result.find("<LIBDIR>") != std::string::npos){
      auto pos = result.find("<LIBDIR>");
      auto new_str = result.substr(0, pos) + replacement + result.substr(pos+8);
      result = new_str;
    }
    return result;
  }
  if (ConfigShow::CFLAGS == choice){
    std::string result = info;
    auto replacement = lookup_choice(ConfigShow::INCLUDEDIR);
    while (result.find("<INCLUDEDIR>") != std::string::npos){
      auto pos = result.find("<INCLUDEDIR>");
      auto new_str = result.substr(0, pos) + replacement + result.substr(pos+12);
      result = new_str;
    }
    return result;
  }
  return installation_path(info).string();
}



struct ConfgShowReader
{
  void operator()(const std::string &name, const std::string &in_value, ConfigShow &destination)
  {
    auto value = in_value;
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    if (value == "libdir") destination = ConfigShow::LIBDIR;
    else if (value == "includedir") destination = ConfigShow::INCLUDEDIR;
    else if (value == "compdir") destination = ConfigShow::COMPDIR;
    else if (value == "libname") destination = ConfigShow::LIBNAME;
    else if (value == "version") destination = ConfigShow::VERSION;
    else if (value == "bindir") destination = ConfigShow::BINDIR;
    else if (value == "ldflags") destination = ConfigShow::LDFLAGS;
    else if (value == "cflags") destination = ConfigShow::CFLAGS;
    else throw std::runtime_error("Invalid choice: " + in_value + " for " + name);
  }
};


int main(int argc, char * argv[]){
  std::string choices{"libdir, includedir, compdir, libname, bindir, ldflags, cflags"};
  args::ArgumentParser parser("Readout library configuration information",
                              "Resolved paths are only valid if this binary is installed.");
  args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"});
  args::Flag version(parser, "version", "Print the library version", {'v', "version"});
  args::Flag intversion(parser, "intversion", "Print the library version as an integer", {'i', "intversion"});
  args::ValueFlag<ConfigShow, ConfgShowReader> choice(parser, "CHOICE", "Show requested information about installation CHOICE=["+choices+"]", {'s', "show"});

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

  if (version) {
    std::cout << libreadout::config::version << std::endl;
    return 0;
  }
  if (intversion) {
    std::cout << version_integer(libreadout::config::version) << std::endl;
    return 0;
  }
  if (choice && choice.Get() != ConfigShow::VERSION) {
    std::cout << lookup_choice(choice.Get()) << std::endl;
    return 0;
  }

  std::cout << parser << std::endl;
  std::cout << "(Hint: select '-s' and one of the following choices: " << choices  << ")" << std::endl;

  return EXIT_SUCCESS;
}