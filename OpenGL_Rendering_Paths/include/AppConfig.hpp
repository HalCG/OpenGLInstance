#ifndef APP_CONFIG_HPP
#define APP_CONFIG_HPP

#include <glm/glm.hpp>
#include <string>

namespace AppConfig {
constexpr unsigned int kInitialWidth = 1280;
constexpr unsigned int kInitialHeight = 720;

constexpr const char *kResourceRoot = "resources/";
constexpr const char *kWindowTitle = "OpenGL Rendering Paths Demo";

constexpr float kFovDegrees = 45.0f;
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 100.0f;
constexpr float kInitialOrbitAngle = 45.0f;
constexpr float kInitialOrbitPitch = 22.0f;
constexpr float kMinOrbitRadius = 3.0f;
constexpr float kMaxOrbitRadius = 24.0f;
constexpr float kMouseOrbitFullWidthDegrees = 100.0f;
constexpr float kPanFactor = 2.0f;
constexpr float kDollyFactor = 3.0f;
constexpr float kScrollZoomSensitivity = 0.9f;

constexpr float kSpotScale = 1.2f;
constexpr float kSpotGridSpacing = 3.8f;
constexpr float kCameraOrbitRadius = 7.0f;
constexpr float kCameraHeight = 3.5f;
constexpr float kCameraTargetY = 0.8f;

constexpr int kMaxLights = 512;
constexpr int kTileSize = 16;
constexpr int kMaxLightsPerTile = 64;
constexpr int kSpotInstanceCount = 12;

constexpr int kLightCountPresets[] = {64, 128, 256, 512};
constexpr int kLightPresetCount = 4;

static const glm::vec3 &cameraPosition() {
    static const glm::vec3 v(0.0f, 4.0f, 12.0f);
    return v;
}

static const glm::vec3 &materialCoeffs() {
    static const glm::vec3 v(0.15f, 0.75f, 0.35f);
    return v;
}

static std::string resourcePath(const std::string &relative) {
    return std::string(kResourceRoot) + relative;
}

static std::string shaderPath(const std::string &name) {
    return resourcePath("shaders/" + name);
}
} // namespace AppConfig

#endif
