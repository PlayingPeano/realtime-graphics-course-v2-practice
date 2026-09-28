struct VertexOut {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
}

struct Immediates {
    transform: mat4x4f,
    view: mat4x4f,
}

var<immediate> immediates: Immediates;

const center = vec2f(0.0, 0.0);
const c0 = vec2f( 0.50,  0.00);
const c1 = vec2f( 0.25,  0.4330127);
const c2 = vec2f(-0.25,  0.4330127);
const c3 = vec2f(-0.50,  0.00);
const c4 = vec2f(-0.25, -0.4330127);
const c5 = vec2f( 0.25, -0.4330127);

const POSITIONS = array<vec2f, 18>(
    center, c0, c1,
    center, c1, c2,
    center, c2, c3,
    center, c3, c4,
    center, c4, c5,
    center, c5, c0,
);

const COLORS = array<vec4f, 18>(
    vec4f(1.00, 0.29, 0.29, 1.0), vec4f(1.00, 0.29, 0.29, 1.0), vec4f(1.00, 0.50, 0.20, 1.0),
    vec4f(1.00, 0.50, 0.20, 1.0), vec4f(1.00, 0.50, 0.20, 1.0), vec4f(1.00, 0.84, 0.40, 1.0),
    vec4f(1.00, 0.84, 0.40, 1.0), vec4f(1.00, 0.84, 0.40, 1.0), vec4f(0.40, 0.90, 0.40, 1.0),
    vec4f(0.40, 0.90, 0.40, 1.0), vec4f(0.40, 0.90, 0.40, 1.0), vec4f(0.16, 0.72, 0.79, 1.0),
    vec4f(0.16, 0.72, 0.79, 1.0), vec4f(0.16, 0.72, 0.79, 1.0), vec4f(0.45, 0.45, 0.95, 1.0),
    vec4f(0.45, 0.45, 0.95, 1.0), vec4f(0.45, 0.45, 0.95, 1.0), vec4f(1.00, 0.29, 0.29, 1.0),
);

@vertex
fn vertexMain(@builtin(vertex_index) vertexIndex: u32) -> VertexOut {
    let p = immediates.view * immediates.transform * vec4f(POSITIONS[vertexIndex], 0.0, 1.0);
    return VertexOut(p, COLORS[vertexIndex]);
}

@fragment
fn fragmentMain(in: VertexOut) -> @location(0) vec4f {
    return in.color;
}
