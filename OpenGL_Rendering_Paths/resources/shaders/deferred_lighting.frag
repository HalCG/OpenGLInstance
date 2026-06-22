#version 430 core
layout (location = 0) out vec4 FragColor;

in vec2 vTexCoord;

uniform sampler2D uGAlbedo;
uniform sampler2D uGNormal;
uniform sampler2D uGMaterial;
uniform sampler2D uGDepth;

uniform vec3 uCameraPos;
uniform mat4 uInvView;
uniform mat4 uInvProjection;

struct PointLight {
    vec4 positionRadius;
    vec4 colorIntensity;
};

layout(std430, binding = 0) readonly buffer LightBuffer {
    PointLight lights[];
};

uniform int uLightCount;

vec3 reconstructWorldPos(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 viewPos = uInvProjection * clip;
    viewPos /= viewPos.w;
    vec4 worldPos = uInvView * viewPos;
    return worldPos.xyz;
}

void main() {
    vec3 albedo = texture(uGAlbedo, vTexCoord).rgb;
    vec3 normal = normalize(texture(uGNormal, vTexCoord).rgb);
    vec3 materialK = texture(uGMaterial, vTexCoord).rgb;
    float depth = texture(uGDepth, vTexCoord).r;
    vec3 worldPos = reconstructWorldPos(vTexCoord, depth);
    vec3 viewDir = normalize(uCameraPos - worldPos);

    vec3 result = materialK.x * albedo;
    int count = min(uLightCount, 512);

    for (int i = 0; i < count; ++i) {
        vec3 lightPos = lights[i].positionRadius.xyz;
        float radius = lights[i].positionRadius.w;
        vec3 lightColor = lights[i].colorIntensity.rgb * lights[i].colorIntensity.w;

        vec3 lightDir = lightPos - worldPos;
        float dist = length(lightDir);
        if (dist > radius) {
            continue;
        }
        lightDir = normalize(lightDir);

        float attenuation = 1.0 - smoothstep(radius * 0.7, radius, dist);
        vec3 diffuse = materialK.y * max(dot(normal, lightDir), 0.0) * lightColor * albedo;
        vec3 halfway = normalize(lightDir + viewDir);
        vec3 specular = materialK.z * pow(max(dot(normal, halfway), 0.0), 32.0) * lightColor;
        result += (diffuse + specular) * attenuation;
    }

    FragColor = vec4(result, 1.0);
}
