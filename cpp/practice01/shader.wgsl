@vertex
fn vertexMain(@builtin(vertex_index) vertexIndex: u32)
    -> @builtin(position) vec4f
{
    var positions = array<vec2f, 3>(
        vec2f( 0.0,  0.5),
        vec2f(-0.5, -0.5),
        vec2f( 0.5, -0.5),
    );
    return vec4f(positions[vertexIndex], 0.0, 1.0);
}

@fragment
fn fragmentMain(@builtin(position) pos: vec4f) -> @location(0) vec4f {
    let cellSize = 32.0;
    let p = pos.xy / cellSize;
    let mx = modf(p.x);
    let my = modf(p.y);
    let checker = (i32(mx.whole) + i32(my.whole)) % 2;
    if (checker == 0) {
        return vec4f(0.95, 0.35, 0.15, 1.0);
    } else {
        return vec4f(0.1, 0.1, 0.1, 1.0);
    }
}