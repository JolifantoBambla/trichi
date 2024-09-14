export const cullClustersShader = `
const f32_max: f32 = 3.40282e+38;

override WORKGROUP_SIZE: u32 = 256u;
override MAX_TRIANGLES_PER_CLUSTER: u32 = 128;

struct Camera {
    view: mat4x4<f32>,
    projection: mat4x4<f32>,
    position: vec3<f32>,
    pad: f32,
    frustum: array<vec4<f32>, 5>,
}

struct ErrorProjectionParams {
    resolution: f32,
    z_near: f32,
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
    radius: f32,
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
@group(0) @binding(1) var<uniform> error_projection_params: ErrorProjectionParams;

// mesh & instance pool
@group(1) @binding(0) var<storage> instances: array<Instance>;
@group(1) @binding(1) var<storage> error_bounds: array<f32>;
@group(1) @binding(2) var<storage> cluster_bounds: array<ClusterBounds>;

// selected clusters
@group(2) @binding(0) var<storage> cluster_instances: array<ClusterInstance>;
@group(2) @binding(1) var<storage> cluster_count: u32;

// visible clusters
@group(3) @binding(0) var<storage, read_write> visible_cluster_instances: array<ClusterInstance>;
@group(3) @binding(1) var<storage, read_write> render_clusters_args: DrawIndirectArgs;

// from Nexus
// https://github.com/cnr-isti-vclab/nexus/blob/ae6bf8601303884250d3c73b9e1d4cbe179f9b92/src/common/metric.h#L53
fn project_error_bounds(transform: mat4x4<f32>, bounds: GroupError) -> f32 {
    if bounds.error == 0.0 || bounds.error == f32_max {
        return bounds.error;
    }
    var dist = distance((transform * vec4<f32>(bounds.center, 1.0)).xyz, camera.position) - bounds.radius;
    if dist < error_projection_params.z_near {
        dist = error_projection_params.z_near;
    }
    let size = dist * error_projection_params.resolution;
    if size <= 0.00001 {
        return f32_max;
    } else {
        return bounds.error / size;
    }
}

fn is_selected_lod(transform: mat4x4<f32>, bounds: ErrorBounds) -> bool {
    let cluster_error = project_error_bounds(transform, bounds.cluster_error);
    let parent_error = project_error_bounds(transform, bounds.parent_error);
    return parent_error > error_projection_params.threshold && cluster_error <= error_projection_params.threshold;
}

fn get_error_bounds(cluster_index: u32) -> ErrorBounds {
    let index = cluster_index * 5 * 2;
    return ErrorBounds(
        GroupError(
            vec3<f32>(error_bounds[index + 0], error_bounds[index + 1], error_bounds[index + 2]),   // center
            error_bounds[index + 3],                                                                // radius
            error_bounds[index + 4],                                                                // error
        ),
        GroupError(
            vec3<f32>(error_bounds[index + 5], error_bounds[index + 6], error_bounds[index + 7]),   // center
            error_bounds[index + 8],                                                                // radius
            error_bounds[index + 9],                                                                // error
        ),
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
    let error = get_error_bounds(cluster_instance.cluster_index);
    let bounds = cluster_bounds[cluster_instance.cluster_index];
    let transform = instances[cluster_instance.instance_index].model;

    if !is_selected_lod(transform, error) {
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

`;