#version 430 core
layout (location = 0) out vec4 FragColor;

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoords;

uniform vec3 uCameraPos;
uniform vec3 uMaterialK;
uniform vec3 uLightPos0;
uniform vec3 uLightPos1;
uniform vec3 uLightColor0;
uniform vec3 uLightColor1;
uniform sampler2D texture_diffuse1;

void main() {
    vec3 albedo = texture(texture_diffuse1, vTexCoords).rgb;
    vec3 normal = normalize(vNormal);
    vec3 viewDir = normalize(uCameraPos - vWorldPos);
    vec3 result = uMaterialK.x * albedo;

    vec3 lights[2] = vec3[](uLightPos0, uLightPos1);
    vec3 colors[2] = vec3[](uLightColor0, uLightColor1);

    for (int i = 0; i < 2; ++i) {
        vec3 lightDir = normalize(lights[i] - vWorldPos);
        vec3 diffuse = uMaterialK.y * max(dot(normal, lightDir), 0.0) * colors[i] * albedo;
        vec3 halfway = normalize(lightDir + viewDir);
        vec3 specular = uMaterialK.z * pow(max(dot(normal, halfway), 0.0), 64.0) * colors[i];
        result += diffuse + specular;
    }

    FragColor = vec4(result, 1.0);
}
