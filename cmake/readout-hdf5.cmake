# HighFive is used to simplify the interface with HDF5:
find_package(HighFive REQUIRED)
foreach(HF_TARGET IN LISTS CXX_TARGETS)
    target_link_libraries(${HF_TARGET} PRIVATE HighFive)
endforeach()