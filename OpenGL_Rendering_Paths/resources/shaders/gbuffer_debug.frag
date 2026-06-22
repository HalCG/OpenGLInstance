#version 430 core
layout (location = 0) out vec4 FragColor;

in vec2 vTexCoord;

uniform sampler2D uGAlbedo;
uniform sampler2D uGNormal;
uniform sampler2D uGMaterial;
uniform int uDebugMode;

void main() {
    if (uDebugMode == 1) {
        FragColor = vec4(texture(uGAlbedo, vTexCoord).rgb, 1.0);
    } else if (uDebugMode == 2) {
        FragColor = vec4(normalize(texture(uGNormal, vTexCoord).rgb) * 0.5 + 0.5, 1.0);
    } else {
        FragColor = vec4(texture(uGMaterial, vTexCoord).rgb, 1.0);
    }
}
