#version 330 core
out vec4 FragColor;

in VS_OUT {
    vec3 FragPos;
    vec3 Normal;
    vec2 TexCoords;
} fs_in;

struct Material {
    sampler2D texture_diffuse1;
};

uniform Material material;

void main()
{
    vec3 baseColor = texture(material.texture_diffuse1, fs_in.TexCoords).rgb;
    // simple lighting แบบขำๆ
    vec3 lightDir = normalize(vec3(0.3, 1.0, 0.4));
    float diff = max(dot(normalize(fs_in.Normal), lightDir), 0.2); // มี minimum 0.2 ไม่ให้มืดสนิท
    vec3 color = baseColor * diff;
    FragColor = vec4(color, 1.0);
}
