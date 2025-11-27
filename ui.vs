#version 330 core
layout (location = 0) in vec2 aPos;

uniform mat4 projection;

out vec3 vColor;

void main()
{
    vColor = vec3(1.0, 1.0, 1.0);
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
}
