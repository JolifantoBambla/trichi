CPMAddPackage("gh:assimp/assimp#v5.4.2")
CPMAddPackage("gh:zeux/meshoptimizer#v0.23")

if (TRICHI_PARALLEL)
    target_compile_options(assimp PUBLIC ${TRICHI_EMSCRIPTEN_PARALLEL_COMPILE_OPTIONS})
    target_link_options(assimp PUBLIC ${TRICHI_EMSCRIPTEN_PARALLEL_LINK_OPTIONS})
endif (TRICHI_PARALLEL)

add_executable(trichi-wasm trichi-wasm.cpp)
target_link_libraries(trichi-wasm trichi assimp meshoptimizer)
target_compile_options(trichi-wasm PUBLIC
        -O3
)
target_link_options(trichi-wasm PUBLIC
        -lembind
        -O3
        -sEXPORT_ES6=1
        -sMODULARIZE=1
        -sEXPORT_NAME="TrichiJs"
        -sALLOW_MEMORY_GROWTH=1
        --emit-tsd
        # --emit-tsd trichi.d.ts how exactly is this option supposed to work? I get: "trichi.d.ts" was expected to be an input file, based on the commandline arguments provided
)

if (TRICHI_PARALLEL)
    set_target_properties(trichi-wasm PROPERTIES
            OUTPUT_NAME trichi-wasm-threads
    )
endif (TRICHI_PARALLEL)
