export const cullClustersShader = `
override WORKGROUP_SIZE: u32 = 256u;
override MAX_TRIANGLES_PER_CLUSTER: u32 = 128;

struct Camera {
    view: mat4x4<f32>,
    projection: mat4x4<f32>,
    position: vec3<f32>,
    pad: f32,
    frustum: array<vec4<f32>, 5>,
}

struct Instance {
    model: mat4x4<f32>,
}

struct ClusterInstance {
    instance_index: u32,
    cluster_index: u32,
}

struct DrawIndirectArgs {
    vertex_count: u32,
    instance_count: atomic<u32>,
    first_vertex: u32,
    first_instance: u32,
}

struct ClusterBounds {
    center: vec3<f32>,
    radius: f32,
    cone_axis: vec3<f32>,
    error: f32,
    cone_apex: vec3<f32>,
    cone_cutoff: f32,
}

// per frame uniforms
@group(0) @binding(0) var<uniform> camera: Camera;

// mesh & instance pool
@group(1) @binding(0) var<storage> instances: array<Instance>;
@group(1) @binding(1) var<storage> cluster_bounds: array<f32>;

// selected clusters
@group(2) @binding(0) var<storage> cluster_instances: array<ClusterInstance>;
@group(2) @binding(1) var<storage> cluster_count: u32;

// visible clusters
@group(3) @binding(0) var<storage, read_write> visible_cluster_instances: array<ClusterInstance>;
@group(3) @binding(1) var<storage, read_write> render_clusters_args: DrawIndirectArgs;

fn get_bounds(index: u32) -> ClusterBounds {
    let bounds_index = index * 48;
    return ClusterBounds(
        vec3<f32>(cluster_bounds[index + 0], cluster_bounds[index + 1], cluster_bounds[index + 2]),
        cluster_bounds[index + 3],
        vec3<f32>(cluster_bounds[index + 4], cluster_bounds[index + 5], cluster_bounds[index + 6]),
        cluster_bounds[index + 7],
        vec3<f32>(cluster_bounds[index + 8], cluster_bounds[index + 9], cluster_bounds[index + 10]),
        cluster_bounds[index + 11],
    );
}

@compute
@workgroup_size(WORKGROUP_SIZE, 1, 1)
fn choose_lods_and_cull_clusters(@builtin(global_invocation_id) global_id: vec3<u32>) {
    let thread_id = global_id.x;

    if (thread_id >= cluster_count) {
        return;
    }

    let cluster_instance = cluster_instances[thread_id];
    let bounds = get_bounds(cluster_instance.cluster_index);
    let transform = instances[cluster_instance.instance_index].model;

    // frustum culling
    let center = (transform * vec4(bounds.center, 1.0)).xyz;
    var visible =
        (dot(vec4(center, 1.0), camera.frustum[0]) + bounds.radius >= 0.0) &&
        (dot(vec4(center, 1.0), camera.frustum[1]) + bounds.radius >= 0.0) &&
        (dot(vec4(center, 1.0), camera.frustum[2]) + bounds.radius >= 0.0) &&
        (dot(vec4(center, 1.0), camera.frustum[3]) + bounds.radius >= 0.0) &&
        (dot(vec4(center, 1.0), camera.frustum[4]) + bounds.radius >= 0.0);
    if !visible {
        return;
    }

    /*
    // backface culling - (if cutoff is >= 1 then the normal cone is too wide for backface culling)
    let cone_apex = bounds.cone_apex;// (transform * vec4(bounds.cone_apex, 1.0)).xyz;
    let cone_axis = normalize(bounds.cone_axis);//normalize((transform * vec4(bounds.cone_axis, 0.0)).xyz);
    if bounds.cone_cutoff < 1.0 && dot(normalize(cone_apex - camera.position), cone_axis) >= bounds.cone_cutoff {
        return;
    }
    */

    visible_cluster_instances[atomicAdd(&render_clusters_args.instance_count, 1)] = cluster_instance;
    render_clusters_args.vertex_count = MAX_TRIANGLES_PER_CLUSTER * 3;
    render_clusters_args.first_vertex = 0;
    render_clusters_args.first_instance = 0;
}

`;