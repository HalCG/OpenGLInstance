#version 430 core
layout (location = 0) out vec4 FragColor;

in vec2 vTexCoord;

uniform sampler2D uCurrentColor;
uniform sampler2D uCurrentDepth;
uniform sampler2D uHistoryColor;

uniform mat4 uInvViewProj;
uniform mat4 uPrevViewProj;
uniform vec2 uTexelSize;
uniform float uBlendFactor;
uniform bool uHasHistory;

vec3 reprojectHistory(vec2 uv, out bool valid) {
    float depth = texture(uCurrentDepth, uv).r;
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProj * ndc;
    world /= world.w;
    vec4 prevNdc = uPrevViewProj * world;
    prevNdc /= prevNdc.w;
    vec2 prevUv = prevNdc.xy * 0.5 + 0.5;
    valid = prevUv.x >= 0.0 && prevUv.x <= 1.0 && prevUv.y >= 0.0 && prevUv.y <= 1.0;
    return texture(uHistoryColor, prevUv).rgb;
}

vec3 clipHistory(vec3 history, vec3 current) {
    vec3 minC = current;
    vec3 maxC = current;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec2 uv = vTexCoord + vec2(float(x), float(y)) * uTexelSize;
            vec3 c = texture(uCurrentColor, uv).rgb;
            minC = min(minC, c);
            maxC = max(maxC, c);
        }
    }
    return clamp(history, minC, maxC);
}

void main() {
    vec3 current = texture(uCurrentColor, vTexCoord).rgb;
    if (!uHasHistory) {
        FragColor = vec4(current, 1.0);
        return;
    }

    bool valid;
    vec3 history = reprojectHistory(vTexCoord, valid);
    if (!valid) {
        FragColor = vec4(current, 1.0);
        return;
    }

    history = clipHistory(history, current);
    vec3 result = mix(history, current, uBlendFactor);
    FragColor = vec4(result, 1.0);
}
