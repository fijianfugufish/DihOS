#version 300 es
precision mediump float;

uniform vec4 u_colour;
layout(location = 0) out vec4 out_colour;

void main()
{
    out_colour = u_colour;
}
