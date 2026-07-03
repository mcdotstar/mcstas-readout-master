# HighFive is used to simplify the interface with HDF5:
find_package(HighFive REQUIRED)

# Public C++ headers (e.g. hdf_interface.h) include HighFive headers, so
# build-tree consumers of Readout::readout need include visibility.
target_include_directories(${READOUT_LIBRARY_TARGET} PUBLIC
        $<BUILD_INTERFACE:$<TARGET_PROPERTY:HighFive,INTERFACE_INCLUDE_DIRECTORIES>>
)

foreach(HF_TARGET IN LISTS CXX_TARGETS)
    target_link_libraries(${HF_TARGET} PRIVATE HighFive)
endforeach()