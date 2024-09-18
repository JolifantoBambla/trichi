import * as Comlink from "https://unpkg.com/comlink/dist/esm/comlink.mjs";
import {threads} from "https://unpkg.com/wasm-feature-detect?module";
import {mat4n} from 'https://wgpu-matrix.org/dist/3.x/wgpu-matrix.module.min.js';

import TrichiJs from './trichi/trichi-wasm.js';

let trichi;
let threadPoolSize = 1;

async function processModel(file, onModelProcessed) {
    if (!trichi) {
        const hasThreads = await threads();
        console.log('has threads', hasThreads);
        threadPoolSize = hasThreads ? navigator.hardwareConcurrency : 1;
        console.log('thread pool size', threadPoolSize);
        trichi = await new TrichiJs({maxThreads: threadPoolSize});
    }

    const trichiParams = {
        maxVerticesPerCluster: 64,
        maxTrianglesPerCluster: 128,
        clusterConeWeight: 0.0,
        targetClustersPerGroup: 4,
        maxHierarchyDepth: 25,
        threadPoolSize,
    };
    const mesh = trichi.buildTriangleClusterHierarchyFromFileBlob(
        file.name,
        file.bytes,
        trichiParams,
    );

    const aabb = {
        min: [mesh.vertices[0], mesh.vertices[1], mesh.vertices[2]],
        max: [mesh.vertices[0], mesh.vertices[1], mesh.vertices[2]],
    };
    for (let i = 3; i < mesh.vertices.length; i += 3) {
        aabb.min[0] = Math.min(aabb.min[0], mesh.vertices[i]);
        aabb.min[1] = Math.min(aabb.min[1], mesh.vertices[i + 1]);
        aabb.min[2] = Math.min(aabb.min[2], mesh.vertices[i + 2]);
        aabb.max[0] = Math.max(aabb.max[0], mesh.vertices[i]);
        aabb.max[1] = Math.max(aabb.max[1], mesh.vertices[i + 1]);
        aabb.max[2] = Math.max(aabb.max[2], mesh.vertices[i + 2]);
    }

    const diag = [
        aabb.max[0] - aabb.min[0],
        aabb.max[1] - aabb.min[1],
        aabb.max[2] - aabb.min[2],
    ];
    const scalingFactor = 10.0 / Math.max(...diag);
    const transform = mat4n.multiply(
        mat4n.uniformScaling(scalingFactor),
        mat4n.translation([-diag[0] / 2, -diag[1] / 2, -diag[2] / 2]),
    );

    mesh.numMeshlets = mesh.clusters.length / 4;

    onModelProcessed(
        Comlink.transfer(
            mesh,
            [
                mesh.vertices.buffer,
                mesh.indices.buffer,
                mesh.clusters.buffer,
                mesh.meshletVertices.buffer,
                mesh.meshletTriangles.buffer,
                mesh.errors.buffer,
                mesh.bounds.buffer,
            ],
        ),
        transform,
        scalingFactor,
    );
}

Comlink.expose(processModel);
