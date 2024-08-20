cmake_minimum_required(VERSION 3.29)
project(per_meshlet_nuances)

set(CMAKE_CXX_STANDARD 20)

# add dependencies
include(cmake/CPM.cmake)

CPMAddPackage("gh:zeux/meshoptimizer#v0.21")
#CPMAddPackage("gh:KarypisLab/METIS#v5.2.1")
#CPMAddPackage("gh:scivision/METIS#v5.2.1.1")

add_library(per_meshlet_nuances src/main.cpp)


add_executable(example src/main.cpp)
target_link_libraries(example meshoptimizer)
target_link_libraries(example METIS)
