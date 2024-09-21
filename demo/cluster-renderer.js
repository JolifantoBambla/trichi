import {vec3n, vec4n, mat4n} from 'https://wgpu-matrix.org/dist/3.x/wgpu-matrix.module.min.js';

import {renderClusterWgsl} from './render-clusters-shader.js';
import {cullClustersShader} from './cull-clusters-shader.js';

const numInstances = 1;

function makeVertexBuffer(device, mesh) {
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

function makeMeshletBuffers(device, mesh) {
    const meshletsBuffer = device.createBuffer({
        label: 'meshlets',
        size: mesh.clusters.byteLength,
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
    const errorsBuffer = device.createBuffer({
        label: 'meshlet errors',
        size: mesh.errors.byteLength,
        usage: GPUBufferUsage.STORAGE,
        mappedAtCreation: true,
    });
    const boundsBuffer = device.createBuffer({
        label: 'meshlet bounds',
        size: mesh.bounds.byteLength,
        usage: GPUBufferUsage.STORAGE,
        mappedAtCreation: true,
    });
    (new Uint32Array(meshletsBuffer.getMappedRange())).set(mesh.clusters);
    (new Uint32Array(meshletVerticesBuffer.getMappedRange())).set(mesh.meshletVertices);
    (new Uint32Array(meshletTrianglesBuffer.getMappedRange())).set(mesh.meshletTriangles);
    (new Float32Array(errorsBuffer.getMappedRange())).set(mesh.errors);
    (new Float32Array(boundsBuffer.getMappedRange())).set(mesh.bounds);
    meshletsBuffer.unmap();
    meshletVerticesBuffer.unmap();
    meshletTrianglesBuffer.unmap();
    errorsBuffer.unmap();
    boundsBuffer.unmap();
    return {
        meshletsBuffer,
        meshletVerticesBuffer,
        meshletTrianglesBuffer,
        errorsBuffer,
        boundsBuffer,
    };
}

function makeCullClustersPipeline(device, {perFrameUniformsLayout, meshPoolLayout, selectedClustersLayout, visibleClustersLayout}) {
    return device.createComputePipeline({
        label: 'cull clusters',
        layout: device.createPipelineLayout({
            label: 'cull clusters',
            bindGroupLayouts: [
                perFrameUniformsLayout,
                meshPoolLayout,
                selectedClustersLayout,
                visibleClustersLayout,
            ],
        }),
        compute: {
            module: device.createShaderModule({
                label: 'cull clusters',
                code: cullClustersShader,
                constants: {
                    WORKGROUP_SIZE: 256,
                    MAX_TRIANGLES_PER_CLUSTER: 128,
                },
            }),
        },
    });
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
                VERTEX_STRIDE_FLOATS: 3,
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

export function makeClusterRenderer(device, colorFormat = 'rgba16float', depthFormat = 'depth24plus', initialMesh = null, reverseZ = true) {
    const bindGroupLayouts = {
        perFrameUniformsLayout: device.createBindGroupLayout({
            label: 'per frame uniforms',
            entries: [
                {
                    binding: 0,
                    visibility: GPUShaderStage.COMPUTE | GPUShaderStage.VERTEX,
                    buffer: {
                        type: 'uniform',
                        minBindingSize: Float32Array.BYTES_PER_ELEMENT * ((16 * 2) + (4 * 6)),
                    }
                },
                {
                    binding: 1,
                    visibility: GPUShaderStage.COMPUTE,
                    buffer: {
                        type: 'uniform',
                        minBindingSize: Float32Array.BYTES_PER_ELEMENT * 4,
                    }
                },
            ],
        }),
        meshPoolLayout: device.createBindGroupLayout({
            label: 'mesh & instance pool',
            entries: [
                {
                    binding: 0,
                    visibility: GPUShaderStage.COMPUTE,
                    buffer: {
                        type: 'read-only-storage',
                        minBindingSize: Float32Array.BYTES_PER_ELEMENT * 16 * numInstances,
                    },
                },
                {
                    binding: 1,
                    visibility: GPUShaderStage.COMPUTE,
                    buffer: {
                        type: 'read-only-storage',
                        minBindingSize: Float32Array.BYTES_PER_ELEMENT * 10,
                    },
                },
                {
                    binding: 2,
                    visibility: GPUShaderStage.COMPUTE,
                    buffer: {
                        type: 'read-only-storage',
                        minBindingSize: Float32Array.BYTES_PER_ELEMENT * 4,
                    },
                },
            ],
        }),
        selectedClustersLayout: device.createBindGroupLayout({
            label: 'selected clusters',
            entries: [
                {
                    binding: 0,
                    visibility: GPUShaderStage.COMPUTE,
                    buffer: {
                        type: 'read-only-storage',
                        minBindingSize: Uint32Array.BYTES_PER_ELEMENT * 2 * numInstances,
                    },
                },
                {
                    binding: 1,
                    visibility: GPUShaderStage.COMPUTE,
                    buffer: {
                        type: 'read-only-storage',
                        minBindingSize: Uint32Array.BYTES_PER_ELEMENT,
                    },
                },
            ],
        }),
        visibleClustersLayout: device.createBindGroupLayout({
            label: 'selected clusters',
            entries: [
                {
                    binding: 0,
                    visibility: GPUShaderStage.COMPUTE,
                    buffer: {
                        type: 'storage',
                        minBindingSize: Uint32Array.BYTES_PER_ELEMENT * 2 * numInstances,
                    },
                },
                {
                    binding: 1,
                    visibility: GPUShaderStage.COMPUTE,
                    buffer: {
                        type: 'storage',
                        minBindingSize: Uint32Array.BYTES_PER_ELEMENT * 4,
                    },
                },
            ],
        }),
    };

    const cullClustersPipeline = makeCullClustersPipeline(device, bindGroupLayouts);
    const renderClustersPipeline = makeRenderClusterPipeline(device, colorFormat, depthFormat, reverseZ);

    const cameraBuffer = device.createBuffer({
        label: 'camera',
        size: Float32Array.BYTES_PER_ELEMENT * 16 * 2,
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });
    const renderSettingsBuffer = device.createBuffer({
        lable: 'render settings',
        size: Uint32Array.BYTES_PER_ELEMENT,
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });
    const uniformsBindGroup = device.createBindGroup({
        label: 'uniforms',
        layout: renderClustersPipeline.getBindGroupLayout(0),
        entries: [
            {binding: 0, resource: {buffer: cameraBuffer}},
            {binding: 1, resource: {buffer: renderSettingsBuffer}},
        ],
    });

    const cullingCameraBuffer = device.createBuffer({
        label: 'culling camera',
        size: Float32Array.BYTES_PER_ELEMENT * ((16 * 2) + 4 + (4 * 5)),
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });
    const cullingConfigBuffer = device.createBuffer({
        label: 'culling config',
        size: Float32Array.BYTES_PER_ELEMENT * 4,
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });
    const cullingUniformsBindGroup = device.createBindGroup({
        label: 'culling uniforms',
        layout: bindGroupLayouts.perFrameUniformsLayout,
        entries: [
            {binding: 0, resource: {buffer: cullingCameraBuffer}},
            {binding: 1, resource: {buffer: cullingConfigBuffer}},
        ],
    });

    const instancesBuffer = device.createBuffer({
        label: 'instances',
        size: Float32Array.BYTES_PER_ELEMENT * 16 * numInstances,
        usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
    });
    const indirectDrawArgsBuffer = device.createBuffer({
        label: `indirect draw args`,
        size: Uint32Array.BYTES_PER_ELEMENT * 4,
        usage: GPUBufferUsage.STORAGE | GPUBufferUsage.INDIRECT | GPUBufferUsage.COPY_DST,
    });

    function makeMeshBindgroups(mesh) {
        const bindGroups = {};

        const meshletBuffers = makeMeshletBuffers(device, mesh);
        const vertexBuffer = makeVertexBuffer(device, mesh);

        bindGroups.meshletBindGroup = device.createBindGroup({
            label: 'mesh & instance data',
            layout: renderClustersPipeline.getBindGroupLayout(1),
            entries: [
                {binding: 0, resource: {buffer: instancesBuffer}},
                {binding: 1, resource: {buffer: meshletBuffers.meshletsBuffer}},
                {binding: 2, resource: {buffer: meshletBuffers.meshletVerticesBuffer}},
                {binding: 3, resource: {buffer: meshletBuffers.meshletTrianglesBuffer}},
                {binding: 4, resource: {buffer: vertexBuffer}},
            ],
        });

        bindGroups.cullClustersMeshletBindGroup = device.createBindGroup({
            label: 'cull clusters: mesh & instance data',
            layout: bindGroupLayouts.meshPoolLayout,
            entries: [
                {binding: 0, resource: {buffer: instancesBuffer}},
                {binding: 1, resource: {buffer: meshletBuffers.errorsBuffer}},
                {binding: 2, resource: {buffer: meshletBuffers.boundsBuffer}},
            ],
        });

        const selectedInstancesBuffer = device.createBuffer({
            label: 'selected instances',
            size: Uint32Array.BYTES_PER_ELEMENT * 2 * mesh.numMeshlets * numInstances,
            usage: GPUBufferUsage.STORAGE,
            mappedAtCreation: true,
        });
        const selectedInstancesCountBuffer = device.createBuffer({
            label: 'instance count',
            size: Uint32Array.BYTES_PER_ELEMENT,
            usage: GPUBufferUsage.STORAGE,
            mappedAtCreation: true,
        });
        (new Uint32Array(selectedInstancesBuffer.getMappedRange())).set(new Uint32Array(Array(mesh.numMeshlets * numInstances).fill(0).map((_, c) => {
            return [
                Math.floor(c / mesh.numMeshlets),
                c,
            ];
        }).flat()));
        (new Uint32Array(selectedInstancesCountBuffer.getMappedRange())).set(new Uint32Array([mesh.numMeshlets  * numInstances]));
        selectedInstancesBuffer.unmap();
        selectedInstancesCountBuffer.unmap();

        bindGroups.cullClustersSelectedInstancesBindGroups = device.createBindGroup({
            label: 'selected instances',
            layout: bindGroupLayouts.selectedClustersLayout,
            entries: [
                {binding: 0, resource: {buffer: selectedInstancesBuffer}},
                {binding: 1, resource: {buffer: selectedInstancesCountBuffer}},
            ],
        });

        const visibleClustersBuffer = device.createBuffer({
            label: `visible instances`,
            size: Uint32Array.BYTES_PER_ELEMENT * 2 * mesh.numMeshlets * numInstances,
            usage: GPUBufferUsage.STORAGE,
        });
        bindGroups.cullClustersResultBindGroup = device.createBindGroup({
            label: 'cull clusters result',
            layout: bindGroupLayouts.visibleClustersLayout,
            entries: [
                {binding: 0, resource: {buffer: visibleClustersBuffer}},
                {binding: 1, resource: {buffer: indirectDrawArgsBuffer}},
            ],
        });

        bindGroups.clusterInstancesBindGroup = device.createBindGroup({
            label: 'cluster instances bind group',
            layout: renderClustersPipeline.getBindGroupLayout(2),
            entries: [
                {binding: 0, resource: {buffer: visibleClustersBuffer}},
            ],
        });
        bindGroups.numMeshlets = mesh.numMeshlets;

        return bindGroups;
    }

    let currentMesh = initialMesh ? makeMeshBindgroups(initialMesh) : null;

    return {
        update({view, projection, position}, {instances}, {resolution, zNear, threshold, radiusScale}, {renderMode}, updateCullingCamera = true) {
            device.queue.writeBuffer(indirectDrawArgsBuffer, 0, new Uint32Array([0, 0, 0, 0]));
            device.queue.writeBuffer(cameraBuffer, 0, new Float32Array([...view, ...projection]));
            device.queue.writeBuffer(renderSettingsBuffer, 0, new Uint32Array([renderMode]));
            device.queue.writeBuffer(instancesBuffer, 0, new Float32Array([...instances.flat()]));
            device.queue.writeBuffer(cullingConfigBuffer, 0, new Float32Array([resolution, zNear, threshold, radiusScale]));
            if (updateCullingCamera) {
                const viewProjectionTranspose = mat4n.transpose(mat4n.mul(projection, view));
                const x = vec4n.create(...mat4n.getAxis(viewProjectionTranspose, 0), viewProjectionTranspose[3]);
                const y = vec4n.create(...mat4n.getAxis(viewProjectionTranspose, 1), viewProjectionTranspose[7]);
                const z = vec4n.create(...mat4n.getAxis(viewProjectionTranspose, 2), viewProjectionTranspose[11]);
                const w = vec4n.create(viewProjectionTranspose[12], viewProjectionTranspose[13], viewProjectionTranspose[14], viewProjectionTranspose[15]);
                const frustumPlanes = [
                    vec4n.add(w, x),
                    vec4n.subtract(w, x),
                    vec4n.add(w, y),
                    vec4n.subtract(w, y),
                    vec4n.subtract(w, z),
                ];
                for (let i = 0; i < frustumPlanes.length; ++i) {
                    vec4n.divScalar(frustumPlanes[i], vec3n.length(vec3n.create(frustumPlanes[i][0], frustumPlanes[i][1], frustumPlanes[i][2])), frustumPlanes[i]);
                }
                device.queue.writeBuffer(cullingCameraBuffer, 0, new Float32Array([...view, ...projection, ...position, 0, ...frustumPlanes.flat()]));
            }
        },
        encodeIndirect(commandEncoder, backBufferView, depthBufferView) {
            const cullingPass = commandEncoder.beginComputePass({
                label: 'cull clusters',
            });
            cullingPass.setPipeline(cullClustersPipeline);
            cullingPass.setBindGroup(0, cullingUniformsBindGroup);
            cullingPass.setBindGroup(1, currentMesh.cullClustersMeshletBindGroup);
            cullingPass.setBindGroup(2, currentMesh.cullClustersSelectedInstancesBindGroups);
            cullingPass.setBindGroup(3, currentMesh.cullClustersResultBindGroup);
            cullingPass.dispatchWorkgroups(Math.ceil((currentMesh.numMeshlets * numInstances) / 256));
            cullingPass.end();

            const geometryPass = commandEncoder.beginRenderPass({
                colorAttachments: [{
                    view: backBufferView,
                    clearValue: [0, 0, 0, 1],
                    loadOp: 'clear',
                    storeOp: 'store',
                }],
                depthStencilAttachment: {
                    view: depthBufferView,
                    depthClearValue: 0.0, // reverse z
                    depthLoadOp: 'clear',
                    depthStoreOp: 'store',
                },
            });
            geometryPass.setPipeline(renderClustersPipeline);
            geometryPass.setBindGroup(0, uniformsBindGroup);
            geometryPass.setBindGroup(1, currentMesh.meshletBindGroup);
            geometryPass.setBindGroup(2, currentMesh.clusterInstancesBindGroup);
            geometryPass.drawIndirect(indirectDrawArgsBuffer, 0);
            geometryPass.end();
        },
        newMesh(mesh) {
            currentMesh = makeMeshBindgroups(mesh);
        },
        numInstances,
    };
}

