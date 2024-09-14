add_executable(trichi-wasm trichi-wasm.cpp)
target_link_libraries(trichi-wasm trichi)
target_compile_options(trichi-wasm PUBLIC
        -Os
        -sMODULARIZE
        -sEXPORT_NAME="trichijs"
)
target_link_options(trichi-wasm PUBLIC
        -lembind
        -Os
        -sMODULARIZE
        -sEXPORT_NAME="trichijs"
        -sALLOW_MEMORY_GROWTH=1
        # --emit-tsd trichi.d.ts how exactly is this option supposed to work? I get: "trichi.d.ts" was expected to be an input file, based on the commandline arguments provided
)

if (TRICHI_PARALLEL)
    target_compile_options(trichi-wasm PUBLIC
            -pthread
            -sUSE_PTHREADS=1
            -matomics
            -mbulk-memory
    )
    target_link_options(trichi-wasm PUBLIC
            --no-check-features
            -pthread
            -sUSE_PTHREADS=1
            -matomics
            -mbulk-memory
            -sPTHREAD_POOL_SIZE="navigator.hardwareConcurrency"
    )
endif (TRICHI_PARALLEL)
