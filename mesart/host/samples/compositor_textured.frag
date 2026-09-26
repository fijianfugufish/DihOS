#version 300 es
/* Desktop-sized normalized coordinates need enough mantissa bits to select
 * every texel independently at 2944x1840.  mediump was sufficient for the
 * original 16x16 proof but visibly softened the wallpaper and could leave
 * short seams when the same frame was sampled twice. */
precision highp float;

in vec2 v_uv;
uniform sampler2D u_texture;
uniform vec4 u_tint;
out vec4 out_colour;

void main()
{
    out_colour = texture(u_texture, v_uv) * u_tint;
}
