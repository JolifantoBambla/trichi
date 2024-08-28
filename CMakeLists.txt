cmake_minimum_required(VERSION 3.28)
project(per_meshlet_nuances)

set(CMAKE_CXX_STANDARD 20)

# add dependencies
include(cmake/CPM.cmake)

CPMAddPackage("gh:zeux/meshoptimizer#v0.21")
include(cmake/metis.cmake)
CPMAddPackage("gh:nmwsharp/geometry-central#v0.1.0")
CPMAddPackage("gh:assimp/assimp#v5.4.2")
CPMAddPackage(
        NAME BS_thread_pool
        GITHUB_REPOSITORY bshoshany/thread-pool
        VERSION 4.1.0)
add_library(BS_thread_pool INTERFACE)
target_include_directories(BS_thread_pool INTERFACE ${BS_thread_pool_SOURCE_DIR}/include)
CPMAddPackage("gh:p-ranav/argparse#v3.0")


# lib
add_library(per_meshlet_nuances
        src/per_meshlet_nuances.cpp
        src/common.cpp
        src/sequential.cpp
        src/metis.cpp)
target_include_directories(per_meshlet_nuances PUBLIC
        include)
target_link_libraries(per_meshlet_nuances
        meshoptimizer
        libmetis
        geometry-central
        BS_thread_pool
)

# example
add_executable(example
        src/main.cpp)
target_link_libraries(example
        per_meshlet_nuances
        assimp
        argparse::argparse)
