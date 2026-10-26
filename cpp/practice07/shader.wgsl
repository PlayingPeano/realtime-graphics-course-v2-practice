struct VertexIn {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) texcoord: vec2f,
}

struct VertexOut {
    @builtin(position) clip_position: vec4f,
    @location(0) world_position: vec3f,
    @location(1) normal: vec3f,
}

struct CameraUniforms {
    viewProjection: mat4x4f,
    position: vec3f,
}

struct LightsUniforms {
    ambientIntensity: vec3f,
    directionalDirection: vec3f,
    directionalIntensity: vec3f,
    pointPosition: vec3f,
    pointIntensity: vec3f,
    pointAttenuation: vec3f,
}

struct Immediates {
    model: mat4x4f,
    albedo: vec3f,
    roughness: f32,
    specularFactor: f32,
}

@group(0) @binding(0) var<uniform> camera: CameraUniforms;
@group(1) @binding(0) var<uniform> lights: LightsUniforms;

var<immediate> immediates: Immediates;

const PI = 3.14159265;

fn computeLighting(normal: vec3f, viewDirection: vec3f, lightDirection: vec3f, lightColor: vec3f) -> vec3f {
    let nDotL = max(0.0, dot(normal, lightDirection));
    let diffuse = lightColor * nDotL;

    let roughness = max(immediates.roughness, 0.045);
    let power = 1.0 / (roughness * roughness) - 1.0;
    let halfway = normalize(lightDirection + viewDirection);
    let specular = lightColor * immediates.specularFactor
        * (power + 2.0) / (2.0 * PI)
        * pow(max(0.0, dot(normal, halfway)), power)
        * nDotL;

    return diffuse + specular;
}

@vertex
fn vertexMain(in: VertexIn) -> VertexOut {
    let world_position = immediates.model * vec4f(in.position, 1.0);
    return VertexOut(
        camera.viewProjection * world_position,
        world_position.xyz,
        normalize((immediates.model * vec4f(in.normal, 0.0)).xyz)
    );
}

@fragment
fn fragmentMain(in: VertexOut) -> @location(0) vec4f {
    let normal = normalize(in.normal);
    let viewDirection = normalize(camera.position - in.world_position);

    let directional = computeLighting(
        normal,
        viewDirection,
        normalize(lights.directionalDirection),
        lights.directionalIntensity,
    );

    let toPoint = lights.pointPosition - in.world_position;
    let distance = length(toPoint);
    let pointDirection = toPoint / max(distance, 1e-4);
    let attenuation = 1.0 / dot(lights.pointAttenuation, vec3f(1.0, distance, distance * distance));
    let point = computeLighting(
        normal,
        viewDirection,
        pointDirection,
        lights.pointIntensity * attenuation,
    );

    let color = immediates.albedo * (lights.ambientIntensity + directional + point);
    return vec4f(color, 1.0);
}
