#include "Framebuffer.hpp"

void SingleSampleFbo::create(int width, int height) {
    destroy();
    width_ = width;
    height_ = height;

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    glGenTextures(1, &color_);
    glBindTexture(GL_TEXTURE_2D, color_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_, 0);

    glGenTextures(1, &depth_);
    glBindTexture(GL_TEXTURE_2D, depth_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_, 0);

    const GLenum drawBuf = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuf);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SingleSampleFbo::destroy() {
    if (color_) {
        glDeleteTextures(1, &color_);
        color_ = 0;
    }
    if (depth_) {
        glDeleteTextures(1, &depth_);
        depth_ = 0;
    }
    if (fbo_) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
}

void SingleSampleFbo::resize(int width, int height) {
    if (width == width_ && height == height_ && fbo_) {
        return;
    }
    create(width, height);
}

void SingleSampleFbo::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
}

void SingleSampleFbo::unbind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SingleSampleFbo::blitColorToDefault(int width, int height) const {
    // None 模式：离屏 color 纹理直接 blit 到默认 framebuffer（窗口）
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, width_, height_, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void MsaaFbo::create(int width, int height, int samples) {
    destroy();
    width_ = width;
    height_ = height;
    samples_ = samples;

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    glGenTextures(1, &colorMs_);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, colorMs_);
    glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples_, GL_RGBA8, width, height, GL_TRUE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, colorMs_, 0);

    glGenTextures(1, &depthMs_);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, depthMs_);
    glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples_, GL_DEPTH_COMPONENT32F, width, height, GL_TRUE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D_MULTISAMPLE, depthMs_, 0);

    const GLenum drawBuf = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuf);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void MsaaFbo::destroy() {
    if (colorMs_) {
        glDeleteTextures(1, &colorMs_);
        colorMs_ = 0;
    }
    if (depthMs_) {
        glDeleteTextures(1, &depthMs_);
        depthMs_ = 0;
    }
    if (fbo_) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
}

void MsaaFbo::resize(int width, int height, int samples) {
    if (width == width_ && height == height_ && samples == samples_ && fbo_) {
        return;
    }
    create(width, height, samples);
}

void MsaaFbo::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
}

void MsaaFbo::unbind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void MsaaFbo::resolveColorToDefault(int width, int height) const {
    // MSAA 模式：multisample 颜色 resolve 到屏幕（等价于硬件 MSAA resolve）
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, width_, height_, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
