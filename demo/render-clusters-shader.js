export const renderClusterWgsl = `
override VERTEX_STRIDE_FLOATS: u32 = 3;

struct Camera {
    view: mat4x4<f32>,
    projection: mat4x4<f32>,
}

struct Settings {
    render_mode: u32,
}

struct Instance {
    model: mat4x4<f32>,
}

struct ClusterInstance {
    instance_index: u32,
    cluster_index: u32,
}

struct Meshlet {
    vertex_offset: u32,
    triangle_offset: u32,
    vertex_count: u32,
    triangle_count: u32,
}

struct Vertex {
    position: vec3<f32>,
}

// per frame uniforms
@group(0) @binding(0) var<uniform> camera: Camera;
@group(0) @binding(1) var<uniform> settings: Settings;

// mesh & instance pool
@group(1) @binding(0) var<storage> instances: array<Instance>;
@group(1) @binding(1) var<storage> meshlets: array<Meshlet>;
@group(1) @binding(2) var<storage> vertices: array<u32>;
@group(1) @binding(3) var<storage> triangles: array<u32>;
@group(1) @binding(4) var<storage> vertex_data: array<f32>;

// visible clusters
@group(2) @binding(0) var<storage> cluster_instances: array<ClusterInstance>;

fn get_vertex(meshlet: Meshlet, vertex_index: u32) -> Vertex {
    let index = vertices[meshlet.vertex_offset + triangles[meshlet.triangle_offset + vertex_index]] * VERTEX_STRIDE_FLOATS;
    return Vertex(
        vec3(
            vertex_data[index],
            vertex_data[index + 1],
            vertex_data[index + 2],
        ),
    );
}

fn get_meshlet_color(index: u32) -> vec3<f32> {
    return unpack4x8unorm(((index % 127) + 1) * 123456789).rgb;
}

struct VertexOut {
    @builtin(position) position: vec4<f32>,
    @location(0) @interpolate(flat) id: u32,
}

fn get_primitive_id(cluster_index: u32, vertex_index: u32) -> u32 {
    if settings.render_mode == 0 {
        return cluster_index;
    } else {
        let triangle_index = vertex_index / 3;
        return (cluster_index << 7) | triangle_index;
    }
}

@vertex
fn vertex(@builtin(instance_index) cluster_instance_index: u32, @builtin(vertex_index) vertex_index: u32) -> VertexOut {
    let cluster_instance = cluster_instances[cluster_instance_index];

    let meshlet = meshlets[cluster_instance.cluster_index];
    if vertex_index >= (meshlet.triangle_count * 3) {
        return VertexOut(
            vec4<f32>(1.0, 1.0, 1.0, 0.0), // position will be clipped
            0,
        );
    }
    
    let model = instances[cluster_instance.instance_index].model;

    let vertex = get_vertex(meshlet, vertex_index);
    return VertexOut(
        camera.projection * camera.view * model * vec4(vertex.position, 1.0),        
        get_primitive_id(cluster_instance.cluster_index, vertex_index),
    );
}

@fragment
fn fragment(frag_in: VertexOut) -> @location(0) vec4<f32> {
    let albedo = get_meshlet_color(frag_in.id);
    return vec4(saturate(albedo), 1.0);
}
`;