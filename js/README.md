# Tri Chi JavaScript Bindings

## Install dependencies

```
sudo apt-get install emscripten
```

## Build

```bash
# with multithreading
mkdir build-par && cd build-par
cmake -DCMAKE_TOOLCHAIN_FILE=/usr/share/emscripten/cmake/Modules/Platform/Emscripten.cmake -DTRICHI_BUILD_JS_MODULE=ON -DASSIMP_BUILD_ZLIB=ON -G "Ninja" ..
cmake --build . --target trichi-wasm
cd ..

# without multithreading
mkdir build-seq && cd build-seq
cmake -DCMAKE_TOOLCHAIN_FILE=/usr/share/emscripten/cmake/Modules/Platform/Emscripten.cmake -DTRICHI_PARALLEL=OFF -DTRICHI_BUILD_JS_MODULE=ON -DASSIMP_BUILD_ZLIB=ON -G "Ninja" ..
cmake --build . --target trichi-wasm
cd ..

# copy results
mkdir js
mkdir js/parallel && cp -a build/js/*.js js/parallel/ && cp -a build/js/*.wasm js/parallel/
mkdir js/sequential && cp -a build-seq/js/*.js js/sequential/ && cp -a build-seq/js/*.wasm js/sequential/

# cleanup
rm -rf build-par
rm -rf build-seq
```

## Usage

```js
import TrichiJs from './trichi-wasm.js';

new TrichiJs().then(trichi => {
    const triangleClusterHierarchy = trichi.buildTriangleClusterHierarchy(
        indices,
        vertices,
        vertexStrideInBytes,
        {
            maxVerticesPerCluster: 64,
            maxTrianglesPerCluster: 128,
            clusterConeWeight: 0.0,
            targetClustersPerGroup: 4,
            maxHierarchyDepth: 25,
            threadPoolSize: 1,
        }
    );
    // do something with cluster hierarchy
});
```
