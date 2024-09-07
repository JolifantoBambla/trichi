cmake_minimum_required(VERSION 3.28)
project(trichi
        VERSION 0.1.0
        DESCRIPTION "Creates a hierarchical triangle cluster DAG"
        HOMEPAGE_URL "https://jolifantobambla.github.io/per-meshlet-nuances/"
        LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)

option(TRICHI_PARALLEL "Build the library with parallelization enabled." ON)

# add dependencies
include(cmake/CPM.cmake)

CPMAddPackage("gh:zeux/meshoptimizer#v0.21")
include(cmake/metis.cmake)
CPMAddPackage("gh:nmwsharp/geometry-central#v0.1.0") # todo: remove
CPMAddPackage("gh:assimp/assimp#v5.4.2")
CPMAddPackage(
        NAME BS_thread_pool
        GITHUB_REPOSITORY bshoshany/thread-pool
        VERSION 4.1.0)
add_library(BS_thread_pool INTERFACE)
target_include_directories(BS_thread_pool INTERFACE ${BS_thread_pool_SOURCE_DIR}/include)
CPMAddPackage("gh:p-ranav/argparse#v3.0") # todo: move to own subdirectory with example target

# lib
add_library(trichi
        src/trichi.cpp
        src/common.cpp
        src/metis.cpp)
target_include_directories(trichi PUBLIC
        include)
target_include_directories(trichi PRIVATE
        src
)
target_link_libraries(trichi
        meshoptimizer
        libmetis
        geometry-central
        BS_thread_pool
)

if (TRICHI_PARALLEL)
    target_compile_definitions(trichi PRIVATE TRICHI_PARALLEL)
endif (TRICHI_PARALLEL)

# example
# todo: move to own subdirectory
add_executable(example
        src/main.cpp)
target_link_libraries(example
        trichi
        assimp
        argparse::argparse)
