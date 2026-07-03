# The readout core package: libreadout, the CLI tools (readout-config,
# readout-replay, readout-combine), the McStas components, and the install rules.

set(READOUT_LIBRARY_TARGET readout)
set(READOUT_REPLAY_BINARY readout-replay)
set(READOUT_CONFIG_BINARY readout-config)
set(READOUT_COMBINE_BINARY readout-combine)

# One layout everywhere: these are install-relative directories, and the build
# tree mirrors them (output dirs set at the root, header/component copies below),
# so a single set of baked relative paths serves both trees.
set(Readout_BINDIR "${CMAKE_INSTALL_BINDIR}")  # e.g. <prefix>/bin
set(Readout_LIBDIR "${CMAKE_INSTALL_LIBDIR}")  # e.g. <prefix>/lib
set(Readout_INCDIR "${CMAKE_INSTALL_INCLUDEDIR}")  # e.g. <prefix>/include
set(Readout_DATADIR "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}") # e.g. <prefix>/share/Readout

if (NOT Readout_CMAKEDIR)
    set(Readout_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}") #e.g. <prefix>/lib/cmake/Readout
endif()

# Relative paths between the installed artefacts, baked into readout-config and
# the CMake package files so that both are relocatable (resolved at runtime
# against the location of the binary / package file):
file(RELATIVE_PATH Readout_BIN2LIB "${PROJECT_BINARY_DIR}/${Readout_BINDIR}" "${PROJECT_BINARY_DIR}/${Readout_LIBDIR}")
file(RELATIVE_PATH Readout_BIN2INC "${PROJECT_BINARY_DIR}/${Readout_BINDIR}" "${PROJECT_BINARY_DIR}/${Readout_INCDIR}")
file(RELATIVE_PATH Readout_BIN2DATA "${PROJECT_BINARY_DIR}/${Readout_BINDIR}" "${PROJECT_BINARY_DIR}/${Readout_DATADIR}")
file(RELATIVE_PATH Readout_CMAKE2ROOT "${PROJECT_BINARY_DIR}/${Readout_CMAKEDIR}" "${PROJECT_BINARY_DIR}/")
file(RELATIVE_PATH Readout_CMAKE2BIN "${PROJECT_BINARY_DIR}/${Readout_CMAKEDIR}" "${PROJECT_BINARY_DIR}/${Readout_BINDIR}")
file(RELATIVE_PATH Readout_CMAKE2LIB "${PROJECT_BINARY_DIR}/${Readout_CMAKEDIR}" "${PROJECT_BINARY_DIR}/${Readout_LIBDIR}")
file(RELATIVE_PATH Readout_CMAKE2INC "${PROJECT_BINARY_DIR}/${Readout_CMAKEDIR}" "${PROJECT_BINARY_DIR}/${Readout_INCDIR}")

# ---- libreadout --------------------------------------------------------------

add_library(${READOUT_LIBRARY_TARGET} SHARED)
set(Readout_LIBNAME "${CMAKE_SHARED_LIBRARY_PREFIX}${READOUT_LIBRARY_TARGET}${CMAKE_SHARED_LIBRARY_SUFFIX}")
set_target_properties(${READOUT_LIBRARY_TARGET} PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})
add_library(Readout::${READOUT_LIBRARY_TARGET} ALIAS ${READOUT_LIBRARY_TARGET})

target_sources(${READOUT_LIBRARY_TARGET} PRIVATE
        src/readout_orig.cpp
        src/readout_collector.cpp
        src/readout_discrete.cpp
        src/Readout_merge.cpp
        src/ReadoutClass.cpp
        src/enums.cpp
        src/hdf_interface.cpp
        src/replay.cpp
        src/Sender.cpp
        src/SenderConfigs.cpp
        src/CollectorClass.cpp
        src/TypeDescriptionParser.cpp
        src/readout_type_descriptions.cpp
        src/Array.cpp
)
target_include_directories(${READOUT_LIBRARY_TARGET}
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
        $<INSTALL_INTERFACE:${Readout_INCDIR}>
)
target_include_directories(${READOUT_LIBRARY_TARGET} PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src"
        "${CMAKE_CURRENT_LIST_DIR}/src/ctream"
)

# nlohmann_json is used only inside SenderConfigs.cpp: keep it out of the
# installed export so find_package(Readout) does not require it downstream
find_package(nlohmann_json REQUIRED)
target_link_libraries(${READOUT_LIBRARY_TARGET} PRIVATE nlohmann_json::nlohmann_json)

# ---- CLI tools ---------------------------------------------------------------

set(ARGS_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/app_common")

# readout-config: self-locating installation information tool
configure_file("${CMAKE_CURRENT_LIST_DIR}/cmake/readout_config.h.in" "${CMAKE_CURRENT_BINARY_DIR}/readout_config.h" @ONLY)
add_executable(${READOUT_CONFIG_BINARY}
        app_config/main.cpp
        app_config/helper.cpp
)
target_include_directories(${READOUT_CONFIG_BINARY} PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/app_config" "${CMAKE_CURRENT_BINARY_DIR}" "${ARGS_INCLUDE_DIR}")

# readout-replay: sampled replay of collector files to EFUs
add_executable(${READOUT_REPLAY_BINARY} app_replay/main.cpp)
target_include_directories(${READOUT_REPLAY_BINARY} PRIVATE "${ARGS_INCLUDE_DIR}")
target_link_libraries(${READOUT_REPLAY_BINARY} PRIVATE ${READOUT_LIBRARY_TARGET})

# readout-combine: validate/append/concatenate collector files
add_executable(${READOUT_COMBINE_BINARY} app_combine/main.cpp)
target_include_directories(${READOUT_COMBINE_BINARY} PRIVATE "${ARGS_INCLUDE_DIR}")
target_link_libraries(${READOUT_COMBINE_BINARY} PRIVATE ${READOUT_LIBRARY_TARGET})

# ---- dependencies needing all targets ------------------------------------------

set(CXX_TARGETS ${READOUT_LIBRARY_TARGET} ${READOUT_REPLAY_BINARY} ${READOUT_COMBINE_BINARY})
foreach(CXX_TARGET IN LISTS CXX_TARGETS)
    addCheckDependency(${CXX_TARGET})
endforeach()
addCheckDependency(${READOUT_CONFIG_BINARY})

include(readout-hdf5) # find_package(HighFive) and link it to the CXX_TARGETS

# ---- CLI smoke tests -----------------------------------------------------------

if (READOUT_BUILD_TESTS)
    add_test(NAME replay_binary_test COMMAND ${READOUT_REPLAY_BINARY} --help)
    add_test(NAME replay_config_help_test COMMAND ${READOUT_CONFIG_BINARY} --help)
    add_test(NAME replay_config_show_test COMMAND ${READOUT_CONFIG_BINARY} --show libdir)
    add_test(NAME combine_binary_help_test COMMAND ${READOUT_COMBINE_BINARY} --help)
    add_test(NAME combine_validate_missing_test COMMAND ${READOUT_COMBINE_BINARY} validate /nonexistent_file_for_testing.h5)
    set_tests_properties(combine_validate_missing_test PROPERTIES WILL_FAIL TRUE)
endif()

# ---- build-tree layout ----------------------------------------------------------

# Headers and components are copied into the build tree's install-mirrored
# locations; binaries and the library land there directly via the output
# directories set at the root. The build tree is thereby directly usable by
# mccode-antlr (readout-config on PATH) without an install step.
add_custom_command(TARGET ${READOUT_LIBRARY_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_BINARY_DIR}/${Readout_INCDIR}"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_BINARY_DIR}/gen/version.hpp"
        "${CMAKE_BINARY_DIR}/${Readout_INCDIR}/version.hpp"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/components"
        "${CMAKE_BINARY_DIR}/${Readout_DATADIR}"
)

# ---- install rules --------------------------------------------------------------

include(CMakePackageConfigHelpers)
write_basic_package_version_file( "${PROJECT_BINARY_DIR}/ReadoutConfigVersion.cmake"
        VERSION ${Readout_VERSION} COMPATIBILITY SameMajorVersion )

install(TARGETS ${READOUT_LIBRARY_TARGET} ${READOUT_REPLAY_BINARY} ${READOUT_CONFIG_BINARY} ${READOUT_COMBINE_BINARY} EXPORT ReadoutTargets
        RUNTIME DESTINATION ${Readout_BINDIR}
        LIBRARY DESTINATION ${Readout_LIBDIR}
        ARCHIVE DESTINATION ${Readout_LIBDIR})
install(EXPORT ${PROJECT_NAME}Targets FILE ${PROJECT_NAME}Targets.cmake NAMESPACE Readout:: DESTINATION ${Readout_CMAKEDIR})

configure_file("${CMAKE_CURRENT_LIST_DIR}/cmake/ReadoutConfig.cmake.in" "${PROJECT_BINARY_DIR}/ReadoutConfig.cmake" @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/ReadoutConfigVersion.cmake" "${PROJECT_BINARY_DIR}/ReadoutConfig.cmake"
        DESTINATION ${Readout_CMAKEDIR} )

# The McStas components and their support library are runtime data, located by
# mccode-antlr through `readout-config --show compdir`:
file(GLOB ALL_COMPONENTS "${CMAKE_CURRENT_LIST_DIR}/components/*.comp")
install(FILES ${ALL_COMPONENTS} DESTINATION ${Readout_DATADIR})
file(GLOB ALL_MCCODE_LIBS "${CMAKE_CURRENT_LIST_DIR}/components/lib-*")
install(FILES ${ALL_MCCODE_LIBS} DESTINATION ${Readout_DATADIR})

# The full public header set: the C API headers used by the components'
# generated code, plus the C++ consumer surface (reader, replay, Collector,
# Sender, ...) used by tools like mccode-plumber. C++ consumers must provide
# HighFive/HDF5 headers themselves (they are not part of the export).
install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/include/" DESTINATION ${Readout_INCDIR})
install(FILES "${CMAKE_BINARY_DIR}/gen/version.hpp" DESTINATION ${Readout_INCDIR})
