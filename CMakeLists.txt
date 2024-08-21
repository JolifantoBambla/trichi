cmake_minimum_required(VERSION 3.29)
project(per_meshlet_nuances)

set(CMAKE_CXX_STANDARD 20)

# add dependencies
include(cmake/CPM.cmake)

CPMAddPackage("gh:zeux/meshoptimizer#v0.21")
include(cmake/metis.cmake)

# lib
add_library(per_meshlet_nuances src/main.cpp)
target_link_libraries(per_meshlet_nuances meshoptimizer)
target_link_libraries(per_meshlet_nuances libmetis)

# example
add_executable(example src/main.cpp)
target_link_libraries(example meshoptimizer)
target_link_libraries(example libmetis)
