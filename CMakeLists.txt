cmake_minimum_required(VERSION 3.30)
cmake_policy(SET CMP0076 NEW) # Ensure target_sources converts relative paths
cmake_policy(SET CMP0017 NEW) # Prefer cmake's own files for include/find_package before CMAKE_MODULE_PATH

set(CMAKE_MACOSX_RPATH OFF)
list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_CURRENT_LIST_DIR}/cmake")

set(READOUT_BUILD_ON_CONDA OFF CACHE BOOL "Set to ON to build for conda")
set(READOUT_USE_CONAN ON CACHE BOOL "Use Conan to manage dependencies") # Set to OFF to use system libraries
set(READOUT_BUILD_TESTS ON CACHE BOOL "Build test binary")
set(READOUT_DEVELOPMENT_MODE OFF CACHE BOOL "Build with paths relative to build directory for local development")

if (READOUT_USE_CONAN)
    set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES "${CMAKE_CURRENT_LIST_DIR}/cmake/conan_provider.cmake")
endif()

# Read the version of readout (from the VERSION file and git metadata);
# generates version.hpp in ${CMAKE_BINARY_DIR}/gen
include(check)
checkSetup(READOUT)
message(STATUS "Build Readout v${READOUT_VERSION} with type ${CMAKE_BUILD_TYPE}")
project(Readout LANGUAGES C CXX VERSION "${READOUT_VERSION}")

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

add_definitions(-DUSE_HIGHFIVE)
if (MSVC)
    # warning level 4 -- add /WX for all warnings as errors
    add_compile_options(/W4)
    # suppress MSVC warning C4996 about 'localtime' vs 'localtime_s'
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    # Allow UTF-8 identifiers https://stackoverflow.com/a/47704050
    add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
    add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
    # Prevent Windows.h from defining its own min and max macros
    add_definitions(-DNOMINMAX -DREADOUT_SHARED -DREADOUT_EXPORT)
elseif(APPLE)
    add_compile_options(-Wall -Wextra -pedantic)
    if (READOUT_BUILD_ON_CONDA)
        # Enable use of, e.g. std::filesystem::path on macOS under Conda:
        # https://conda-forge.org/docs/maintainer/knowledge_base/#newer-c-features-with-old-sdk
        add_definitions(-D_LIBCPP_DISABLE_AVAILABILITY)
    endif()
else()
    add_compile_options(-Wall -Wextra -pedantic)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif(NOT CMAKE_BUILD_TYPE)
if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    add_definitions(-DDEBUG)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# With GCC 10+ the interprocedural optimization only adds to compilation time without improving performance
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)

if (READOUT_BUILD_TESTS)
    include(CTest)
endif()

# The library, CLI tools, McStas components, and install rules:
add_subdirectory(readout_core)

# The test suite lives at the repository root (mcpl-style): it exercises the
# readout_core targets but is not part of any installed package.
if (READOUT_BUILD_TESTS)
    add_subdirectory(test)
endif()
