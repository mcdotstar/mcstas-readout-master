file(GLOB CATCH_SOURCES *_test.cpp)
target_sources(${READOUT_TESTER_TARGET} PRIVATE ${CATCH_SOURCES})
target_include_directories(${READOUT_TESTER_TARGET} PRIVATE ${CMAKE_CURRENT_LIST_DIR})


find_program(BASH_PROGRAM bash)
if (BASH_PROGRAM)
    add_test(integration "${BASH_PROGRAM}" "${CMAKE_CURRENT_SOURCE_DIR}/test_integration.sh")
    set_tests_properties(integration PROPERTIES
        SKIP_RETURN_CODE 100
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
endif(BASH_PROGRAM)