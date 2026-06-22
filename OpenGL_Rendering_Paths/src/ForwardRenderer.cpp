#include "ForwardRenderer.hpp"

#include "AppConfig.hpp"

#include <glad/glad.h>

bool ForwardRenderer::init() {
    shader_ = std::make_unique<Shader>(AppConfig::shaderPath("mesh.vert").c_str(),
                                       AppConfig::shaderPath("forward.frag").c_str());
    return shader_ && shader_->ID != 0;
}

void ForwardRenderer::shutdown() {
    shader_.reset();
}

void ForwardRenderer::render(const Scene &scene, LightManager &lights, const FrameCamera &camera, int /*width*/,
                             int /*height*/, PerfStats &stats) {
    stats.forwardPass.begin();

    glClearColor(0.08f, 0.09f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    lights.uploadToGpu();
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, lights.lightBuffer());

    shader_->use();
    shader_->setMat4("view", camera.view);
    shader_->setMat4("projection", camera.projection);
    shader_->setVec3("uCameraPos", camera.eye);
    shader_->setVec3("uMaterialK", AppConfig::materialCoeffs());
    shader_->setInt("uLightCount", lights.activeCount());

    scene.drawFloor(*shader_);
    scene.drawSpotMeshes(*shader_);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);

    stats.frameStats().forwardPassMs = stats.forwardPass.endMs();
}
