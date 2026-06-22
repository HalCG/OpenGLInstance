#ifndef RENDER_TYPES_HPP
#define RENDER_TYPES_HPP

#include <glm/glm.hpp>

// 渲染路径枚举：Forward / Deferred / Forward+
enum class RenderPath {
    Forward = 0,
    Deferred = 1,
    ForwardPlus = 2
};

// GPU 点光源布局（与 shader SSBO 对齐）
struct GpuPointLight {
    glm::vec4 positionRadius;  // xyz = position, w = radius
    glm::vec4 colorIntensity;  // rgb = color, w = intensity
};

struct FrameCamera {
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 eye;
};

// 各 pass GPU 耗时 + 帧时间/FPS
struct FrameStats {
    float forwardPassMs = 0.0f;
    float geometryPassMs = 0.0f;
    float lightingPassMs = 0.0f;
    float cullPassMs = 0.0f;
    float shadingPassMs = 0.0f;
    float totalFrameMs = 0.0f;
    float fps = 0.0f;
};

#endif
