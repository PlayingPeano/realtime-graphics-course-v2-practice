struct VertexOut {
    @builtin(position) clip_position: vec4f,
}

struct CameraUniforms {
    viewProjection: mat4x4f,
    position: vec3f,
}

const VERTICES = array<vec3f, 6>(
    vec3f(-1.0,  0.0,  0.0),
    vec3f( 1.0,  0.0,  0.0),
    vec3f( 0.0, -1.0,  0.0),
    vec3f( 0.0,  1.0,  0.0),
    vec3f( 0.0,  0.0, -1.0),
    vec3f( 0.0,  0.0,  1.0),
);

struct Immediates {
    position: vec3f,
}

@group(0) @binding(0) var<uniform> camera: CameraUniforms;

var<immediate> immediates: Immediates;

const SCALE = 0.1;

@vertex
fn vertexMain(@builtin(vertex_index) vertex_id: u32) -> VertexOut {
    return VertexOut(
        camera.viewProjection * vec4f(immediates.position + SCALE * VERTICES[vertex_id], 1.0)
    );
}

@fragment
fn fragmentMain(in: VertexOut) -> @location(0) vec4f {
    return vec4f(1.0);
}
