#version 430 core
layout (location = 0) out vec4 FragColor;

in vec2 vTexCoord;

uniform sampler2D uInput;
uniform vec2 uTexelSize;

float luma(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

void main() {
    vec3 rgbM = texture(uInput, vTexCoord).rgb;
    vec3 rgbN = texture(uInput, vTexCoord + vec2(0.0, uTexelSize.y)).rgb;
    vec3 rgbS = texture(uInput, vTexCoord - vec2(0.0, uTexelSize.y)).rgb;
    vec3 rgbE = texture(uInput, vTexCoord + vec2(uTexelSize.x, 0.0)).rgb;
    vec3 rgbW = texture(uInput, vTexCoord - vec2(uTexelSize.x, 0.0)).rgb;

    float lumaM = luma(rgbM);
    float lumaN = luma(rgbN);
    float lumaS = luma(rgbS);
    float lumaE = luma(rgbE);
    float lumaW = luma(rgbW);
    float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
    float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));

    float edgeVert = abs((lumaN + lumaS) - 2.0 * lumaM);
    float edgeHorz = abs((lumaE + lumaW) - 2.0 * lumaM);
    bool horz = edgeHorz >= edgeVert;

    vec2 offset = horz ? vec2(uTexelSize.x, 0.0) : vec2(0.0, uTexelSize.y);
    vec3 rgbA = texture(uInput, vTexCoord - offset).rgb;
    vec3 rgbB = texture(uInput, vTexCoord + offset).rgb;
    vec3 result = mix(rgbA, rgbB, 0.5);

    float lumaResult = luma(result);
    result = clamp(result, min(rgbM, result), max(rgbM, result));
    if (lumaResult < lumaMin || lumaResult > lumaMax) {
        result = rgbM;
    }

    FragColor = vec4(result, 1.0);
}
