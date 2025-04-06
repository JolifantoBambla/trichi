import * as Comlink from "https://unpkg.com/comlink/dist/esm/comlink.mjs";
import {mat4n} from 'https://wgpu-matrix.org/dist/3.x/wgpu-matrix.module.min.js';

console.log('worker script');

import initTrichiJs from './trichi/trichi.module.min.js';

let trichi;
let threadPoolSize = 1;

async function processModel(file, onModelProcessed, onError) {
    try {
        if (!trichi) {
            trichi = await initTrichiJs(navigator.hardwareConcurrency);
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

        const diag = [
            mesh.aabbMax[0] - mesh.aabbMin[0],
            mesh.aabbMax[1] - mesh.aabbMin[1],
            mesh.aabbMax[2] - mesh.aabbMin[2],
        ];
        const scalingFactor = 10.0 / Math.max(...diag);
        const transform = mat4n.multiply(
            mat4n.uniformScaling(scalingFactor),
            mat4n.translation([-diag[0] / 2, -diag[1] / 2, -diag[2] / 2]),
        );

        mesh.numMeshlets = mesh.clusters.length / 4;
        mesh.meshletVertices = mesh.clusterVertices;
        mesh.meshletTriangles = mesh.clusterTriangles;

        console.log(mesh);

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
        );
    } catch (e) {
        onError(e);
    }
}

Comlink.expose(processModel);
