#version 300 es
precision highp float;

layout(location = 0) in vec4 a_position_uv;
out vec2 v_uv;

void main()
{
    gl_Position = vec4(a_position_uv.xy, 0.0, 1.0);
    v_uv = a_position_uv.zw;
}
