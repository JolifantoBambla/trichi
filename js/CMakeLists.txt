CPMAddPackage("gh:assimp/assimp#v5.4.2")

add_executable(trichi-wasm trichi-wasm.cpp)
target_link_libraries(trichi-wasm trichi assimp)
target_compile_options(trichi-wasm PRIVATE
        -Os
)
target_link_options(trichi-wasm PRIVATE
        -lembind
        -Os
        -sEXPORT_ES6=1
        -sMODULARIZE=1
        -sEXPORT_NAME="TrichiJs"
        -sALLOW_MEMORY_GROWTH=1
        --emit-tsd
        # --emit-tsd trichi.d.ts how exactly is this option supposed to work? I get: "trichi.d.ts" was expected to be an input file, based on the commandline arguments provided
)

if (TRICHI_PARALLEL)
    add_compile_options(
            -pthread
            -matomics
            -mbulk-memory
    )
    add_link_options(
            -pthread
            -matomics
            -mbulk-memory
            -sUSE_PTHREADS=1
            -sPTHREAD_POOL_SIZE="navigator.hardwareConcurrency"
    )
endif (TRICHI_PARALLEL)
