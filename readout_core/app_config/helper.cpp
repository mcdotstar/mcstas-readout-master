#include "helper.h"

#ifdef _WIN32

std::filesystem::path executable_path() {
  auto exe_path = GetStringFromWindowsApi<TCHAR>( []( TCHAR* buffer, int size ) {
    return GetModuleFileName(nullptr, buffer, size );
  });
  return std::filesystem::path(exe_path);
}

#elif defined(__APPLE__)

#include <mach-o/dyld.h>
#include <vector>

std::filesystem::path executable_path() {
  char buffer[1024];
  uint32_t size = sizeof(buffer);
  // If the buffer is not large enough, size is set to the correct size and return value is -1
  if (_NSGetExecutablePath(buffer, &size) == 0) {
    return std::filesystem::canonical(buffer);
  } else {
    std::vector<char> largerBuffer(size);
    if (_NSGetExecutablePath(largerBuffer.data(), &size) == 0) {
      return std::filesystem::canonical(largerBuffer.data());
    }
  }
  throw std::runtime_error("Unable to determine executable path");
}

#else

std::filesystem::path executable_path(){
  std::filesystem::path result("/proc/self/exe");
  return std::filesystem::canonical(result);
}

#endif


std::filesystem::path installation_path(const std::string & relative) {
  auto cwd = executable_path().parent_path();
  return std::filesystem::weakly_canonical(cwd / relative);
}