struct VertexIn {
    @location(0) position: vec2f,
    @location(1) color: vec4f,
    @location(2) distance: f32,
}

struct VertexOut {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
    @location(1) distance: f32,
}

struct Immediates {
    view: mat4x4f,
    time: f32,
    dash: f32,
}

var<immediate> immediates: Immediates;

@vertex
fn vertexMain(in: VertexIn) -> VertexOut {
    return VertexOut(
        immediates.view * vec4f(in.position, 0.0, 1.0),
        in.color,
        in.distance,
    );
}

@fragment
fn fragmentMain(in: VertexOut) -> @location(0) vec4f {
    if (immediates.dash > 0.5) {
        // Crawling dash: draw [0,20], skip [20,40], ...
        let d = in.distance + immediates.time * 60.0;
        let cell = modf(d / 20.0);
        if ((i32(cell.whole) % 2) != 0) {
            discard;
        }
    }
    return in.color;
}
