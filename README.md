# Tri Chi: Triangle Cluster Hierarchy (WIP)

A library for generating triangle cluster hierarchies for per-surface-patch LOD selection & rendering.

Note that this library only preprocesses triangle meshes but provides no rendering solution. Check out the [WebGPU demo](https://jolifantobambla.github.io/trichi/) (**takes its sweet time to load!**) to see what a very naive renderer for such cluster hierarchies could look like.

**This library is still in development. The API is not yet stable and the implementation is neither optimized for runtime performance nor quality, nor does it handle all edge cases.
If you want to help change that, you are more than welcome to submit a PR :)**

## Getting Started

### CPM

```cmake
CPMAddPackage("gh:JolifantoBambla/trichi#v0.1.0")

target_link_libraries(${YOUR_TARGET} trichi)
```

### CMake options

 - `TRICHI_PARALLEL`: build multithreaded version

## Usage

```cpp
#include "trichi.hpp"

const std::vector<uint32_t> indices = /* triangle indices */
const std::vector<float> vertices = /* the first three floats of a vertex should be its 3d position */
const auto clusterHierarchy = trichi::buildClusterHierarchy(
  indices,
  vertices,
  vertexStrideInBytes,
  trichi::Params {
    .maxVerticesPerCluster: 64,
    .maxTrianglesPerCluster: 128,
    .clusterConeWeight: 0.0,
    .targetClustersPerGroup: 4,
    .maxHierarchyDepth: 25,
    .threadPoolSize: std::thread::hardware_concurrency(),
  });
```

## Dependencies

 - [meshoptimizer](https://github.com/zeux/meshoptimizer): used for triangle clustering and mesh simplification, MIT licensed
 - [METIS](https://github.com/KarypisLab/METIS): used for grouping neighboring triangle clusters, Apache 2.0 licensed
 - [BS::thread_pool](https://github.com/bshoshany/thread-pool) (if built with the `TRICHI_PARALLEL` option): used for parallelizing some dag construction steps, MIT licensed

## Caveats

### No faceted meshes supported yet

The algorithm builds on the assumption that the input mesh is contiguous. It is currently the user's responsibility to ensure that this condition is satisfied, e.g., by welding similar vertices beforehand.

## Related Projects

 - [Nanite (Unreal Engine 5)](https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-virtualized-geometry-in-unreal-engine)
 - [Nexus](https://github.com/cnr-isti-vclab/nexus): The OG triangle cluster DAG. This data format is open source and supported by Three.js.
 - [Carrot Engine](https://github.com/jglrxavpok/Carrot): A WIP game engine supporting virtualized geometry including hardware-accelerated ray tracing of Nanite-like cluster hierarchies.
 - [THREE Nanite](https://github.com/AIFanatic/three-nanite): A proof of concept for constructing & rendering Nanite-like cluster hierarchies in Three.js.
 - [Nanite WebGPU](https://github.com/Scthe/nanite-webgpu): A proof of concept for constructing & rendering Nanite-like cluster hierarchies in WebGPU. Includes software rasterization for very small triangles.
 - [meshoptimizer](https://github.com/zeux/meshoptimizer): A wild nanite demo appeared! Looks like Zeux is working on a new addition to the meshoptimizer library <3

## Further Reading

 - [Karis et al., "A Deep Dive into Nanite Virtualized Geometry"](https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf)
 - [Ponchio, "Multiresolution structures for interactive visualization of very large 3D datasets"](https://vcg.isti.cnr.it/~ponchio/download/ponchio_phd.pdf)
 - [Cignoni et al., "Batched Multi Triangulation"](https://ieeexplore.ieee.org/document/1532797)
 - [Yoon et al., "Quick-VDR: out-of-core view-dependent rendering of gigantic models"](https://ieeexplore.ieee.org/document/1432683)
 - [Benthin and Peters, "Real-Time Ray Tracing of Micro-Poly Geometry with Hierarchical Level of Detail"](https://onlinelibrary.wiley.com/doi/10.1111/cgf.14868)
 - [Cao, "Seamless Rendering on Mobile: The Magic of Adaptive LOD Pipeline"](https://advances.realtimerendering.com/s2024/content/Cao-NanoMesh/AdavanceRealtimeRendering_NanoMesh0810.pdf)
 - [jglrxavpok, "Recreating Nanite"](https://jglrxavpok.github.io/2023/11/12/recreating-nanite-the-plan.html)
