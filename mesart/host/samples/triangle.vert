#version 300 es
precision highp float;

layout(location = 0) in vec2 a_position;

void main()
{
    /* The three positions live in a kernel-owned R32G32_FLOAT buffer.  They
     * form (-1,-1), (3,-1), (-1,3), an oversized triangle covering the
     * viewport.  This deliberately avoids the auto-index/gl_VertexID path
     * while preserving Mesa-generated IR3 and the A7xx vertex front end. */
    gl_Position = vec4(a_position, 0.0, 1.0);
}
