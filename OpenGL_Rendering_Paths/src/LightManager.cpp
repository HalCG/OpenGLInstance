#include "LightManager.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <random>

void LightManager::init() {
    lights_.resize(AppConfig::kMaxLights);
    regenerate(activeCount_);

    glGenBuffers(1, &lightBuffer_);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, lightBuffer_);
    glBufferData(GL_SHADER_STORAGE_BUFFER, lights_.size() * sizeof(GpuPointLight), lights_.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    glGenBuffers(1, &tileCountBuffer_);
    glGenBuffers(1, &tileIndexBuffer_);
}

void LightManager::shutdown() {
    if (lightBuffer_) {
        glDeleteBuffers(1, &lightBuffer_);
        lightBuffer_ = 0;
    }
    if (tileCountBuffer_) {
        glDeleteBuffers(1, &tileCountBuffer_);
        tileCountBuffer_ = 0;
    }
    if (tileIndexBuffer_) {
        glDeleteBuffers(1, &tileIndexBuffer_);
        tileIndexBuffer_ = 0;
    }
}

// 固定种子随机生成 activeCount_ 个点光源
void LightManager::regenerate(int activeCount) {
    activeCount_ = std::max(1, std::min(activeCount, AppConfig::kMaxLights));

    std::mt19937 rng(1337);
    std::uniform_real_distribution<float> posX(-4.0f, 4.0f);
    std::uniform_real_distribution<float> posY(0.5f, 3.5f);
    std::uniform_real_distribution<float> posZ(-4.0f, 4.0f);
    std::uniform_real_distribution<float> hue(0.0f, 1.0f);
    std::uniform_real_distribution<float> intensity(0.6f, 1.4f);

    for (int i = 0; i < AppConfig::kMaxLights; ++i) {
        GpuPointLight light{};
        if (i < activeCount_) {
            light.positionRadius = glm::vec4(posX(rng), posY(rng), posZ(rng), 3.5f);
            const float h = hue(rng);
            // 生成彩虹色
            const glm::vec3 color = glm::abs(glm::vec3(h * 6.0f + 0.0f, h * 6.0f + 2.0f, h * 6.0f + 4.0f) -
                                               glm::vec3(3.0f));
            light.colorIntensity = glm::vec4(color, intensity(rng));
        } else {
            light.positionRadius = glm::vec4(0.0f);
            light.colorIntensity = glm::vec4(0.0f);
        }
        lights_[static_cast<size_t>(i)] = light;
    }
}

void LightManager::uploadToGpu() const {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, lightBuffer_);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, lights_.size() * sizeof(GpuPointLight), lights_.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

// CPU 逐光源：投影 AABB 到屏幕 tile，写入 tileCount/tileIndex SSBO
void LightManager::buildForwardPlusTiles(int screenWidth, int screenHeight, const FrameCamera &camera) {
    tilesX_ = (screenWidth + AppConfig::kTileSize - 1) / AppConfig::kTileSize;
    tilesY_ = (screenHeight + AppConfig::kTileSize - 1) / AppConfig::kTileSize;
    const int tileCount = tilesX_ * tilesY_;

    std::vector<uint32_t> counts(static_cast<size_t>(tileCount), 0);
    std::vector<uint32_t> indices(static_cast<size_t>(tileCount) * AppConfig::kMaxLightsPerTile, 0xFFFFFFFFu);

    const glm::mat4 viewProj = camera.projection * camera.view;

    for (int i = 0; i < activeCount_; ++i) {
        const glm::vec3 center = glm::vec3(lights_[static_cast<size_t>(i)].positionRadius);
        const float radius = lights_[static_cast<size_t>(i)].positionRadius.w;

        glm::vec3 corners[8];
        for (int x = 0; x < 2; ++x) {
            for (int y = 0; y < 2; ++y) {
                for (int z = 0; z < 2; ++z) {
                    const glm::vec3 offset((x ? 1.0f : -1.0f) * radius, (y ? 1.0f : -1.0f) * radius,
                                           (z ? 1.0f : -1.0f) * radius);
                    corners[x * 4 + y * 2 + z] = center + offset;
                }
            }
        }

        float minX = static_cast<float>(screenWidth);
        float minY = static_cast<float>(screenHeight);
        float maxX = 0.0f;
        float maxY = 0.0f;
        bool anyInFront = false;

        for (const glm::vec3 &world : corners) {
            glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
            if (clip.w <= 0.0f) {
                continue;
            }
            anyInFront = true;
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            const float sx = (ndc.x * 0.5f + 0.5f) * screenWidth;
            const float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * screenHeight;
            minX = std::min(minX, sx);
            minY = std::min(minY, sy);
            maxX = std::max(maxX, sx);
            maxY = std::max(maxY, sy);
        }

        if (!anyInFront) {
            continue;
        }

        minX = glm::clamp(minX, 0.0f, static_cast<float>(screenWidth - 1));
        maxX = glm::clamp(maxX, 0.0f, static_cast<float>(screenWidth - 1));
        minY = glm::clamp(minY, 0.0f, static_cast<float>(screenHeight - 1));
        maxY = glm::clamp(maxY, 0.0f, static_cast<float>(screenHeight - 1));

        const int tileMinX = static_cast<int>(minX) / AppConfig::kTileSize;
        const int tileMaxX = static_cast<int>(maxX) / AppConfig::kTileSize;
        const int tileMinY = static_cast<int>(minY) / AppConfig::kTileSize;
        const int tileMaxY = static_cast<int>(maxY) / AppConfig::kTileSize;

        for (int ty = tileMinY; ty <= tileMaxY; ++ty) {
            for (int tx = tileMinX; tx <= tileMaxX; ++tx) {
                if (tx < 0 || ty < 0 || tx >= tilesX_ || ty >= tilesY_) {
                    continue;
                }
                const int tileIndex = ty * tilesX_ + tx;
                uint32_t &count = counts[static_cast<size_t>(tileIndex)];
                if (count >= static_cast<uint32_t>(AppConfig::kMaxLightsPerTile)) {
                    continue;
                }
                indices[static_cast<size_t>(tileIndex) * AppConfig::kMaxLightsPerTile + count] =
                    static_cast<uint32_t>(i);
                ++count;
            }
        }
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, tileCountBuffer_);
    glBufferData(GL_SHADER_STORAGE_BUFFER, counts.size() * sizeof(uint32_t), counts.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, tileIndexBuffer_);
    glBufferData(GL_SHADER_STORAGE_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}
