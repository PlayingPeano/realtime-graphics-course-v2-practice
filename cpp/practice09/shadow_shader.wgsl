struct VertexIn {
    @location(0) position: vec3f,
}

struct VertexOut {
    @builtin(position) clip_position: vec4f,
}

struct Immediates {
	shadowProjection: mat4x4f,
}

var<immediate> immediates: Immediates;

@vertex
fn vertexMain(in: VertexIn) -> VertexOut {
	return VertexOut(immediates.shadowProjection * vec4f(in.position, 1.0));
}

@fragment
fn fragmentMain(in: VertexOut) -> @location(0) vec4f {
    let z = in.clip_position.z;
    let dzdx = dpdx(z);
    let dzdy = dpdy(z);
    let z2 = z * z + 0.25 * (dzdx * dzdx + dzdy * dzdy);
    return vec4f(z, z2, 0.0, 0.0);
}
