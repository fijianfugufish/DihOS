#version 300 es
precision highp float;

uniform vec2 u_translation;
layout(location = 0) in vec2 a_position;

void main()
{
    gl_Position = vec4(a_position + u_translation, 0.0, 1.0);
}
