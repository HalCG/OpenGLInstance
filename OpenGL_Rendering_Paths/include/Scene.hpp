#ifndef SCENE_HPP
#define SCENE_HPP

#include "AppConfig.hpp"
#include "Model.hpp"

#include <glm/glm.hpp>
#include <memory>
#include <vector>

struct SceneObject {
    glm::mat4 model;
};

// 渲染路径 Demo 共用场景：棋盘地板 + 多实例 spot 模型
class Scene {
public:
    bool init();    // 加载 spot 模型、生成 floor mesh、布置 instances
    void shutdown();

    void drawSpotMeshes(Shader &shader) const; // 遍历 objects_ 绘制 spot
    void drawFloor(Shader &shader) const;      // 绘制地板平面

    const std::vector<SceneObject> &objects() const { return objects_; }
    int objectCount() const { return static_cast<int>(objects_.size()) + 1; } // spot 数 + 地板

private:
    std::unique_ptr<Model> spot_;
    mutable Mesh floor_;
    std::vector<SceneObject> objects_;
};

#endif
