# The C++ test suite: white-box tests of the readout_core library plus the
# McStas-toolchain integration script.

set(READOUT_TESTER_TARGET readout_tester)

find_package(Catch2 REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(${READOUT_TESTER_TARGET})
file(GLOB CATCH_SOURCES *_test.cpp)
target_sources(${READOUT_TESTER_TARGET} PRIVATE ${CATCH_SOURCES})
target_include_directories(${READOUT_TESTER_TARGET} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        # white-box access to internal headers (RecordBuffer, samplers, ...):
        "${PROJECT_SOURCE_DIR}/readout_core/src"
        "${PROJECT_SOURCE_DIR}/readout_core/src/ctream"
)
target_link_libraries(${READOUT_TESTER_TARGET} PRIVATE Catch2::Catch2WithMain Threads::Threads readout)
addCheckDependency(${READOUT_TESTER_TARGET})

if (WIN32)
    # hdf5.dll (shared on Windows, see conanfile.py) next to the tester:
    add_custom_command(TARGET ${READOUT_TESTER_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_RUNTIME_DLLS:${READOUT_TESTER_TARGET}>" "$<TARGET_FILE_DIR:${READOUT_TESTER_TARGET}>"
        COMMAND_EXPAND_LISTS)
endif()

include(Catch)
catch_discover_tests(${READOUT_TESTER_TARGET})

find_program(BASH_PROGRAM bash)
if (BASH_PROGRAM)
    add_test(integration "${BASH_PROGRAM}" "${CMAKE_CURRENT_SOURCE_DIR}/test_integration.sh")
    set_tests_properties(integration PROPERTIES
        SKIP_RETURN_CODE 100
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
endif(BASH_PROGRAM)
