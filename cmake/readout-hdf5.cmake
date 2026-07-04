# HighFive is used to simplify the interface with HDF5:
find_package(HighFive REQUIRED)

set(READOUT_HIGHFIVE_TARGET "")
if (TARGET HighFive)
    set(READOUT_HIGHFIVE_TARGET HighFive)
elseif (TARGET HighFive::HighFive)
    set(READOUT_HIGHFIVE_TARGET HighFive::HighFive)
elseif (TARGET highfive::highfive)
    set(READOUT_HIGHFIVE_TARGET highfive::highfive)
else()
    message(FATAL_ERROR "Could not find a known HighFive CMake target")
endif()

# Make the HighFive and HDF5 headers available directly to the readout build on
# all generators. HighFive's own headers include HDF5 C headers (e.g.
# H5Ppublic.h) so both include paths are required. Using the package variables
# directly avoids relying on imported-target INTERFACE_INCLUDE_DIRECTORIES
# propagation, which is fragile with MSVC multi-config project generation.
#
# Note: we iterate over each path individually so that $<BUILD_INTERFACE:...>
# wraps a single path per call, avoiding issues with semicolons inside genexes.
foreach(_hf_inc IN LISTS HighFive_INCLUDE_DIRS HDF5_INCLUDE_DIRS)
    target_include_directories(${READOUT_LIBRARY_TARGET} PRIVATE ${_hf_inc})
    target_include_directories(${READOUT_LIBRARY_TARGET} PUBLIC
            $<BUILD_INTERFACE:${_hf_inc}>
    )
endforeach()
unset(_hf_inc)

# Link HighFive (and transitively HDF5) for the build tree only.
# $<INSTALL_INTERFACE:> is empty so installed consumers of Readout::readout
# are not required to provide HighFive themselves.
foreach(HF_TARGET IN LISTS CXX_TARGETS)
    target_link_libraries(${HF_TARGET} PUBLIC
            $<BUILD_INTERFACE:${READOUT_HIGHFIVE_TARGET}>
            $<INSTALL_INTERFACE:>
    )
endforeach()