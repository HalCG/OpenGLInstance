#include "SceneRenderer.hpp"

#include "AppConfig.hpp"

bool SceneRenderer::init() {
    sceneShader_ = std::make_unique<Shader>(AppConfig::shaderPath("scene.vert").c_str(),
                                            AppConfig::shaderPath("scene.frag").c_str());
    lineShader_ = std::make_unique<Shader>(AppConfig::shaderPath("line.vert").c_str(),
                                             AppConfig::shaderPath("line.frag").c_str());
    return sceneShader_ && lineShader_ && sceneShader_->ID != 0;
}

void SceneRenderer::shutdown() {
    sceneShader_.reset();
    lineShader_.reset();
}

void SceneRenderer::render(AATestScene &scene, const FrameCamera &camera) {
    sceneShader_->use();
    sceneShader_->setMat4("view", camera.view);
    sceneShader_->setMat4("projection", camera.projection);
    sceneShader_->setVec3("uCameraPos", camera.eye);
    sceneShader_->setVec3("uMaterialK", AppConfig::materialCoeffs());
    sceneShader_->setVec3("uLightPos0", glm::vec3(4.0f, 6.0f, 2.0f));
    sceneShader_->setVec3("uLightPos1", glm::vec3(-3.0f, 5.0f, -4.0f));
    sceneShader_->setVec3("uLightColor0", glm::vec3(1.0f, 0.95f, 0.9f));
    sceneShader_->setVec3("uLightColor1", glm::vec3(0.7f, 0.8f, 1.0f));
    scene.drawOpaque(*sceneShader_);

    lineShader_->use();
    lineShader_->setMat4("view", camera.view);
    lineShader_->setMat4("projection", camera.projection);
    scene.drawLines(*lineShader_);
}
