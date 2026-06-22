#version 430 core
layout (location = 0) out vec4 FragColor;

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoords;

struct PointLight {
    vec4 positionRadius;
    vec4 colorIntensity;
};

layout(std430, binding = 0) readonly buffer LightBuffer {
    PointLight lights[];
};

layout(std430, binding = 1) readonly buffer TileCounts {
    uint counts[];
};

layout(std430, binding = 2) readonly buffer TileIndices {
    uint indices[];
};

uniform vec3 uCameraPos;
uniform vec3 uMaterialK;
uniform int uLightCount;
uniform int uTilesX;
uniform int uTilesY;
uniform int uTileSize;
uniform int uMaxLightsPerTile;
uniform sampler2D texture_diffuse1;

void main() {
    ivec2 tile = ivec2(gl_FragCoord.xy) / uTileSize;
    tile = clamp(tile, ivec2(0), ivec2(uTilesX - 1, uTilesY - 1));
    int tileIndex = tile.y * uTilesX + tile.x;
    uint localCount = counts[tileIndex];
    localCount = min(localCount, uint(uMaxLightsPerTile));

    vec3 albedo = texture(texture_diffuse1, vTexCoords).rgb;
    vec3 normal = normalize(vNormal);
    vec3 viewDir = normalize(uCameraPos - vWorldPos);
    vec3 result = uMaterialK.x * albedo;

    for (uint i = 0u; i < localCount; ++i) {
        uint lightIndex = indices[tileIndex * uMaxLightsPerTile + int(i)];
        if (lightIndex >= uint(uLightCount)) {
            continue;
        }

        vec3 lightPos = lights[lightIndex].positionRadius.xyz;
        float radius = lights[lightIndex].positionRadius.w;
        vec3 lightColor = lights[lightIndex].colorIntensity.rgb * lights[lightIndex].colorIntensity.w;

        vec3 lightDir = lightPos - vWorldPos;
        float dist = length(lightDir);
        if (dist > radius) {
            continue;
        }
        lightDir = normalize(lightDir);

        float attenuation = 1.0 - smoothstep(radius * 0.7, radius, dist);
        vec3 diffuse = uMaterialK.y * max(dot(normal, lightDir), 0.0) * lightColor * albedo;
        vec3 halfway = normalize(lightDir + viewDir);
        vec3 specular = uMaterialK.z * pow(max(dot(normal, halfway), 0.0), 32.0) * lightColor;
        result += (diffuse + specular) * attenuation;
    }

    FragColor = vec4(result, 1.0);
}
