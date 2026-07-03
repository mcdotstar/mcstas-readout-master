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

# Make the HighFive headers available directly to the readout build on all
# generators. This avoids relying on imported target propagation, which can be
# fragile with MSVC's multi-config project generation.
target_include_directories(${READOUT_LIBRARY_TARGET} PRIVATE
        ${HighFive_INCLUDE_DIRS}
)
target_include_directories(${READOUT_LIBRARY_TARGET} PUBLIC
        $<BUILD_INTERFACE:${HighFive_INCLUDE_DIRS}>
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