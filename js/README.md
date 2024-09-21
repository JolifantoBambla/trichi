# Tri Chi

WebAssembly & JavaScript bindings for the [Tri Chi library](https://github.com/JolifantoBambla/trichi).

**This library is still in development. The API is not yet stable and the implementation is neither optimized for runtime performance nor quality, nor does it handle all edge cases.
If you want to help change that, you are more than welcome to submit a PR :)**

## Docs

Find the docs [here](jolifantobambla.github.io/trichi/js).

Try it out [here](jolifantobambla.github.io/trichi/demo).

## Installation

### NPM

```bash
npm install trichi
```

```js
import initTrichiJs from 'trichi';
```

### From GitHub

```js
import initTrichiJs from 'https://jolifantobambla.github.io/trichi/js/dist/0.x/trichi.module.min.js';
```

## Usage

```js
import initTrichiJs from 'trichi';

initTrichiJs().then(trichi => {
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
