# Tri Chi JavaScript Bindings

## Install dependencies

```
sudo apt-get install emscripten
```

## Build

```
mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=/usr/share/emscripten/cmake/Modules/Platform/Emscripten.cmake -DTRICHI_BUILD_CLI=OFF -DASSIMP_BUILD_ZLIB=ON -G "Ninja" ..
cmake --build . --target trichi-wasm
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
