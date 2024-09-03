# per-meshlet-nuances
Poor man's nanite


## Dependencies

 - [meshoptimizer](https://github.com/zeux/meshoptimizer): for triangle clustering and mesh simplification
 - [METIS](https://github.com/KarypisLab/METIS): for grouping neighboring triangle clusters
 - [BS::thread_pool](https://github.com/bshoshany/thread-pool) (if built with multithreading option): for parallelizing some dag construction steps

## Related Projects

 - [Nanite (Unreal Engine 5)](https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-virtualized-geometry-in-unreal-engine)
 - [Nexus](https://github.com/cnr-isti-vclab/nexus): The OG triangle cluster DAG. Try it in Three.js!
 - [Carrot Engine](https://github.com/jglrxavpok/Carrot)
 - [THREE Nanite](https://github.com/AIFanatic/three-nanite)
 - [Nanite WebGPU](https://github.com/Scthe/nanite-webgpu)

## Further Reading

 - [Karis et al., "A Deep Dive into Nanite Virtualized Geometry"](https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf)
 - [Cignoni et al., "Batched Multi Triangulation"](https://ieeexplore.ieee.org/document/1532797)
 - [Yoon et al., "Quick-VDR: out-of-core view-dependent rendering of gigantic models"](https://ieeexplore.ieee.org/document/1432683)
 - [Cao, "Seamless Rendering on Mobile: The Magic of Adaptive LOD Pipeline"](https://advances.realtimerendering.com/s2024/content/Cao-NanoMesh/AdavanceRealtimeRendering_NanoMesh0810.pdf)
 - [jglrxavpok, "Recreating Nanite"](https://jglrxavpok.github.io/2023/11/12/recreating-nanite-the-plan.html)
