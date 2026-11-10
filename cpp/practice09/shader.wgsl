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
    shadowProjection: mat4x4f,
    ambientIntensity: vec3f,
    lightDirection: vec3f,
    lightIntensity: vec3f,
}

struct Immediates {
    model: mat4x4f,
    albedo: vec3f,
    roughness: f32,
    specularFactor: f32,
}

@group(0) @binding(0) var<uniform> camera: CameraUniforms;
@group(1) @binding(0) var<uniform> lights: LightsUniforms;
@group(1) @binding(2) var shadowMapSampler: sampler;
@group(1) @binding(3) var shadowMoments: texture_2d<f32>;

var<immediate> immediates: Immediates;

@vertex
fn vertexMain(in: VertexIn) -> VertexOut {
    let world_position = immediates.model * vec4f(in.position, 1.0);
    return VertexOut(
        camera.viewProjection * world_position,
        world_position.xyz,
        normalize((immediates.model * vec4f(in.normal, 0.0)).xyz)
    );
}

const PI = 3.1415926535;
const LIGHT_BLEED = 0.2;
const GAUSS = array<f32, 7>(0.00598, 0.060626, 0.241843, 0.383103, 0.241843, 0.060626, 0.00598);

fn sampleMoments(uv: vec2f) -> vec2f {
    let texel = 1.0 / vec2f(textureDimensions(shadowMoments));
    var sum = vec2f(0.0);
    for (var j = 0; j < 7; j++) {
        for (var i = 0; i < 7; i++) {
            let offset = vec2f(f32(i) - 3.0, f32(j) - 3.0) * texel;
            sum += GAUSS[i] * GAUSS[j] * textureSample(shadowMoments, shadowMapSampler, uv + offset).xy;
        }
    }
    return sum;
}

fn vsmVisibility(moments: vec2f, depth: f32) -> f32 {
    let mu = moments.x;
    if (depth <= mu) {
        return 1.0;
    }
    let variance = max(moments.y - mu * mu, 1e-5);
    let d = depth - mu;
    let p = variance / (variance + d * d);
    return saturate((p - LIGHT_BLEED) / (1.0 - LIGHT_BLEED));
}

@fragment
fn fragmentMain(in: VertexOut) -> @location(0) vec4f {
    let normal = normalize(in.normal);

    let viewDirection = normalize(camera.position - in.world_position);
    let halfway = normalize(lights.lightDirection + viewDirection);
    let specularPower = 1.0 / max(0.0001, immediates.roughness * immediates.roughness) - 1.0;
    let cosine = max(0.0, dot(normal, lights.lightDirection));

    var shadowCoord = lights.shadowProjection * vec4f(in.world_position, 1.0);
    shadowCoord /= shadowCoord.w;

    let uv = shadowCoord.xy * vec2f(0.5, -0.5) + vec2f(0.5);
    let visibility = vsmVisibility(sampleMoments(uv), shadowCoord.z);
    let inside = all(abs(shadowCoord.xy) < vec2f(1.0));
    let shadowFactor = select(1.0, visibility, inside);

    let color = immediates.albedo * (lights.ambientIntensity + lights.lightIntensity * cosine * shadowFactor)
        + lights.lightIntensity * shadowFactor * immediates.specularFactor * (specularPower + 2.0) / (2.0 * PI) * pow(max(0.0, dot(halfway, normal)), specularPower);

    return vec4f(color, 1.0);
}
