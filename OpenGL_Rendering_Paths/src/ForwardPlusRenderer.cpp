#include "ForwardPlusRenderer.hpp"

#include "AppConfig.hpp"

bool ForwardPlusRenderer::init() {
    shader_ = std::make_unique<Shader>(AppConfig::shaderPath("mesh.vert").c_str(),
                                       AppConfig::shaderPath("forward_plus.frag").c_str());
    return shader_ && shader_->ID != 0;
}

void ForwardPlusRenderer::shutdown() {
    shader_.reset();
}

void ForwardPlusRenderer::render(const Scene &scene, LightManager &lights, const FrameCamera &camera, int width,
                                 int height, PerfStats &stats) {
    // Forward+ 两阶段：CPU tile culling → GPU 按 tile 读 SSBO 做 shading
    stats.cullPass.begin();
    lights.buildForwardPlusTiles(width, height, camera);
    stats.frameStats().cullPassMs = stats.cullPass.endMs();

    stats.shadingPass.begin();
    glClearColor(0.08f, 0.09f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    lights.uploadToGpu();
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, lights.lightBuffer());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, lights.tileCountBuffer());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, lights.tileIndexBuffer());

    shader_->use();
    shader_->setMat4("view", camera.view);
    shader_->setMat4("projection", camera.projection);
    shader_->setVec3("uCameraPos", camera.eye);
    shader_->setVec3("uMaterialK", AppConfig::materialCoeffs());
    shader_->setInt("uLightCount", lights.activeCount());
    shader_->setInt("uTilesX", lights.tilesX());
    shader_->setInt("uTilesY", lights.tilesY());
    shader_->setInt("uTileSize", AppConfig::kTileSize);
    shader_->setInt("uMaxLightsPerTile", AppConfig::kMaxLightsPerTile);

    scene.drawFloor(*shader_);
    scene.drawSpotMeshes(*shader_);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, 0);

    stats.frameStats().shadingPassMs = stats.shadingPass.endMs();
}
