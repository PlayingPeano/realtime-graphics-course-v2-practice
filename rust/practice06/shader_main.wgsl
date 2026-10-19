struct VertexIn {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
}

struct VertexOut {
    @builtin(position) position: vec4f,
    @location(0) normal: vec3f,
}

struct Immediates {
    model: mat4x4f,
    viewProjection: mat4x4f,
}

var<immediate> immediates: Immediates;

@vertex
fn vertexMain(in: VertexIn) -> VertexOut {
    return VertexOut(
        immediates.viewProjection * immediates.model * vec4f(in.position, 1.0),
        normalize((immediates.model * vec4f(in.normal, 0.0)).xyz)
    );
}

@fragment
fn fragmentMain(in: VertexOut) -> @location(0) vec4f {
    let normal = normalize(in.normal);

    let albedo = vec3f(0.81, 0.54, 0.27);
    let specular_albedo = vec3f(0.81, 0.61, 0.40);
    let ambient = vec3f(0.0, 0.02, 0.1);

    let light_dir = normalize(vec3f(3.0, 2.0, 1.0));
    let color = pow(0.5 + 0.5 * dot(normal, light_dir), 2.0) * albedo
        + pow(max(0.0, dot(normal, vec3f(0.0, 0.0, 1.0))), 64.0) * specular_albedo;

    return vec4f(color, 1.0);
}
