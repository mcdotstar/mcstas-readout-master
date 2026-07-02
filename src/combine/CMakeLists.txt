list(APPEND COMBINE_BIN_SOURCES
        main.cpp
)

foreach(BIN_SOURCE IN LISTS COMBINE_BIN_SOURCES)
    set_source_files_properties(${BIN_SOURCE} PROPERTIES COMPILE_OPTIONS "${SOURCE_FILE_COMPILE_OPTIONS}")
endforeach()

foreach(BIN_TARGET IN LISTS COMBINE_BIN_TARGETS)
    target_sources(${BIN_TARGET} PRIVATE ${COMBINE_BIN_SOURCES})
    target_include_directories(${BIN_TARGET} PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${ARGS_INCLUDE_DIR})
endforeach()


if (READOUT_BUILD_TESTS)
    add_test(NAME combine_binary_help_test COMMAND ${READOUT_COMBINE_BINARY} --help)
    add_test(NAME combine_validate_missing_test COMMAND ${READOUT_COMBINE_BINARY} validate /nonexistent_file_for_testing.h5)
    set_tests_properties(combine_validate_missing_test PROPERTIES WILL_FAIL TRUE)
endif()
