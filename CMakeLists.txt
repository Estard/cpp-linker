cmake_minimum_required(VERSION 3.13)
project(cppld)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(Infrastructure)
#include(BundledBenchmark)

include(CheckIPOSupported)
check_ipo_supported() # fatal error if IPO is not supported
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE) # turn on just in general. Project scope is small enough to just do that

add_subdirectory(src)
add_subdirectory(test)