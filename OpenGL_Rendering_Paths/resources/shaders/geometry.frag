#version 430 core
layout (location = 0) out vec4 gAlbedo;
layout (location = 1) out vec3 gNormal;
layout (location = 2) out vec4 gMaterial;

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoords;

uniform sampler2D texture_diffuse1;

void main() {
    gAlbedo = texture(texture_diffuse1, vTexCoords);
    gNormal = normalize(vNormal);
    gMaterial = vec4(0.15, 0.75, 0.35, 1.0);
}
