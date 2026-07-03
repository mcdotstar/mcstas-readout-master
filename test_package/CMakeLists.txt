cmake_minimum_required(VERSION 3.20)
project(ReadoutTestPackage LANGUAGES C)

find_package(Readout REQUIRED)

add_executable(consumer src/main.c)
target_link_libraries(consumer PRIVATE Readout::readout)
