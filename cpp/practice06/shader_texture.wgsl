@group(0) @binding(0) var input_texture: texture_2d<f32>;
@group(0) @binding(1) var linear_sampler: sampler;

struct Immediates {
    center: vec2f,
    extent: vec2f,
    mode: i32,
    time: f32,
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
    var uv = in.texcoord;

    if (immediates.mode == 2) {
        uv += vec2f(sin(in.texcoord.y * 50.0 + immediates.time) * 0.01, 0.0);
    }

    if (immediates.mode == 3) {
        let N = 10;
        let sigma = 6.0;
        let tex_size = vec2f(textureDimensions(input_texture));
        var color = vec3f(0.0);
        var weight_sum = 0.0;

        for (var i = -N; i <= N; i++) {
            for (var j = -N; j <= N; j++) {
                let w = exp(-f32(i * i + j * j) / (2.0 * sigma * sigma));
                let sample_uv = uv + vec2f(f32(i), f32(j)) / tex_size;
                color += textureSample(input_texture, linear_sampler, sample_uv).rgb * w;
                weight_sum += w;
            }
        }
        return vec4f(color / weight_sum, 1.0);
    }

    var color = textureSample(input_texture, linear_sampler, uv);

    if (immediates.mode == 1) {
        color = vec4f(floor(color.rgb * 4.0) / 3.0, color.a);
    }

    return color;
}
