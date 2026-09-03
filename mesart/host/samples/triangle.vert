#version 300 es
precision highp float;

void main()
{
    const vec2 positions[3] = vec2[3](
        vec2( 0.0,  0.65),
        vec2(-0.65, -0.65),
        vec2( 0.65, -0.65));
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
