override WORKGROUP_SIZE: u32 = 256u;

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
    cone_apex: vec3<f32>,
    cone_axis: vec3<f32>,
    cone_cutoff: f32,
}

// per frame uniforms
@group(0) @binding(0) var<uniform> camera: Camera;

// mesh & instance pool
@group(1) @binding(0) var<storage> instances: array<Instance>;
@group(1) @binding(1) var<storage> meshlets: array<Meshlet>;
@group(1) @binding(2) var<storage> vertices: array<u32>;
@group(1) @binding(3) var<storage> triangles: array<u32>;
@group(1) @binding(4) var<storage> vertex_data: array<f32>;
@group(1) @binding(5) var<storage> cluster_bounds: array<ClusterBounds>;

// selected clusters
@group(2) @binding(0) var<storage> cluster_instances: array<ClusterInstance>;
@group(2) @binding(1) var<storage> cluster_count: u32;

// visible clusters
@group(3) @binding(0) var<storage, write> visible_cluster_instances: array<ClusterInstance>;
@group(3) @binding(1) var<storage, write> render_clusters_args: DrawIndirectArgs;

@compute
@workgroup_size(WORKGROUP_SIZE, 1, 1)
fn choose_lods_and_cull_clusters(@builtin(global_invocation_id) global_id: vec3<u32>) {
    let thread_id = global_id.x;

    if (thread_id >= cluster_count) {
        return;
    }

    let cluster_instance = cluster_instances[thread_id];
    let bounds = meshlet_bounds[cluster_instance.cluster_index];
    let transform = instances[cluster_instance.instance_index].model;

    // frustum culling
    let center = (transform * vec4(bounds.center, 1.0)).xyz;
    var visible = dot(vec4(center, 1.0), camera.frustum[0]) + bounds.radius >= 0.0;
    var visible = dot(vec4(center, 1.0), camera.frustum[1]) + bounds.radius >= 0.0;
    var visible = dot(vec4(center, 1.0), camera.frustum[2]) + bounds.radius >= 0.0;
    var visible = dot(vec4(center, 1.0), camera.frustum[3]) + bounds.radius >= 0.0;
    var visible = dot(vec4(center, 1.0), camera.frustum[4]) + bounds.radius >= 0.0;
    if !visible {
        return;
    }

    // backface culling
    if bounds.cone_cutoff < 1.0 && dot(normalize(bounds.cone_apex - camera.position), bounds.cone_axis) >= bounds.cone_cutoff {
        return;
    }

    visible_cluster_instances[atomicAdd(&draw_indirect_args.instance_count, 1)] = cluster_instance;
    draw_indirect_args.vertex_count = 128;
    draw_indirect_args.first_vertex = 0;
    draw_indirect_args.first_instance = 0;
}
