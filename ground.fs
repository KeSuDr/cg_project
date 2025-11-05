#version 330 core
out vec4 FragColor;

in vec2 TexCoord;
uniform sampler2D groundTex;

void main() {
    vec3 col = texture(groundTex, TexCoord).rgb;
    FragColor = vec4(col, 1.0);
}
