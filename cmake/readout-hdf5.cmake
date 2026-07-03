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

# Explicitly propagate HighFive include dirs onto the readout library for
# build-tree consumers. On MSVC multi-config generators the generator
# expression $<BUILD_INTERFACE:HighFive> in target_link_libraries is not
# always sufficient to carry include paths, so we set them explicitly here.
target_include_directories(${READOUT_LIBRARY_TARGET} PUBLIC
        $<BUILD_INTERFACE:$<TARGET_PROPERTY:${READOUT_HIGHFIVE_TARGET},INTERFACE_INCLUDE_DIRECTORIES>>
)

# Link HighFive (and transitively HDF5) for the build tree only.
# $<INSTALL_INTERFACE:> is empty so installed consumers of Readout::readout
# are not required to provide HighFive themselves.
foreach(HF_TARGET IN LISTS CXX_TARGETS)
    target_link_libraries(${HF_TARGET} PUBLIC
            $<BUILD_INTERFACE:${READOUT_HIGHFIVE_TARGET}>
            $<INSTALL_INTERFACE:>
    )
endforeach()