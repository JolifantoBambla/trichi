import {mesh} from './test-mesh.js'; //'./demo-mesh.js';
import {renderClusterWgsl} from './render-clusters-shader.js';

function makeVertexBuffer(device) {
    const vertexBuffer = device.createBuffer({
        label: 'vertices',
        size: mesh.vertices.byteLength,
        usage: GPUBufferUsage.STORAGE,
        mappedAtCreation: true,
    });
    (new Float32Array(vertexBuffer.getMappedRange())).set(mesh.vertices);
    vertexBuffer.unmap();
    return vertexBuffer;
}

function makeMeshletBuffers(device) {
    const meshletsBuffer = device.createBuffer({
        label: 'meshlets',
        size: mesh.meshlets.byteLength,
        usage: GPUBufferUsage.STORAGE,
        mappedAtCreation: true,
    });
    const meshletVerticesBuffer = device.createBuffer({
        label: 'meshlet vertices',
        size: mesh.meshletVertices.byteLength,
        usage: GPUBufferUsage.STORAGE,
        mappedAtCreation: true,
    });
    const meshletTrianglesBuffer = device.createBuffer({
        label: 'meshlet triangles',
        size: mesh.meshletTriangles.byteLength,
        usage: GPUBufferUsage.STORAGE,
        mappedAtCreation: true,
    });
    (new Uint32Array(meshletsBuffer.getMappedRange())).set(mesh.meshlets);
    (new Uint32Array(meshletVerticesBuffer.getMappedRange())).set(mesh.meshletVertices);
    (new Uint32Array(meshletTrianglesBuffer.getMappedRange())).set(mesh.meshletTriangles);
    meshletsBuffer.unmap();
    meshletVerticesBuffer.unmap();
    meshletTrianglesBuffer.unmap();
    return {
        meshletsBuffer,
        meshletVerticesBuffer,
        meshletTrianglesBuffer,
    };
}


function makeRenderClusterPipeline(device, colorFormat, depthFormat, reverseZ) {
    const meshletModule = device.createShaderModule({
        label: 'render clusters',
        code: renderClusterWgsl,
    });
    const pipeline = device.createRenderPipeline({
        label: 'meshlet',
        layout: 'auto',
        vertex: {
            module: meshletModule,
            buffers: [],
            constants: {
                VERTEX_STRIDE_FLOATS: mesh.strideFloats,
            },
        },
        primitive: {
            topology: 'triangle-list',
            cullMode: 'back',
        },
        fragment: {
            module: meshletModule,
            targets: [{format: colorFormat}],
        },
        depthStencil: {
            depthWriteEnabled: true,
            depthCompare: reverseZ ? 'greater' : 'less',
            format: depthFormat,
        },
    });

    return pipeline;
}

function numLodClusters(lod) {
    return lod < (mesh.lods.length - 1) ? mesh.lods[lod + 1] - mesh.lods[lod] : mesh.numMeshlets - mesh.lods[lod];
}

export function makeClusterRenderer(device, colorFormat = 'rgba16float', depthFormat = 'depth24plus', reverseZ = true) {
    const pipeline = makeRenderClusterPipeline(device, colorFormat, depthFormat, reverseZ);

    const cameraBuffer = device.createBuffer({
        label: 'camera',
        size: Float32Array.BYTES_PER_ELEMENT * 16 * 2,
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });
    const uniformsBindGroup = device.createBindGroup({
        label: 'uniforms',
        layout: pipeline.getBindGroupLayout(0),
        entries: [
            {binding: 0, resource: {buffer: cameraBuffer}},
        ],
    });

    const numInstances = 1;
    const instancesBuffer = device.createBuffer({
        label: 'instances',
        size: Float32Array.BYTES_PER_ELEMENT * 16 * numInstances,
        usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
    });

    const meshletBuffers = makeMeshletBuffers(device);
    const vertexBuffer = makeVertexBuffer(device);

    const meshletBindGroup = device.createBindGroup({
        label: 'mesh & instance data',
        layout: pipeline.getBindGroupLayout(1),
        entries: [
            {binding: 0, resource: {buffer: instancesBuffer}},
            {binding: 1, resource: {buffer: meshletBuffers.meshletsBuffer}},
            {binding: 2, resource: {buffer: meshletBuffers.meshletVerticesBuffer}},
            {binding: 3, resource: {buffer: meshletBuffers.meshletTrianglesBuffer}},
            {binding: 4, resource: {buffer: vertexBuffer}},
        ],
    });

    const lodBuffers = [];
    for (let i = 0; i < mesh.lods.length; ++i) {
        const lodBuffer = device.createBuffer({
            label: `lod ${i} instances`,
            size: Uint32Array.BYTES_PER_ELEMENT * 2 * numLodClusters(i) * numInstances,
            usage: GPUBufferUsage.STORAGE,
            mappedAtCreation: true,
        });
        (new Uint32Array(lodBuffer.getMappedRange())).set(new Uint32Array(Array(numLodClusters(i) * numInstances).fill(0).map((_, c) => {
            return [
                Math.floor(c / numLodClusters(i)),
                mesh.lods[i] + Math.floor(c % numLodClusters(i)),
            ];
        }).flat()));
        lodBuffer.unmap();
        lodBuffers.push(lodBuffer);
    }
    const lodBindGroups = lodBuffers.map(b => {
        return device.createBindGroup({
            layout: pipeline.getBindGroupLayout(2),
            entries: [
                {binding: 0, resource: {buffer: b}},
            ],
        });
    });

    console.log(numLodClusters(0), numInstances, mesh.meshlets.length / 4, mesh.numMeshlets, mesh.strideFloats, mesh.vertices.length / mesh.strideFloats);

    return {
        update({view, projection}, {instances}) {
            device.queue.writeBuffer(cameraBuffer, 0, new Float32Array([...view, ...projection]));
            device.queue.writeBuffer(instancesBuffer, 0, new Float32Array([...instances.flat()]));
        },
        encode(passEncoder, lod = 0) {
            lod = Math.min(Math.max(lod, 0), mesh.lods.length - 1);
            passEncoder.setPipeline(pipeline);
            passEncoder.setBindGroup(0, uniformsBindGroup);
            passEncoder.setBindGroup(1, meshletBindGroup);
            passEncoder.setBindGroup(2, lodBindGroups[lod]);
            passEncoder.draw(mesh.maxClusterTriangles * 3, numLodClusters(lod) * numInstances);
        },
        aabb: mesh.aabb,
        numLods: mesh.lods.length,
        numInstances,
    };
}
