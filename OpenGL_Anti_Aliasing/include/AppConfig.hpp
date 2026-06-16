#ifndef APP_CONFIG_HPP
#define APP_CONFIG_HPP

#include <glm/glm.hpp>
#include <string>

namespace AppConfig {
constexpr unsigned int kInitialWidth = 1280;
constexpr unsigned int kInitialHeight = 720;

constexpr const char *kResourceRoot = "resources/";
constexpr const char *kWindowTitle = "OpenGL Anti-Aliasing Demo";

constexpr float kFovDegrees = 45.0f;
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 100.0f;
constexpr float kInitialOrbitAngle = 30.0f;
constexpr float kInitialOrbitPitch = 18.0f;
constexpr float kMinOrbitRadius = 2.5f;
constexpr float kMaxOrbitRadius = 18.0f;
constexpr float kMouseOrbitFullWidthDegrees = 100.0f;
constexpr float kPanFactor = 2.0f;
constexpr float kDollyFactor = 3.0f;
constexpr float kScrollZoomSensitivity = 0.8f;

constexpr float kSpotScale = 1.6f;
constexpr float kCameraOrbitRadius = 5.5f;
constexpr float kCameraTargetY = 1.0f;
constexpr int kSpotInstanceCount = 4;

constexpr int kMsaaSamplesPresets[] = {2, 4};
constexpr int kMsaaPresetCount = 2;
constexpr int kDefaultMsaaPresetIndex = 1;

static const glm::vec3 &materialCoeffs() {
    static const glm::vec3 v(0.2f, 0.8f, 0.4f);
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
