import {threads} from 'wasm-feature-detect';

/**
 * Tuning parameters for generating triangle cluster hierarchies
 */
export interface Params {
    /**
     * The maximum number of vertices per cluster.
     */
    maxVerticesPerCluster: number,

    /**
     * The maximum number of triangles per cluster.
     */
    maxTrianglesPerCluster: number,

    /**
     * A weighting factor for the importance of cluster normal cones used when building the clusters.
     * In range [0..1].
     */
    clusterConeWeight: number,

    /**
     * The target number of clusters per group.
     */
    targetClustersPerGroup: number,

    /**
     * The maximum number of iterations when building the hierarchy.
     * In each iteration, the number of triangles is approximately halved.
     */
    maxHierarchyDepth: number,

    /**
     * The size of the thread pool used for parallelizing DAG building steps.
     * If `trichi` is not built with multithreading enabled, this is ignored.
     * If this is 0, defaults to 1.
     */
    threadPoolSize: number,
}

/**
 * A triangle cluster hierarchy
 */
export interface TriangleClusterHierarchy {
    /**
     * The model's vertex indices
     */
    indices: Uint32Array,

    /**
     * The model's vertices
     */
    vertices: Float32Array,

    /**
     * Error bounds of clusters.
     * Used for LOD selection.
     *
     * For each cluster this stores 10 floats: its parent group's error (first 5 floats) and its own error (second 5 floats)
     * Each error bound stores:
     *  - the bounding sphere's center                      3 floats
     *  - the bounding sphere's radius                      1 float
     *  - the cluster's absolute simplification error       1 float
     */
    errors: Float32Array,

    /**
     * Bounds of clusters.
     * Used for cluster culling.
     *
     * For each cluster this stores 4 floats:
     *  - the cluster's tight bounding sphere's center      3 floats
     *  - the cluster's tight bounding sphere's radius      1 float
     */
    bounds: Float32Array,

    /**
     * Clusters in the hierarchy.
     *
     * Each cluster consists of 4 unsigned integers:
     *  - the cluster's offset in the array of cluster vertex
     *  - the cluster's offset in the array of cluster triangles
     *  - the number of vertex indices used by the cluster
     *  - the number of triangles in the cluster
     */
    clusters: Uint32Array,

    /**
     * Vertex indices of the clusters in the hierarchy.
     *
     * The first and last (exclusive) vertices of a cluster with index c are:
     *    clusterVertices[clusters[c]], clusterVertices[clusters[c] + clusters[c + 2]
     */
    clusterVertices: Float32Array,

    /**
     * Triangles (triplets of indices into `clusterVertices`) of the clusters in the hierarchy.
     *
     * The first and last (exclusive) triangles of a cluster with index c are:
     *    clusterTriangles[clusters[c + 1], clusterTriangles[clusters[c + 1] + clusters[c + 3] * 3]
     */
    clusterTriangles: Uint32Array,
}

export interface Trichi {
    /**
     * Builds a cluster hierarchy for a given triangle mesh.
     *
     * Note that faceted meshes are currently not supported.
     * It is currently the user's responsibility to ensure the input mesh is contiguous, e.g., by first welding similar vertices.
     *
     * @param indices vertex indices of the input mesh
     * @param vertices vertices of the input mesh - the first 3 floats of a vertex are expected to store the position
     * @param vertexStrideBytes the size of each vertex in the vertices array in bytes
     * @param params tuning parameters for building the cluster hierarchy
     */
    buildTriangleClusterHierarchy(indices: Uint32Array, vertices: Float32Array, vertexStrideBytes: number, params: Params): TriangleClusterHierarchy,

    /**
     * Builds a cluster hierarchy for a 3d model given as a file blob.
     *
     * This function is currently more for demonstration purposes, as it...
     *  - ignores vertex attributes: The returned vertices will only contain position data.
     *  - creates the hierarchy only for the first mesh that is found in the file blob
     *
     * @param fileName the name of the file blob, used as a file type hint when loading model data
     * @param bytes the raw bytes containing the model data
     * @param params tuning parameters for building the cluster hierarchy
     */
    buildTriangleClusterHierarchyFromFileBlob(fileName: string, bytes: Uint8Array, params: Params): TriangleClusterHierarchy,
}

/**
 * Initializes a {@link Trichi} module.
 *
 * @param maxThreadPoolSize sets the maximum number of threads in the module's thread pool. In environments that do not support multithreading, this is ignored.
 */
export default async function initTrichiJs(maxThreadPoolSize: number = navigator.hardwareConcurrency): Promise<Trichi> {
    const moduleName = (await threads()) ? './wasm/trichi-wasm-threads.js' : './wasm/trichi-wasm.js';

    const module = await import(moduleName) as unknown;
    // @ts-expect-error we don't care if the module's type is unknown here
    return new module.default({maxThreads: Math.min(maxThreadPoolSize, navigator.hardwareConcurrency)}) as Promise<Trichi>;
}
