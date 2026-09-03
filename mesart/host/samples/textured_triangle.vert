#version 310 es
precision highp float;

layout(location = 0) out vec2 v_uv;

void main()
{
    const vec2 positions[3] = vec2[3](
        vec2(0.0, 0.65), vec2(-0.65, -0.65), vec2(0.65, -0.65));
    const vec2 uvs[3] = vec2[3](
        vec2(0.5, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0));
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
    v_uv = uvs[gl_VertexID];
}
