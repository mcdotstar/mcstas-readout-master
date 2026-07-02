list(APPEND LIB_SOURCES
        readout_orig.cpp
        readout_collector.cpp
        readout_discrete.cpp
        Readout_merge.cpp
        ReadoutClass.cpp
        enums.cpp
        hdf_interface.cpp
        replay.cpp
        Sender.cpp
        SenderConfigs.cpp
        CollectorClass.cpp
        TypeDescriptionParser.cpp
        CollectorStar.cpp
        readout_type_descriptions.cpp
        Array.cpp
)

foreach(LIB_SOURCE IN LISTS LIB_SOURCES)
    #    message(STATUS "Setting compile options for ${CXX_SOURCE} to ${SOURCE_FILE_COMPILE_OPTIONS}")
    set_source_files_properties(${LIB_SOURCE} PROPERTIES COMPILE_OPTIONS "${SOURCE_FILE_COMPILE_OPTIONS}")
endforeach()

foreach(LIB_TARGET IN LISTS LIB_TARGETS)
    target_sources(${LIB_TARGET} PRIVATE ${LIB_SOURCES})
    #target_include_directories(${CXX_TARGET} PUBLIC ${CMAKE_CURRENT_LIST_DIR})
    target_include_directories(
            ${LIB_TARGET}
            INTERFACE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
            $<INSTALL_INTERFACE:>  # <prefix>
    )
    target_include_directories(${LIB_TARGET} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/ctream")
endforeach()