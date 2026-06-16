#ifndef AA_TEST_SCENE_HPP
#define AA_TEST_SCENE_HPP

#include "AppConfig.hpp"
#include "Model.hpp"
#include "Shader.hpp"

#include <glm/glm.hpp>
#include <memory>
#include <vector>

struct SceneObject {
    glm::mat4 model;
};

// AA 专用测试场景：多实例 spot、棋盘地板、细四边形与网格线
class AATestScene {
public:
    bool init();    // 加载模型、生成 floor/thinQuads/gridLines 与 checker 纹理
    void shutdown();

    void drawOpaque(Shader &shader) const;  // spot 实例 + 地板 + 细四边形
    void drawLines(Shader &lineShader) const; // 网格/辅助线（无深度写入或 line 模式）

private:
    std::unique_ptr<Model> spot_;
    mutable Mesh floor_;
    mutable Mesh thinQuads_;
    mutable Mesh gridLines_;
    std::vector<SceneObject> spots_;
    GLuint checkerTexture_ = 0;
    GLuint lineVao_ = 0;
    GLuint lineVbo_ = 0;
    int lineVertexCount_ = 0;
};

#endif
