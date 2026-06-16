#include "AntiAliasingApp.hpp"

#include <glad/glad.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/common.hpp>
#include <glm/trigonometric.hpp>
#include <iostream>
#include <sstream>

AntiAliasingApp *AntiAliasingApp::s_instance_ = nullptr;

namespace {
const char *modeName(AAMode mode) {
    switch (mode) {
    case AAMode::None:
        return "None";
    case AAMode::MSAA:
        return "MSAA";
    case AAMode::FXAA:
        return "FXAA";
    case AAMode::TAA:
        return "TAA";
    }
    return "Unknown";
}

// 将 MSAA sample 数限制在 GPU 支持的范围内
int clampMsaaSamples(int requested) {
    GLint maxSamples = 4;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    if (requested > maxSamples) {
        return maxSamples;
    }
    if (requested < 2) {
        return 1;
    }
    return requested;
}
} // namespace

bool AntiAliasingApp::init() {
    s_instance_ = this;
    if (!initWindow()) {
        return false;
    }
    if (!scene_.init()) {
        return false;
    }
    if (!sceneRenderer_.init() || !postProcess_.init() || !taaPass_.init()) {
        return false;
    }

    perf_.scenePass.init();
    perf_.postPass.init();

    resizeTargets();
    glViewport(0, 0, static_cast<GLsizei>(width_), static_cast<GLsizei>(height_));
    cachedOverlayText_ = buildOverlayText();
    return true;
}

bool AntiAliasingApp::initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 0);

    window_ = glfwCreateWindow(static_cast<int>(width_), static_cast<int>(height_), AppConfig::kWindowTitle, nullptr,
                               nullptr);
    if (!window_) {
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window_);
    glfwSetFramebufferSizeCallback(window_, framebufferSizeCallback);
    glfwSetKeyCallback(window_, keyCallback);
    glfwSetMouseButtonCallback(window_, mouseButtonCallback);
    glfwSetCursorPosCallback(window_, cursorPosCallback);
    glfwSetScrollCallback(window_, scrollCallback);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        return false;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    return true;
}

// 窗口尺寸变化时同步重建所有离屏渲染目标
void AntiAliasingApp::resizeTargets() {
    if (width_ == 0 || height_ == 0) {
        return;
    }
    singleFbo_.resize(static_cast<int>(width_), static_cast<int>(height_));
    const int samples = clampMsaaSamples(AppConfig::kMsaaSamplesPresets[msaaPresetIndex_]);
    msaaFbo_.resize(static_cast<int>(width_), static_cast<int>(height_), samples);
    postProcess_.resize(static_cast<int>(width_), static_cast<int>(height_));
    taaPass_.resize(static_cast<int>(width_), static_cast<int>(height_));
}

void AntiAliasingApp::markCameraDirty() {
    // 统一入口：相机移动、按键切模式、窗口 resize 都会触发重绘
    cameraDirty_ = true;
}

// 组装 view/projection/VP/invVP；TAA 模式下 projection 含 jitter 偏移
FrameCamera AntiAliasingApp::buildCamera(glm::vec2 jitterNdc) const {
    FrameCamera camera;
    camera.eye = camera_.eye();
    camera.view = camera_.viewMatrix();
    const float aspect =
        static_cast<float>(width_ > 0 ? width_ : 1) / static_cast<float>(height_ > 0 ? height_ : 1);
    camera.projection = glm::perspective(glm::radians(AppConfig::kFovDegrees), aspect, AppConfig::kNearPlane,
                                        AppConfig::kFarPlane);
    if (currentMode_ == AAMode::TAA) {
        // TAA 需要子像素抖动，写入 projection 的第三列偏移
        camera.projection[2][0] += jitterNdc.x * 2.0f;
        camera.projection[2][1] += jitterNdc.y * 2.0f;
    }
    camera.viewProjection = camera.projection * camera.view;
    camera.invViewProjection = glm::inverse(camera.viewProjection);
    camera.jitterNdc = jitterNdc;
    return camera;
}

void AntiAliasingApp::renderFrame() {
    if (width_ == 0 || height_ == 0) {
        return;
    }
    perf_.beginFrame();

    const bool measureGpu = !camera_.isDragging(); // 拖拽时关闭 GPU timer，减轻卡顿
    perf_.scenePass.setEnabled(measureGpu);
    perf_.postPass.setEnabled(measureGpu);

    glm::vec2 jitter(0.0f);
    if (currentMode_ == AAMode::TAA) {
        jitter = taaPass_.nextJitter(static_cast<int>(width_), static_cast<int>(height_));
    }
    const FrameCamera camera = buildCamera(jitter);

    perf_.scenePass.begin();
    // Scene Pass：MSAA 写入 multisample FBO，其余模式写入 singleFbo_
    if (currentMode_ == AAMode::MSAA) {
        glEnable(GL_MULTISAMPLE);
        msaaFbo_.bind();
    } else {
        glDisable(GL_MULTISAMPLE);
        singleFbo_.bind();
    }

    glClearColor(0.12f, 0.14f, 0.18f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    sceneRenderer_.render(scene_, camera);

    if (currentMode_ == AAMode::MSAA) {
        msaaFbo_.unbind();
    } else {
        singleFbo_.unbind();
    }
    perf_.frameStats().scenePassMs = perf_.scenePass.endMs();

    perf_.postPass.begin();
    // Post Pass：按 currentMode_ 选择 blit / MSAA resolve / FXAA / TAA 输出路径
    switch (currentMode_) {
    case AAMode::None:
        singleFbo_.blitColorToDefault(static_cast<int>(width_), static_cast<int>(height_));
        break;
    case AAMode::MSAA:
        msaaFbo_.resolveColorToDefault(static_cast<int>(width_), static_cast<int>(height_));
        break;
    case AAMode::FXAA:
        postProcess_.applyFxaa(singleFbo_.colorTexture());
        break;
    case AAMode::TAA:
        taaPass_.apply(singleFbo_.colorTexture(), singleFbo_.depthTexture(), camera, prevViewProj_,
                       hasPrevViewProj_);
        postProcess_.blitTexture(taaPass_.outputTexture());
        break;
    }
    perf_.frameStats().postPassMs = perf_.postPass.endMs();

    prevViewProj_ = camera.viewProjection;
    hasPrevViewProj_ = true; // 供下一帧 TAA 重投影使用

    perf_.endFrame();
    if (!camera_.isDragging() && ++titleUpdateCounter_ % 15 == 0) {
        cachedOverlayText_ = buildOverlayText();
        updateWindowTitle();
    }
}

void AntiAliasingApp::run() {
    int frameCounter = 0;
    while (!glfwWindowShouldClose(window_)) {
        // TAA 每帧都需要新样本；其它模式仅在 cameraDirty_ 时渲染（事件驱动省电）
        const bool continuousRender = currentMode_ == AAMode::TAA;
        const bool active = cameraDirty_ || continuousRender || camera_.isDragging();
        if (active) {
            glfwPollEvents(); // 先处理输入，再 render，减少相机 1 帧延迟
        } else {
            glfwWaitEvents();
        }

        if (cameraDirty_ || continuousRender) {
            renderFrame();
            glfwSwapBuffers(window_);
            if (!continuousRender) {
                cameraDirty_ = false; // TAA 模式保持 dirty，持续出帧
            }

            if (++frameCounter % 120 == 0) {
                const FrameStats stats = perf_.latest();
                std::cout << "mode," << modeName(currentMode_) << ",msaa,"
                          << (currentMode_ == AAMode::MSAA ? msaaFbo_.samples() : 0) << ",fps," << stats.fps
                          << ",frame_ms," << stats.totalFrameMs << ",scene_ms," << stats.scenePassMs << ",post_ms,"
                          << stats.postPassMs << std::endl;
            }
        }
    }
}

void AntiAliasingApp::shutdown() {
    taaPass_.shutdown();
    postProcess_.shutdown();
    sceneRenderer_.shutdown();
    msaaFbo_.destroy();
    singleFbo_.destroy();
    perf_.postPass.shutdown();
    perf_.scenePass.shutdown();
    scene_.shutdown();
    glfwTerminate();
    s_instance_ = nullptr;
}

int AntiAliasingApp::nextMsaaPreset(int delta) const {
    int idx = msaaPresetIndex_ + delta;
    if (idx < 0) {
        idx = 0;
    }
    if (idx >= AppConfig::kMsaaPresetCount) {
        idx = AppConfig::kMsaaPresetCount - 1;
    }
    return idx;
}

std::string AntiAliasingApp::buildOverlayText() const {
    const FrameStats stats = perf_.latest();
    std::ostringstream oss;
    oss << modeName(currentMode_);
    if (currentMode_ == AAMode::MSAA) {
        oss << " x" << msaaFbo_.samples();
    }
    oss << " | FPS=" << static_cast<int>(stats.fps) << " | frame=" << stats.totalFrameMs
        << "ms | scene=" << stats.scenePassMs << "ms post=" << stats.postPassMs
        << "ms | VTK: LMB rotate, MMB pan, RMB dolly, wheel zoom, 1-4 mode, ESC quit";
    return oss.str();
}

void AntiAliasingApp::updateWindowTitle() {
    if (window_) {
        glfwSetWindowTitle(window_, cachedOverlayText_.c_str());
    }
}

void AntiAliasingApp::framebufferSizeCallback(GLFWwindow * /*window*/, int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    if (s_instance_) {
        s_instance_->width_ = static_cast<unsigned int>(width);
        s_instance_->height_ = static_cast<unsigned int>(height);
        s_instance_->resizeTargets();
        s_instance_->taaPass_.resetHistory();
        s_instance_->hasPrevViewProj_ = false; // resize 后 history 无效，避免 TAA 鬼影
        s_instance_->markCameraDirty();
    }
    glViewport(0, 0, width, height);
}

void AntiAliasingApp::mouseButtonCallback(GLFWwindow *window, int button, int action, int mods) {
    (void)mods;
    if (!s_instance_) {
        return;
    }

    CameraDragMode mode = CameraDragMode::None;
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        mode = CameraDragMode::Rotate;
    } else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        mode = CameraDragMode::Pan;
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        mode = CameraDragMode::Dolly;
    } else {
        return;
    }

    if (action == GLFW_PRESS) {
        double x = 0.0;
        double y = 0.0;
        glfwGetCursorPos(window, &x, &y);
        s_instance_->camera_.beginDrag(mode, x, y);
        s_instance_->markCameraDirty();
    } else if (action == GLFW_RELEASE) {
        s_instance_->camera_.endDrag();
        s_instance_->cachedOverlayText_ = s_instance_->buildOverlayText();
        s_instance_->updateWindowTitle();
    }
}

void AntiAliasingApp::cursorPosCallback(GLFWwindow * /*window*/, double xpos, double ypos) {
    if (!s_instance_) {
        return;
    }
    if (s_instance_->camera_.applyCursorDelta(xpos, ypos, s_instance_->width_, s_instance_->height_)) {
        s_instance_->markCameraDirty();
    }
}

void AntiAliasingApp::scrollCallback(GLFWwindow * /*window*/, double /*xoffset*/, double yoffset) {
    if (!s_instance_) {
        return;
    }

    s_instance_->camera_.applyScroll(yoffset);
    s_instance_->markCameraDirty();
}

void AntiAliasingApp::keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods) {
    (void)scancode;
    (void)mods;
    if (action != GLFW_PRESS || !s_instance_) {
        return;
    }

    s_instance_->markCameraDirty();

    // 1-4 切换 AA 模式；离开 TAA 或改 MSAA 档位时需重置 history / FBO
    switch (key) {
    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(window, true);
        break;
    case GLFW_KEY_1:
    case GLFW_KEY_F1:
        s_instance_->currentMode_ = AAMode::None;
        s_instance_->taaPass_.resetHistory();
        s_instance_->hasPrevViewProj_ = false;
        break;
    case GLFW_KEY_2:
    case GLFW_KEY_F2:
        s_instance_->currentMode_ = AAMode::MSAA;
        s_instance_->taaPass_.resetHistory();
        s_instance_->hasPrevViewProj_ = false;
        s_instance_->resizeTargets();
        break;
    case GLFW_KEY_3:
    case GLFW_KEY_F3:
        s_instance_->currentMode_ = AAMode::FXAA;
        s_instance_->taaPass_.resetHistory();
        s_instance_->hasPrevViewProj_ = false;
        break;
    case GLFW_KEY_4:
    case GLFW_KEY_F4:
        s_instance_->currentMode_ = AAMode::TAA;
        s_instance_->taaPass_.resetHistory();
        s_instance_->hasPrevViewProj_ = false;
        break;
    case GLFW_KEY_LEFT_BRACKET: {
        const int idx = s_instance_->nextMsaaPreset(-1);
        if (idx != s_instance_->msaaPresetIndex_) {
            s_instance_->msaaPresetIndex_ = idx;
            s_instance_->resizeTargets();
        }
        break;
    }
    case GLFW_KEY_RIGHT_BRACKET: {
        const int idx = s_instance_->nextMsaaPreset(1);
        if (idx != s_instance_->msaaPresetIndex_) {
            s_instance_->msaaPresetIndex_ = idx;
            s_instance_->resizeTargets();
        }
        break;
    }
    default:
        break;
    }
}
