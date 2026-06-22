#ifndef LIGHT_MANAGER_HPP
#define LIGHT_MANAGER_HPP

#include "AppConfig.hpp"
#include "RenderTypes.hpp"

#include <glad/glad.h>
#include <vector>

// 点光源管理：SSBO 上传 + Forward+ tile 可见性裁剪
class LightManager {
public:
    void init();              // 创建 light/tile SSBO
    void shutdown();
    void regenerate(int activeCount); // 按数量随机生成点光源（位置/颜色/半径）

    void uploadToGpu() const; // 将 lights_ 写入 lightBuffer_
    void buildForwardPlusTiles(int screenWidth, int screenHeight, const FrameCamera &camera); // CPU 逐 tile 填 tileCount/index SSBO

    int activeCount() const { return activeCount_; }
    const std::vector<GpuPointLight> &lights() const { return lights_; }

    GLuint lightBuffer() const { return lightBuffer_; }
    GLuint tileCountBuffer() const { return tileCountBuffer_; }
    GLuint tileIndexBuffer() const { return tileIndexBuffer_; }
    int tilesX() const { return tilesX_; }
    int tilesY() const { return tilesY_; }

private:
    std::vector<GpuPointLight> lights_;
    int activeCount_ = 256;
    int tilesX_ = 0;
    int tilesY_ = 0;

    GLuint lightBuffer_ = 0;
    GLuint tileCountBuffer_ = 0;
    GLuint tileIndexBuffer_ = 0;
};

#endif
