override WORKGROUP_SIZE: u32 = 256u;
override MAX_TRIANGLES_PER_CLUSTER: u32 = 128;

struct Camera {
    view: mat4x4<f32>,
    projection: mat4x4<f32>,
    position: vec3<f32>,
    pad: f32,
    frustum: array<vec4<f32>, 5>,
}

struct ErrorConfig {
    cot_half_fov: f32,
    view_height: f32,
    threshold: f32,
    pad: f32,
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

struct GroupError {
    center: vec3<f32>,
    error: f32,
}

struct ErrorBounds {
    parent_error: GroupError,
    cluster_error: GroupError,
}

struct ClusterBounds {
    center: vec3<f32>,
    radius: f32,
    // todo: normal cone?
}

// per frame uniforms
@group(0) @binding(0) var<uniform> camera: Camera;
@group(0) @binding(1) var<uniform> error_config: ErrorConfig;

// mesh & instance pool
@group(1) @binding(0) var<storage> instances: array<Instance>;
@group(1) @binding(1) var<storage> error_bounds: array<ErrorBounds>;
@group(1) @binding(2) var<storage> cluster_bounds: array<ClusterBounds>;

// selected clusters
@group(2) @binding(0) var<storage> cluster_instances: array<ClusterInstance>;
@group(2) @binding(1) var<storage> cluster_count: u32;

// visible clusters
@group(3) @binding(0) var<storage, read_write> visible_cluster_instances: array<ClusterInstance>;
@group(3) @binding(1) var<storage, read_write> render_clusters_args: DrawIndirectArgs;

fn project_error_bounds(transform: mat4x4<f32>, bounds: GroupError) -> f32 {
  let center = (transform * vec4<f32>(bounds.center, 1.0f)).xyz;
  let radius = (error_config.cot_half_fov * bounds.error) / sqrt(max(0.0, dot(center, center) - bounds.error * bounds.error));
  return (radius * error_config.view_height) / 2.0;
}

fn is_selected_lod(transform: mat4x4<f32>, bounds: ErrorBounds) -> bool {
  let cluster_error = project_error_bounds(transform, bounds.cluster_error);
  let parent_error = project_error_bounds(transform, bounds.parent_error);
  return parent_error > error_config.threshold && cluster_error <= error_config.threshold;
}

// todo: output new error format
// todo: output new bounds format
// todo: add error_config buffer (with threshold slider in ui)

@compute
@workgroup_size(WORKGROUP_SIZE, 1, 1)
fn choose_lods_and_cull_clusters(@builtin(global_invocation_id) global_id: vec3<u32>) {
    let thread_id = global_id.x;

    if (thread_id >= cluster_count) {
        return;
    }

    let cluster_instance = cluster_instances[thread_id];
    let error = error_bounds[cluster_instance.cluster_index];
    let bounds = cluster_bounds[cluster_instance.cluster_index];
    let transform = instances[cluster_instance.instance_index].model;

    if !is_selected_lod(camera.projection * camera.view * transform, error) {
        return;
    }

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
