#include "TaaPass.hpp"

#include "AppConfig.hpp"
#include "Shader.hpp"

namespace {
const float kQuadVerts[] = {
    -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f,
    1.0f,  1.0f,  1.0f, 1.0f, -1.0f, 1.0f,  0.0f, 1.0f,
};

// Halton 低差异序列，用于 TAA 子像素抖动采样
float halton(int index, int base) {
    float f = 1.0f;
    float r = 0.0f;
    int i = index;
    while (i > 0) {
        f /= static_cast<float>(base);
        r += f * static_cast<float>(i % base);
        i /= base;
    }
    return r;
}
} // namespace

bool TaaPass::init() {
    shader_ = Shader(AppConfig::shaderPath("fullscreen.vert").c_str(), AppConfig::shaderPath("taa.frag").c_str()).ID;

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

    locCurrentColor_ = glGetUniformLocation(shader_, "uCurrentColor");
    locCurrentDepth_ = glGetUniformLocation(shader_, "uCurrentDepth");
    locHistoryColor_ = glGetUniformLocation(shader_, "uHistoryColor");
    locInvViewProj_ = glGetUniformLocation(shader_, "uInvViewProj");
    locPrevViewProj_ = glGetUniformLocation(shader_, "uPrevViewProj");
    locTexelSize_ = glGetUniformLocation(shader_, "uTexelSize");
    locBlendFactor_ = glGetUniformLocation(shader_, "uBlendFactor");
    locHasHistory_ = glGetUniformLocation(shader_, "uHasHistory");

    return shader_ != 0;
}

void TaaPass::shutdown() {
    if (history_[0]) {
        glDeleteTextures(1, &history_[0]);
        history_[0] = 0;
    }
    if (history_[1]) {
        glDeleteTextures(1, &history_[1]);
        history_[1] = 0;
    }
    if (fbo_) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
    if (quadVbo_) {
        glDeleteBuffers(1, &quadVbo_);
        quadVbo_ = 0;
    }
    if (quadVao_) {
        glDeleteVertexArrays(1, &quadVao_);
        quadVao_ = 0;
    }
    if (shader_) {
        glDeleteProgram(shader_);
        shader_ = 0;
    }
}

void TaaPass::resize(int width, int height) {
    if (width == width_ && height == height_ && history_[0]) {
        return;
    }
    width_ = width;
    height_ = height;

    if (history_[0]) {
        glDeleteTextures(1, &history_[0]);
        history_[0] = 0;
    }
    if (history_[1]) {
        glDeleteTextures(1, &history_[1]);
        history_[1] = 0;
    }
    if (fbo_) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }

    glGenTextures(1, &history_[0]);
    glGenTextures(1, &history_[1]);
    for (GLuint tex : history_) {
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    glGenFramebuffers(1, &fbo_);
    currentIndex_ = 0;
    validHistory_ = false;
    frameIndex_ = 0;
}

void TaaPass::resetHistory() {
    validHistory_ = false; // 切模式/resize 后调用，下一帧 uHasHistory=0
    frameIndex_ = 0;
}

glm::vec2 TaaPass::nextJitter(int width, int height) {
    const float jx = halton((frameIndex_ % 16) + 1, 2) - 0.5f;
    const float jy = halton((frameIndex_ % 16) + 1, 3) - 0.5f;
    ++frameIndex_;
    return glm::vec2(jx / static_cast<float>(width), jy / static_cast<float>(height));
}

void TaaPass::apply(GLuint currentColor, GLuint currentDepth, const FrameCamera &camera, const glm::mat4 &prevViewProj,
                    bool hasHistory) {
    // 双缓冲 history：writeIndex 写入本帧结果，readIndex 供 shader 重投影采样
    const int writeIndex = 1 - currentIndex_;
    const int readIndex = currentIndex_;

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, history_[writeIndex], 0);

    glDisable(GL_DEPTH_TEST);
    glUseProgram(shader_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, currentColor);
    glUniform1i(locCurrentColor_, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, currentDepth);
    glUniform1i(locCurrentDepth_, 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, history_[readIndex]);
    glUniform1i(locHistoryColor_, 2);

    glUniformMatrix4fv(locInvViewProj_, 1, GL_FALSE, &camera.invViewProjection[0][0]);
    glUniformMatrix4fv(locPrevViewProj_, 1, GL_FALSE, &prevViewProj[0][0]);
    glUniform2f(locTexelSize_, 1.0f / static_cast<float>(width_), 1.0f / static_cast<float>(height_));
    glUniform1f(locBlendFactor_, 0.1f);
    glUniform1i(locHasHistory_, (hasHistory && validHistory_) ? 1 : 0);

    glBindVertexArray(quadVao_);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_DEPTH_TEST);

    currentIndex_ = writeIndex;
    validHistory_ = hasHistory;
}
