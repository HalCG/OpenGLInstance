/**
 * @file LinkedListOITApp.cpp
 * @brief OIT Linked List 透明渲染应用实现
 *
 * 渲染流程：
 * 1. Pass 1: 不透明物体 → opaqueFBO（Blinn-Phong 光照）
 * 2. Pass 2: 透明物体 → SSBO 链表存储 → oitRenderFBO
 * 3. Pass 3: 遍历链表排序混合 → 默认帧缓冲
 */

#include "LinkedListOITApp.hpp"

#include <cstdio>
#include <glad/glad.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>
#include <iostream>

// ---- 静态成员 ----
LinkedListOITApp *LinkedListOITApp::s_instance_ = nullptr;

// ---- 链表节点结构体（与 oitRender.frag 中的 ListNode 保持一致） ----
struct ListNode {
    glm::vec4 color;
    GLfloat depth;
    GLuint next;
};

// ===================================================================
// 公开接口
// ===================================================================

bool LinkedListOITApp::init() {
    s_instance_ = this;

    if (!initWindow())
        return false;
    if (!initShaders())
        return false;
    if (!initScene())
        return false;
    if (!initOITBuffers())
        return false;
    if (!initFramebuffers())
        return false;

    return true;
}

void LinkedListOITApp::run() {
    glViewport(0, 0, width_, height_);
    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    while (!glfwWindowShouldClose(window_)) {
        viewRotate_ += 1.0f;
        processInput(window_);

        renderOpaquePass();
        renderTransparentPass();
        renderCompositePass();

        glfwSwapBuffers(window_);
        glfwPollEvents();
    }
}

void LinkedListOITApp::shutdown() {
    glfwTerminate();
}

// ===================================================================
// GLFW 回调（静态）
// ===================================================================

void LinkedListOITApp::framebufferSizeCallback(GLFWwindow * /*window*/, int width, int height) {
    if (s_instance_) {
        s_instance_->width_ = width;
        s_instance_->height_ = height;
    }
    glViewport(0, 0, width, height);
}

void LinkedListOITApp::processInput(GLFWwindow *window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    } else if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
        if (s_instance_)
            s_instance_->viewRotate_ += 1.0f;
    } else if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
        if (s_instance_)
            s_instance_->viewRotate_ -= 1.0f;
    }
    if (s_instance_)
        printf("view_rotate:%f\n", s_instance_->viewRotate_);
}

// ===================================================================
// 初始化
// ===================================================================

bool LinkedListOITApp::initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    window_ = glfwCreateWindow(width_, height_, AppConfig::kWindowTitle, nullptr, nullptr);
    if (!window_) {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window_);
    glfwSetFramebufferSizeCallback(window_, framebufferSizeCallback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return false;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_MULTISAMPLE);

    return true;
}

bool LinkedListOITApp::initShaders() {
    blinnPhongShader_ = std::make_unique<Shader>(
        AppConfig::resourcePath("blinnPhong.vert").c_str(),
        AppConfig::resourcePath("blinnPhong.frag").c_str());
    oitRenderShader_ = std::make_unique<Shader>(
        AppConfig::resourcePath("oitRender.vert").c_str(),
        AppConfig::resourcePath("oitRender.frag").c_str());
    compositeShader_ = std::make_unique<Shader>(
        AppConfig::resourcePath("composite.vert").c_str(),
        AppConfig::resourcePath("composite.frag").c_str());
    quadShader_ = std::make_unique<Shader>(
        AppConfig::resourcePath("quad.vert").c_str(),
        AppConfig::resourcePath("quad.frag").c_str());
    return true;
}

bool LinkedListOITApp::initScene() {
    quad_ = std::make_unique<Model>(AppConfig::resourcePath("models/quad/quad.obj"));
    spot_ = std::make_unique<Model>(AppConfig::resourcePath("models/spot/spot.obj"));

    textureWindowR_ = std::make_unique<Texture>(AppConfig::resourcePath("models/quad/window-r.png"));
    textureWindowG_ = std::make_unique<Texture>(AppConfig::resourcePath("models/quad/window-g.png"));
    textureWindowB_ = std::make_unique<Texture>(AppConfig::resourcePath("models/quad/window-b.png"));
    textureSpot_ = std::make_unique<Texture>(AppConfig::resourcePath("models/spot/spot.png"));
    return true;
}

bool LinkedListOITApp::initOITBuffers() {
    maxNodes_ = width_ * height_ * 20;

    GLuint zero = 0;
    GLint nodeSize = 5 * sizeof(GLfloat) + sizeof(GLuint);

    // 原子计数器缓冲区
    glGenBuffers(1, &atomicBuffer_);
    glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, atomicBuffer_);
    glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(GLuint), nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &zero);

    // 链表存储缓冲区
    glGenBuffers(1, &linkedListBuffer_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, linkedListBuffer_);
    glBufferData(GL_SHADER_STORAGE_BUFFER, maxNodes_ * nodeSize, nullptr, GL_DYNAMIC_DRAW);

    // 头指针图像纹理
    glGenTextures(1, &headPtrTexture_);
    glBindTexture(GL_TEXTURE_2D, headPtrTexture_);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32UI, width_, height_);
    glBindImageTexture(0, headPtrTexture_, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
    glBindTexture(GL_TEXTURE_2D, 0);

    // 清空缓冲区（PBO）
    std::vector<GLuint> headPtrClearBuf(width_ * height_, 0xffffffff);
    glGenBuffers(1, &clearBuf_);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, clearBuf_);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, headPtrClearBuf.size() * sizeof(GLuint),
                 headPtrClearBuf.data(), GL_STATIC_COPY);

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, clearBuf_);
    glBindTexture(GL_TEXTURE_2D, headPtrTexture_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    return true;
}

bool LinkedListOITApp::initFramebuffers() {
    // ---- opaqueFBO: 不透明物体渲染目标 ----
    glGenFramebuffers(1, &opaqueFBO_);

    glGenTextures(1, &opaqueTexture_);
    glBindTexture(GL_TEXTURE_2D, opaqueTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width_, height_, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenTextures(1, &opaqueDepthTexture_);
    glBindTexture(GL_TEXTURE_2D, opaqueDepthTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width_, height_, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, opaqueFBO_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, opaqueTexture_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, opaqueDepthTexture_, 0);

    // GLenum drawBuffers[] = {GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT};
    // glDrawBuffers(2, drawBuffers);

    GLenum drawBuffers[] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, drawBuffers);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "ERROR::FRAMEBUFFER:: Opaque framebuffer is not complete!" << std::endl;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // ---- oitRenderFBO: 透明物体渲染目标 ----
    glGenTextures(1, &oitTexture_);
    glBindTexture(GL_TEXTURE_2D, oitTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width_, height_, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &oitRenderFBO_);
    glBindFramebuffer(GL_FRAMEBUFFER, oitRenderFBO_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, oitTexture_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, opaqueDepthTexture_, 0);

    // GLenum drawBuffersOIT[] = {GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT};
    // glDrawBuffers(2, drawBuffersOIT);

    GLenum drawBuffersOIT[] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, drawBuffersOIT);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "ERROR::FRAMEBUFFER:: OIT framebuffer is not complete!" << std::endl;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return true;
}

// ===================================================================
// 渲染 Pass
// ===================================================================

void LinkedListOITApp::renderOpaquePass() {
    /* Pass 1
       model: 不透明物体
       shader: blinnPhongShader
       depth test: 启用深度测试, 启用深度写入
       output: opaqueTexture, opaqueDepthTexture
    */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);

    blinnPhongShader_->use();
    blinnPhongShader_->setVec3("cameraPos", cameraPos_);
    blinnPhongShader_->setVec3("lightPos", lightPos_);
    blinnPhongShader_->setVec3("k", k_);

    glm::mat4 model = modelMatrix({0.0f, 0.0f, 0.0f}, 0.5f);

    glm::mat4 view = glm::lookAt(
        2.0f * glm::vec3(glm::sin(glm::radians(viewRotate_)), 0.0f, glm::cos(glm::radians(viewRotate_))),
        glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    glm::mat4 projection = glm::perspective(
        glm::radians(AppConfig::kFovDegrees), (float)(width_) / (float)(height_),
        AppConfig::kNearPlane, AppConfig::kFarPlane);

    blinnPhongShader_->setMat4("model", model);
    blinnPhongShader_->setMat4("view", view);
    blinnPhongShader_->setMat4("projection", projection);

    spot_->Draw(*blinnPhongShader_, opaqueFBO_,
                {{"diffuse_texture", textureSpot_->id}}, {}, GL_TRIANGLES, {true, true});
}

void LinkedListOITApp::renderTransparentPass() {
    /* Pass 2
       model: 透明物体
       shader: oitRenderShader
       depth test: 开启深度测试, 关闭深度写入
       output: SSBO 链表 + oitRenderFBO
    */
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    // 重置 OIT 缓冲区
    GLuint zero = 0;
    glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, atomicBuffer_);
    glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &zero);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, linkedListBuffer_);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, clearBuf_);
    glBindTexture(GL_TEXTURE_2D, headPtrTexture_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);

    oitRenderShader_->use();
    oitRenderShader_->setVec3("cameraPos", cameraPos_);
    oitRenderShader_->setVec3("lightPos", lightPos_);
    oitRenderShader_->setVec3("k", k_);
    oitRenderShader_->setUint("MaxNodes", maxNodes_);

    glm::mat4 view = glm::lookAt(
        2.0f * glm::vec3(glm::sin(glm::radians(viewRotate_)), 0.0f, glm::cos(glm::radians(viewRotate_))),
        glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    glm::mat4 projection = glm::perspective(
        glm::radians(AppConfig::kFovDegrees), (float)(width_) / (float)(height_),
        AppConfig::kNearPlane, AppConfig::kFarPlane);

    oitRenderShader_->setMat4("view", view);
    oitRenderShader_->setMat4("projection", projection);

    // 红色透明方块
    glm::mat4 model = modelMatrix({-0.5f, 0.0f, 0.8f}, 0.5f);
    oitRenderShader_->setMat4("model", model);
    quad_->Draw(*oitRenderShader_, oitRenderFBO_,
                {{"diffuse_texture", textureWindowR_->id}, {"texture_depth", opaqueDepthTexture_}},
                {}, GL_TRIANGLES, {true, false});

    // 绿色透明方块
    model = modelMatrix({0.2f, -0.5f, -1.0f}, 0.5f);
    oitRenderShader_->setMat4("model", model);
    quad_->Draw(*oitRenderShader_, oitRenderFBO_,
                {{"diffuse_texture", textureWindowG_->id}, {"texture_depth", opaqueDepthTexture_}},
                {}, GL_TRIANGLES, {false, false});

    // 蓝色透明方块
    model = modelMatrix({0.2f, 0.0f, -0.5f}, 0.5f);
    oitRenderShader_->setMat4("model", model);
    quad_->Draw(*oitRenderShader_, oitRenderFBO_,
                {{"diffuse_texture", textureWindowB_->id}, {"texture_depth", opaqueDepthTexture_}},
                {}, GL_TRIANGLES, {false, false});
}

void LinkedListOITApp::renderCompositePass() {
    /* Pass 3
       model: quad
       shader: compositeShader
       depth test: 开启深度测试, 开启深度写入
       output: 默认帧缓冲
    */
    // 确保 SSBO / Image / Atomic 写入完成
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT |
                    GL_ATOMIC_COUNTER_BARRIER_BIT);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    compositeShader_->use();
    quad_->Draw(*compositeShader_, 0,
                {{"texture_opaque", opaqueTexture_}}, {}, GL_TRIANGLES, {true, true});
}

// ===================================================================
// 辅助函数
// ===================================================================

glm::mat4 LinkedListOITApp::modelMatrix(const glm::vec3 &translation, float scale) const {
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, translation);
    model = glm::scale(model, glm::vec3(scale));
    return model;
}