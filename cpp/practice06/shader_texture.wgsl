@group(0) @binding(0) var input_texture: texture_2d<f32>;
@group(0) @binding(1) var linear_sampler: sampler;

struct Immediates {
    center: vec2f,
    extent: vec2f,
}

var<immediate> immediates: Immediates;

struct VertexOut {
    @builtin(position) position: vec4f,
    @location(0) texcoord: vec2f,
}

const VERTICES = array<vec2f, 6>(
    vec2f(-1.0, -1.0),
    vec2f( 1.0, -1.0),
    vec2f(-1.0,  1.0),
    vec2f(-1.0,  1.0),
    vec2f( 1.0, -1.0),
    vec2f( 1.0,  1.0),
);

@vertex
fn vertexMain(@builtin(vertex_index) index: u32) -> VertexOut {
    let vertex = VERTICES[index];

    return VertexOut(
        vec4f(immediates.center + immediates.extent * vertex, 0.0, 1.0),
        vec2f(0.5) + vec2f(0.5, -0.5) * vertex
    );
}

@fragment
fn fragmentMain(in: VertexOut) -> @location(0) vec4f {
    return textureSample(input_texture, linear_sampler, in.texcoord);
}
