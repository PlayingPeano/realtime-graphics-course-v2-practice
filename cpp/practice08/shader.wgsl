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
    lightDirection: vec3f,
    lightIntensity: vec3f,
    shadowProjection: mat4x4f,
}

struct Immediates {
    model: mat4x4f,
    albedo: vec3f,
    roughness: f32,
    specularFactor: f32,
}

@group(0) @binding(0) var<uniform> camera: CameraUniforms;
@group(1) @binding(0) var<uniform> lights: LightsUniforms;
@group(1) @binding(1) var shadowMap: texture_depth_2d;
@group(1) @binding(3) var shadowCompareSampler: sampler_comparison;

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
const SHADOW_BIAS = 0.005;
const PCF_WEIGHTS = array<f32, 7>(0.006, 0.061, 0.242, 0.383, 0.242, 0.061, 0.006);

fn pcfVisibility(uv: vec2f, depth: f32) -> f32 {
    let texel = 1.0 / vec2f(textureDimensions(shadowMap));
    var sum = 0.0;
    for (var j = 0; j < 7; j++) {
        for (var i = 0; i < 7; i++) {
            let offset = vec2f(f32(i) - 3.0, f32(j) - 3.0) * texel;
            sum += PCF_WEIGHTS[i] * PCF_WEIGHTS[j]
                * textureSampleCompareLevel(shadowMap, shadowCompareSampler, uv + offset, depth);
        }
    }
    return sum;
}

fn computeLight(normal: vec3f, lightDirection: vec3f, lightIntensity: vec3f, viewDirection: vec3f) -> vec3f {
    let halfway = normalize(lightDirection + viewDirection);
    let power = 1.0 / max(0.0001, immediates.roughness * immediates.roughness) - 1.0;
    return max(0.0, dot(normal, lightDirection)) * (1.0 + immediates.specularFactor * (power + 2.0) / (2.0 * PI) * pow(max(0.0, dot(halfway, normal)), power)) * lightIntensity;
}

@fragment
fn fragmentMain(in: VertexOut) -> @location(0) vec4f {
    let normal = normalize(in.normal);

    let viewDirection = normalize(camera.position - in.world_position);
    let halfway = normalize(lights.lightDirection + viewDirection);
    let specularPower = 1.0 / max(0.0001, immediates.roughness * immediates.roughness) - 1.0;
    let cosine = max(0.0, dot(normal, lights.lightDirection));

    var shadowClip = lights.shadowProjection * vec4f(in.world_position, 1.0);
    let shadowNdc = shadowClip.xyz / shadowClip.w;
    let shadowUv = vec2f(0.5) + shadowNdc.xy * vec2f(0.5, -0.5);
    let compared = pcfVisibility(shadowUv, shadowNdc.z - SHADOW_BIAS);
    let inside = all(abs(shadowNdc.xy) < vec2f(1.0)) && shadowNdc.z >= 0.0 && shadowNdc.z <= 1.0;
    let visibility = select(1.0, compared, inside);

    let direct = immediates.albedo * lights.lightIntensity * cosine
        + lights.lightIntensity * immediates.specularFactor * (specularPower + 2.0) / (2.0 * PI) * pow(max(0.0, dot(halfway, normal)), specularPower);
    let color = immediates.albedo * lights.ambientIntensity + visibility * direct;

    return vec4f(color, 1.0);
}
