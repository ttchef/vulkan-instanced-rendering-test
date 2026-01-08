#version 450

layout (location = 0) in vec2 in_pos;
layout (location = 1) in vec3 in_color;

layout(location = 0) out vec3 vColor;

void main() {
    const vec2 pos[3] = vec2[](
        vec2( 0.0, -0.6),
        vec2( 0.6,  0.6),
        vec2(-0.6,  0.6)
    );

    gl_Position = vec4(in_pos, 0.0, 1.0);
    gl_PointSize = 3.0;

    const vec3 col[3] = vec3[](
        vec3(1.0, 0.0, 0.0),
        vec3(0.0, 1.0, 0.0),
        vec3(0.0, 0.0, 1.0)
    );

    vColor = in_color;
}

