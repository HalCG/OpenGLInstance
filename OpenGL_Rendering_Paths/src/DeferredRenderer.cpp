#include "DeferredRenderer.hpp"

#include "AppConfig.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <vector>

namespace {
const float kQuadVerts[] = {
    -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f,
    1.0f,  1.0f,  1.0f, 1.0f, -1.0f, 1.0f,  0.0f, 1.0f,
};
} // namespace

bool DeferredRenderer::init() {
    geometryShader_ = std::make_unique<Shader>(AppConfig::shaderPath("mesh.vert").c_str(),
                                               AppConfig::shaderPath("geometry.frag").c_str());
    lightingShader_ = std::make_unique<Shader>(AppConfig::shaderPath("fullscreen.vert").c_str(),
                                               AppConfig::shaderPath("deferred_lighting.frag").c_str());
    debugShader_ = std::make_unique<Shader>(AppConfig::shaderPath("fullscreen.vert").c_str(),
                                            AppConfig::shaderPath("gbuffer_debug.frag").c_str());

    glGenVertexArrays(1, &quadVao_);
    glGenBuffers(1, &quadVbo_);
    glBindVertexArray(quadVao_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVerts), kQuadVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void *>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void *>(2 * sizeof(float)));
    glBindVertexArray(0);

    return geometryShader_ && lightingShader_ && debugShader_ && geometryShader_->ID != 0;
}

void DeferredRenderer::shutdown() {
    destroyGBuffer();
    geometryShader_.reset();
    lightingShader_.reset();
    debugShader_.reset();
    if (quadVbo_) {
        glDeleteBuffers(1, &quadVbo_);
        quadVbo_ = 0;
    }
    if (quadVao_) {
        glDeleteVertexArrays(1, &quadVao_);
        quadVao_ = 0;
    }
}

void DeferredRenderer::resize(int width, int height) {
    if (width == width_ && height == height_ && gBufferFbo_) {
        return;
    }
    destroyGBuffer();
    createGBuffer(width, height);
    width_ = width;
    height_ = height;
}

// 分配 GBuffer FBO：albedo(RGBA8) + normal(RGB16F) + material(RGBA8) + depth
void DeferredRenderer::createGBuffer(int width, int height) {
    glGenFramebuffers(1, &gBufferFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, gBufferFbo_);

    glGenTextures(1, &gAlbedo_);
    glBindTexture(GL_TEXTURE_2D, gAlbedo_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gAlbedo_, 0);

    glGenTextures(1, &gNormal_);
    glBindTexture(GL_TEXTURE_2D, gNormal_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gNormal_, 0);

    glGenTextures(1, &gMaterial_);
    glBindTexture(GL_TEXTURE_2D, gMaterial_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, gMaterial_, 0);

    glGenTextures(1, &gDepth_);
    glBindTexture(GL_TEXTURE_2D, gDepth_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gDepth_, 0);

    const GLenum attachments[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
    glDrawBuffers(3, attachments);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        // Keep running; error will show as blank output.
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void DeferredRenderer::destroyGBuffer() {
    if (gAlbedo_) {
        glDeleteTextures(1, &gAlbedo_);
        gAlbedo_ = 0;
    }
    if (gNormal_) {
        glDeleteTextures(1, &gNormal_);
        gNormal_ = 0;
    }
    if (gMaterial_) {
        glDeleteTextures(1, &gMaterial_);
        gMaterial_ = 0;
    }
    if (gDepth_) {
        glDeleteTextures(1, &gDepth_);
        gDepth_ = 0;
    }
    if (gBufferFbo_) {
        glDeleteFramebuffers(1, &gBufferFbo_);
        gBufferFbo_ = 0;
    }
}

void DeferredRenderer::drawFullscreenQuad() {
    glBindVertexArray(quadVao_);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);
}

void DeferredRenderer::render(const Scene &scene, LightManager &lights, const FrameCamera &camera, int width,
                              int height, PerfStats &stats, bool showGBufferDebug) {
    resize(width, height);

    // Geometry Pass：写入 GBuffer（albedo / normal / material / depth）
    stats.geometryPass.begin();
    glBindFramebuffer(GL_FRAMEBUFFER, gBufferFbo_);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    geometryShader_->use();
    geometryShader_->setMat4("view", camera.view);
    geometryShader_->setMat4("projection", camera.projection);
    scene.drawFloor(*geometryShader_);
    scene.drawSpotMeshes(*geometryShader_);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    stats.frameStats().geometryPassMs = stats.geometryPass.endMs();

    if (showGBufferDebug) {
        // Debug 分支：跳过 lighting pass，直接全屏显示 GBuffer 附件
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        debugShader_->use();
        debugShader_->setInt("uDebugMode", 1);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gAlbedo_);
        debugShader_->setInt("uGAlbedo", 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, gNormal_);
        debugShader_->setInt("uGNormal", 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, gMaterial_);
        debugShader_->setInt("uGMaterial", 2);
        drawFullscreenQuad();
        stats.frameStats().lightingPassMs = 0.0f;
        return;
    }

    stats.lightingPass.begin();
    glClearColor(0.08f, 0.09f, 0.12f,                               1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    lights.uploadToGpu();
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, lights.lightBuffer());

    lightingShader_->use();
    lightingShader_->setVec3("uCameraPos", camera.eye);
    lightingShader_->setMat4("uInvView", glm::inverse(camera.view));
    lightingShader_->setMat4("uInvProjection", glm::inverse(camera.projection));
    lightingShader_->setInt("uLightCount", lights.activeCount());

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gAlbedo_);
    lightingShader_->setInt("uGAlbedo", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gNormal_);
    lightingShader_->setInt("uGNormal", 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, gMaterial_);
    lightingShader_->setInt("uGMaterial", 2);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, gDepth_);
    lightingShader_->setInt("uGDepth", 3);

    drawFullscreenQuad();
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
    glEnable(GL_DEPTH_TEST);

    stats.frameStats().lightingPassMs = stats.lightingPass.endMs();
}
