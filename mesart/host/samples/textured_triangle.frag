#version 310 es
precision mediump float;

layout(location = 0) in vec2 v_uv;
uniform sampler2D u_texture;
uniform vec4 u_tint;
layout(location = 0) out vec4 out_colour;

void main()
{
    out_colour = texture(u_texture, v_uv) * u_tint;
}
