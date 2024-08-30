override WORKGROUP_SIZE: u32 = 256u;

struct Instance {
    transform: mat4x4<f32>,
    mesh_id: u32,
}

struct ClusterInstance {
    instance_id: u32,
    cluster_id: u32,
}

struct Mesh {
    center: vec3<f32>,
    radius: f32,
    num_clusters_total: u32,
}

struct DrawIndirectArgs {
    vertex_count: u32,
    instance_count: atomic<u32>,
    first_vertex: u32,
    first_instance: u32,
}

@group(0) @binding(0) var<storage> scene_instances: array<Instance>;
@group(0) @binding(0) var<storage> meshes: array<Mesh>;

@group(0) @binding(0) var<storage, read_write> visible_instance_counter: atomic<u32>;
@group(0) @binding(0) var<storage, write> choose_lods_indirect_args: vec3<u32>;
@group(0) @binding(0) var<storage, read_write> visible_instances: array<Instance>;

@group(0) @binding(0) var<storage, read_write> visible_clusters_counter: atomic<u32>;
@group(0) @binding(0) var<storage, write> cull_clusters_indirect_args: vec3<u32>;
@group(0) @binding(0) var<storage, write> visible_clusters: array<ClusterInstance>;
@group(0) @binding(0) var<storage, read_write> draw_cluster_args: DrawIndirectArgs;

@compute
@workgroup_size(WORKGROUP_SIZE, 1, 1)
fn cull_instances(@builtin(global_invocation_id) global_id: vec3<u32>) {
    let thread_id = global_id.x;
    if (thread_id >= arrayLength(scene_instances)) {
        return;
    }
    // todo: frustum culling
    visible_instances[atomicAdd(&visible_instance_counter, 1)] = scene_instances[i];

    // todo: last thread needs to write indirect args
    // todo: once there are more instances this will break
    // todo: once there are more meshes this will break
    choose_lods_indirect_args = vec3<u32>(u32(ceil(f32(atomicLoad(&instance_counter) * meshes[scene_instances[i].mesh_id].num_clusters_total) / f32(WORKGROUP_SIZE))), 0u, 0u);
}

// note: this is a very naive brute-force way of doing it. it's ok for this demo but in practice, nodes should be in an acceleration structure, etc. check out deep dive into nanite for more pointers

@compute
@workgroup_size(WORKGROUP_SIZE, 1, 1)
fn choose_lods_and_cull_clusters(@builtin(global_invocation_id) global_id: vec3<u32>) {
    let thread_id = global_id.x;

    let num_visible_instances = atomicLoad(&visible_instance_counter);
    let num_dag_nodes = meshes[0].num_clusters_total;
    let num_clusters_total = num_visible_instance * num_dag_nodes;

    if (thread_id >= num_clusters_total) {
        return;
    }

    // todo
    let target_error = 0.0;

    for (let i = thread_id; i < num_clusters_total; thread_id += WORKGROUP_SIZE) {
        let dag_node_index = i % num_dag_nodes;
        let instance_index = i / num_dag_nodes;
        if instance_index >= num_visible_instances {
            break;
        }

        if node.error < target_error && node.parent_error >= target_error {
            if is_cluster_visible(clusters[cluster_id]) {
                visible_clusters[atomicAdd(&selected_nodes_counter, 1)] = ClusterInstance(instance_id, node.cluster_id);
            }
        }
    }

    draw_indirect_args.vertex_count = 128;
    atomicMax(&draw_indirect_args.instance_count, atomicLoad(&selected_nodes_counter));
    draw_indirect_args.first_vertex = 0;
    draw_indirect_args.first_instance = 0;
}
