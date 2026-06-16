#ifndef ANTI_ALIASING_APP_HPP
#define ANTI_ALIASING_APP_HPP

#include "AATestScene.hpp"
#include "AppConfig.hpp"
#include "Framebuffer.hpp"
#include "PerfStats.hpp"
#include "PostProcess.hpp"
#include "RenderTypes.hpp"
#include "SceneRenderer.hpp"
#include "TaaPass.hpp"
#include "VtkTrackballCamera.hpp"

#include <GLFW/glfw3.h>

// AA 对比 Demo 主应用：管理窗口、输入、四种抗锯齿模式与渲染循环
class AntiAliasingApp {
public:
    bool init();    // 初始化窗口、场景、FBO、后处理与 TAA
    void run();     // 主循环：poll/wait 事件 → 按需 render+swap
    void shutdown(); // 释放 GPU/GLFW 资源

    static void framebufferSizeCallback(GLFWwindow *window, int width, int height); // 窗口 resize，重建 FBO 并重置 TAA history
    static void keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods); // 1-4 切 AA 模式、[ ] 调 MSAA 档位
    static void mouseButtonCallback(GLFWwindow *window, int button, int action, int mods);    // VTK 轨道球：LMB/MMB/RMB 拖拽
    static void cursorPosCallback(GLFWwindow *window, double xpos, double ypos);              // 拖拽中更新相机
    static void scrollCallback(GLFWwindow *window, double xoffset, double yoffset);           // 滚轮缩放

private:
    bool initWindow();                              // 创建 GLFW 窗口并加载 GLAD
    FrameCamera buildCamera(glm::vec2 jitterNdc) const; // 由轨道球参数组装 view/projection（TAA 时含 jitter）
    void renderFrame();                             // 一帧完整渲染：Scene Pass → Post Pass（按 AA 模式分支）
    void resizeTargets();                           // 按当前窗口尺寸重建 single/msaa FBO 与 TAA 缓冲
    std::string buildOverlayText() const;           // 生成窗口标题栏性能/模式文字
    void updateWindowTitle();                       // 将 cachedOverlayText_ 写入 GLFW 标题
    int nextMsaaPreset(int delta) const;            // [ ] 键切换 MSAA sample 档位（2x/4x）
    void markCameraDirty();                         // 标记需要重绘（输入、切模式、resize）

    GLFWwindow *window_ = nullptr;
    unsigned int width_ = AppConfig::kInitialWidth;
    unsigned int height_ = AppConfig::kInitialHeight;

    AAMode currentMode_ = AAMode::None;       // 当前 AA 模式，决定 scene/post 管线分支
    int msaaPresetIndex_ = AppConfig::kDefaultMsaaPresetIndex;
    VtkTrackballCamera camera_;
    bool cameraDirty_ = true;                 // 为 true 时本帧需要 render+swap；输入/模式切换会置位
    glm::mat4 prevViewProj_ = glm::mat4(1.0f); // TAA 重投影用的上一帧 VP
    bool hasPrevViewProj_ = false;            // 首帧或 resize/切模式后为 false，跳过 history 混合
    int titleUpdateCounter_ = 0;
    std::string cachedOverlayText_;

    AATestScene scene_;
    SceneRenderer sceneRenderer_;
    SingleSampleFbo singleFbo_;
    MsaaFbo msaaFbo_;
    PostProcess postProcess_;
    TaaPass taaPass_;
    PerfStats perf_;

    static AntiAliasingApp *s_instance_;
};

#endif
