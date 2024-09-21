import {threads} from 'wasm-feature-detect';

export interface Params {
    maxVerticesPerCluster: number,
    maxTrianglesPerCluster: number,
    clusterConeWeight: number,
    targetClustersPerGroup: number,
    maxHierarchyDepth: number,
    threadPoolSize: number,
}

export interface TriangleClusterHierarchy {
    indices: Uint32Array,
    vertices: Float32Array,
    errors: Float32Array,
    bounds: Float32Array,
    clusters: Uint32Array,
    clusterVertices: Float32Array,
    clusterTriangles: Uint32Array,
}

export interface Trichi {
    buildTriangleClusterHierarchy(indices: Uint32Array, vertices: Float32Array, vertexStrideBytes: number, params: Params): TriangleClusterHierarchy,

    buildTriangleClusterHierarchyFromFileBlob(indices: Uint32Array, vertices: Float32Array, vertexStrideBytes: number, params: Params): TriangleClusterHierarchy,
}

export default async function initTrichiJs(maxThreadPoolSize: number): Promise<Trichi> {
    const moduleName = (await threads()) ? './wasm/trichi-wasm-threads.js' : './wasm/trichi-wasm.js';
    // @ts-ignore
    const module = await import(moduleName) as unknown;
    // @ts-ignore
    return new module.default({maxThreads: Math.min(maxThreadPoolSize, navigator.hardwareConcurrency)}) as Promise<Trichi>;
}
