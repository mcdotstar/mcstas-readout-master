cmake_minimum_required(VERSION 3.30)
cmake_policy(SET CMP0076 NEW) # Ensure target_sources converts relative paths
cmake_policy(SET CMP0017 NEW) # Prefer cmake's own files for include/find_package before CMAKE_MODULE_PATH

set(CMAKE_MACOSX_RPATH OFF)
list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_CURRENT_LIST_DIR}/cmake")

set(READOUT_BUILD_ON_CONDA OFF CACHE BOOL "Set to ON to build for conda")
set(READOUT_USE_CONAN ON CACHE BOOL "Use Conan to manage dependencies") # Set to OFF to use system libraries
set(READOUT_BUILD_TESTS ON CACHE BOOL "Build test binary")

if (READOUT_USE_CONAN)
    set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES "${CMAKE_CURRENT_LIST_DIR}/cmake/conan_provider.cmake")
endif()

# Read the version of readout
include(check)
checkSetup(READOUT) # generates version.hpp and adds the generated directory as a target include directory
# So that we can print it to the console along with the specified build type
message(STATUS "Build Readout v${READOUT_VERSION} with type ${CMAKE_BUILD_TYPE}")
project(Readout LANGUAGES C CXX VERSION "${READOUT_VERSION}")

set(READOUT_LIBRARY_TARGET readout)
set(READOUT_REPLAY_BINARY readout-replay)
set(READOUT_CONFIG_BINARY readout-config)
set(READOUT_SINGLE_HEADER readout.h) # must match template file in project root
SET(READOUT_LIB_DESTINATION lib)
SET(READOUT_BIN_DESTINATION bin)
SET(READOUT_INCLUDE_DESTINATION include)
set(READOUT_TESTER_TARGET readout_tester)
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)
set(Readout_BINDIR "${CMAKE_INSTALL_BINDIR}")  # e.g. <prefix>/bin
set(Readout_LIBDIR "${CMAKE_INSTALL_LIBDIR}")  # e.g. <prefix>/lib
set(Readout_INCDIR "${CMAKE_INSTALL_INCLUDEDIR}")  # e.g. <prefix>/include
set(Readout_DATADIR "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}") # e.g. <prefix>/share/Readout>
if (NOT Readout_CMAKEDIR)
    set(Readout_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}") #e.g. <prefix>/lib/cmake/Readout>
endif()

#Get a few relative paths, mostly for expansion in various installed files (we
#use PROJECT_BINARY_DIR as prefix here, but it should not matter which as long
#as it is an absolute path):
file(RELATIVE_PATH Readout_BIN2LIB "${PROJECT_BINARY_DIR}/${Readout_BINDIR}" "${PROJECT_BINARY_DIR}/${Readout_LIBDIR}")
file(RELATIVE_PATH Readout_BIN2INC "${PROJECT_BINARY_DIR}/${Readout_BINDIR}" "${PROJECT_BINARY_DIR}/${Readout_INCDIR}")
file(RELATIVE_PATH Readout_BIN2DATA "${PROJECT_BINARY_DIR}/${Readout_BINDIR}" "${PROJECT_BINARY_DIR}/${Readout_DATADIR}")
file(RELATIVE_PATH Readout_CMAKE2ROOT "${PROJECT_BINARY_DIR}/${Readout_CMAKEDIR}" "${PROJECT_BINARY_DIR}/")
file(RELATIVE_PATH Readout_CMAKE2BIN "${PROJECT_BINARY_DIR}/${Readout_CMAKEDIR}" "${PROJECT_BINARY_DIR}/${Readout_BINDIR}")
file(RELATIVE_PATH Readout_CMAKE2LIB "${PROJECT_BINARY_DIR}/${Readout_CMAKEDIR}" "${PROJECT_BINARY_DIR}/${Readout_LIBDIR}")
file(RELATIVE_PATH Readout_CMAKE2INC "${PROJECT_BINARY_DIR}/${Readout_CMAKEDIR}" "${PROJECT_BINARY_DIR}/${Readout_INCDIR}")

include(CMakePackageConfigHelpers)
write_basic_package_version_file( "${PROJECT_BINARY_DIR}/ReadoutConfigVersion.cmake"
        VERSION ${Readout_VERSION} COMPATIBILITY SameMajorVersion )

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
    # lots of warnings -- add -Werror for  all warnings as errors
    add_compile_options(-Wall -Wextra -pedantic)
    if (READOUT_BUILD_ON_CONDA)
        # Enable use of, e.g. std::filesystem::path on macOS under Conda:
        # https://conda-forge.org/docs/maintainer/knowledge_base/#newer-c-features-with-old-sdk
        add_definitions(-D_LIBCPP_DISABLE_AVAILABILITY)
    endif()
else()
    # lots of warnings -- add -Werror for  all warnings as errors
    add_compile_options(-Wall -Wextra -pedantic)
endif()

set(CMAKE_MACOSX_RPATH OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif(NOT CMAKE_BUILD_TYPE)

if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    add_definitions(-DDEBUG)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# With GCC 10+ the interproceedural optmization only adds to compilation time without improving performance
SET(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)

#
## Cargs is used to help parse command line arguments in the CLI utilities
#find_package(cargs REQUIRED)


list(APPEND CXX_TARGETS ${READOUT_REPLAY_BINARY})
if (READOUT_BUILD_TESTS)
    list(APPEND CXX_TARGETS ${READOUT_TESTER_BINARY})
endif()


# target for C++ shared library
add_library(${READOUT_LIBRARY_TARGET} SHARED)
list(APPEND LIB_TARGETS ${READOUT_LIBRARY_TARGET})
set(Readout_LIBNAME "${CMAKE_SHARED_LIBRARY_PREFIX}${READOUT_LIBRARY_TARGET}${CMAKE_SHARED_LIBRARY_SUFFIX}")
set_target_properties(${READOUT_LIBRARY_TARGET} PROPERTIES PUBLIC_HEADER lib/Readout.h)
set_target_properties(${READOUT_LIBRARY_TARGET} PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})

find_package(nlohmann_json REQUIRED)
target_link_libraries(${READOUT_LIBRARY_TARGET} PUBLIC nlohmann_json::nlohmann_json)

add_library(Readout::${READOUT_LIBRARY_TARGET} ALIAS ${READOUT_LIBRARY_TARGET}) # always alias namespaces locally

# target for file-replay CLI utility
list(APPEND BIN_TARGETS ${READOUT_REPLAY_BINARY})
add_executable(${READOUT_REPLAY_BINARY})
target_include_directories(${READOUT_REPLAY_BINARY} PRIVATE ${cargs_INCLUDE_DIR})
target_link_libraries(${READOUT_REPLAY_BINARY} PRIVATE ${cargs_LIBRARIES} ${READOUT_LIBRARY_TARGET})

# target for config tool CLI utility
list(APPEND CONFIG_BIN_TARGETS ${READOUT_CONFIG_BINARY})
add_executable(${READOUT_CONFIG_BINARY})
target_include_directories(${READOUT_CONFIG_BINARY} PRIVATE ${cargs_INCLUDE_DIR})
target_link_libraries(${READOUT_CONFIG_BINARY} PRIVATE ${cargs_LIBRARIES})

if (READOUT_BUILD_TESTS)
    # Catch2 is used for testing
    find_package(Catch2 REQUIRED)
    # Threads are used in the library tests
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    # target for testing library functionality
    add_executable(${READOUT_TESTER_TARGET})
    # target_include_directories(${READOUT_TESTER_TARGET} PRIVATE lib)
    target_link_libraries(${READOUT_TESTER_TARGET} PRIVATE Catch2::Catch2WithMain Threads::Threads ${READOUT_LIBRARY_TARGET})
    include(CTest)
    include(Catch)
    catch_discover_tests(${READOUT_TESTER_TARGET})
endif()

add_subdirectory(lib)  # sources for libreadout
add_subdirectory(src)  # sources for file interface in C++

if (READOUT_BUILD_TESTS)
    add_subdirectory(test) # sources for the testing binary
endif()

list(APPEND CXX_TARGETS ${LIB_TARGETS})
foreach(CXX_TARGET IN LISTS CXX_TARGETS)
    addCheckDependency(${CXX_TARGET})
endforeach()

include(readout-hdf5)

install(TARGETS ${READOUT_LIBRARY_TARGET} ${READOUT_REPLAY_BINARY} ${READOUT_CONFIG_BINARY} EXPORT ReadoutTargets
        RUNTIME DESTINATION ${Readout_BINDIR}
        LIBRARY DESTINATION ${Readout_LIBDIR}
        ARCHIVE DESTINATION ${Readout_LIBDIR}
        PUBLIC_HEADER DESTINATION ${Readout_INCDIR})
# The following would allow for importing Readout by another CMake ... but causes a CMake configuration error
install(EXPORT ${PROJECT_NAME}Targets FILE ${PROJECT_NAME}Targets.cmake NAMESPACE Readout:: DESTINATION ${Readout_CMAKEDIR})

configure_file("${PROJECT_SOURCE_DIR}/cmake/ReadoutConfig.cmake.in" "${PROJECT_BINARY_DIR}/ReadoutConfig.cmake" @ONLY)

install(FILES "${PROJECT_BINARY_DIR}/ReadoutConfigVersion.cmake" "${PROJECT_BINARY_DIR}/ReadoutConfig.cmake"
        DESTINATION ${Readout_CMAKEDIR} )

configure_file("${PROJECT_SOURCE_DIR}/cmake/readout_config.h.in" "${CMAKE_BINARY_DIR}/readout_config.h" @ONLY)

# Copy the component file to the include directory too
file(GLOB ALL_COMPONENTS "${CMAKE_CURRENT_SOURCE_DIR}/share/Readout/*.comp")
install(FILES ${ALL_COMPONENTS} DESTINATION ${Readout_DATADIR})
