cmake_minimum_required(VERSION 3.14)
project(trichi
        VERSION 0.1.0
        DESCRIPTION "Creates a hierarchical triangle cluster DAG"
        HOMEPAGE_URL "https://jolifantobambla.github.io/trichi/"
        LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)

option(TRICHI_PARALLEL "Build the library with parallelization enabled." ON)
option(TRICHI_BUILD_JS_MODULE "Build the library as a JavaScript module (currently only valid when built with emscripten)" OFF)
option(TRICHI_BUILD_CLI "Build cli (only for native builds)" ON)

# add dependencies
include(cmake/CPM.cmake)

CPMAddPackage("gh:zeux/meshoptimizer#v0.21")
include(cmake/metis.cmake)
include(cmake/thread-pool.cmake)

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
)

if (TRICHI_PARALLEL)
    target_link_libraries(trichi BS_thread_pool)
    target_compile_definitions(trichi PRIVATE TRICHI_PARALLEL)
endif (TRICHI_PARALLEL)

if (EMSCRIPTEN)
    if (TRICHI_PARALLEL)
        set(TRICHI_EMSCRIPTEN_PARALLEL_COMPILE_OPTIONS
                -pthread
                -matomics
                -mbulk-memory
        )
        set(TRICHI_EMSCRIPTEN_PARALLEL_LINK_OPTIONS
                -pthread
                -matomics
                -mbulk-memory
                -sUSE_PTHREADS=1
                -sPTHREAD_POOL_SIZE=navigator.hardwareConcurrency
        )

        target_compile_options(trichi PUBLIC ${TRICHI_EMSCRIPTEN_PARALLEL_COMPILE_OPTIONS})
        target_link_options(trichi PUBLIC ${TRICHI_EMSCRIPTEN_PARALLEL_LINK_OPTIONS})

        target_compile_options(meshoptimizer PUBLIC ${TRICHI_EMSCRIPTEN_PARALLEL_COMPILE_OPTIONS})
        target_link_options(meshoptimizer PUBLIC ${TRICHI_EMSCRIPTEN_PARALLEL_LINK_OPTIONS})

        target_compile_options(libmetis PUBLIC ${TRICHI_EMSCRIPTEN_PARALLEL_COMPILE_OPTIONS})
        target_link_options(libmetis PUBLIC ${TRICHI_EMSCRIPTEN_PARALLEL_LINK_OPTIONS})

        target_compile_options(GKlib PUBLIC ${TRICHI_EMSCRIPTEN_PARALLEL_COMPILE_OPTIONS})
        target_link_options(GKlib PUBLIC ${TRICHI_EMSCRIPTEN_PARALLEL_LINK_OPTIONS})
    endif (TRICHI_PARALLEL)

    if (TRICHI_BUILD_JS_MODULE)
        add_subdirectory(wasm)
    endif (TRICHI_BUILD_JS_MODULE)
else (EMSCRIPTEN)
    # js dump test thingy
    # todo: move to own subdirectory
    if (TRICHI_BUILD_CLI)
        CPMAddPackage("gh:assimp/assimp#v5.4.2")
        CPMAddPackage("gh:p-ranav/argparse#v3.0")

        add_executable(dump_trichi_js
                src/main.cpp)
        target_link_libraries(dump_trichi_js PUBLIC
                trichi
                assimp
                argparse::argparse)
    endif (TRICHI_BUILD_CLI)
endif (EMSCRIPTEN)
