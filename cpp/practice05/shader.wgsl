struct VertexIn {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) texcoord: vec2f,
}

struct VertexOut {
    @builtin(position) position: vec4f,
    @location(0) normal: vec3f,
    @location(1) texcoord: vec2f,
}

struct Uniforms {
    model: mat4x4f,
    viewProjection: mat4x4f,
    time: f32,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var albedoTexture: texture_2d<f32>;
@group(0) @binding(2) var albedoSampler: sampler;

@vertex
fn vertexMain(in: VertexIn) -> VertexOut {
    return VertexOut(
        uniforms.viewProjection * uniforms.model * vec4f(in.position, 1.0),
        normalize((uniforms.model * vec4f(in.normal, 0.0)).xyz),
        in.texcoord,
    );
}

@fragment
fn fragmentMain(in: VertexOut) -> @location(0) vec4f {
    let normal = normalize(in.normal);
    let uv = in.texcoord + vec2f(uniforms.time * 0.15, uniforms.time * 0.07);
    let albedo = textureSample(albedoTexture, albedoSampler, uv).rgb;

    let light_dir = normalize(vec3f(3.0, 2.0, 1.0));
    let color = (0.5 + 0.5 * dot(normal, light_dir)) * albedo;

    return vec4f(color, 1.0);
}
