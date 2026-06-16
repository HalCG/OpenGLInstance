#ifndef RENDER_TYPES_HPP
#define RENDER_TYPES_HPP

#include <glm/glm.hpp>

// 抗锯齿模式：None=直出 / MSAA=硬件多重采样 / FXAA=后处理 / TAA=时间累积
enum class AAMode {
    None = 0,
    MSAA = 1,
    FXAA = 2,
    TAA = 3
};

// 单帧相机参数（view/projection 及 TAA 用逆矩阵、抖动）
struct FrameCamera {
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 viewProjection;
    glm::mat4 invViewProjection = glm::mat4(1.0f);
    glm::vec3 eye;
    glm::vec2 jitterNdc = glm::vec2(0.0f);
};

// 每帧性能数据（毫秒 + FPS）
struct FrameStats {
    float scenePassMs = 0.0f;
    float postPassMs = 0.0f;
    float totalFrameMs = 0.0f;
    float fps = 0.0f;
};

#endif
