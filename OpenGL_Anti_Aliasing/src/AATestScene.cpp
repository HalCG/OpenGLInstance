#include "AATestScene.hpp"

#include <glad/glad.h>
#include <stb_image.h>
#include <cmath>
#include <iostream>

namespace {
GLuint loadModelTexture(const std::string &relativePath) {
    const std::string fullPath = AppConfig::resourcePath(relativePath);
    GLuint tex = 0;
    glGenTextures(1, &tex);
    int width = 0;
    int height = 0;
    int components = 0;
    unsigned char *data = stbi_load(fullPath.c_str(), &width, &height, &components, 0);
    if (!data) {
        std::cerr << "Failed to load texture: " << fullPath << std::endl;
        glDeleteTextures(1, &tex);
        return 0;
    }

    GLenum format = GL_RGB;
    if (components == 1) {
        format = GL_RED;
    } else if (components == 4) {
        format = GL_RGBA;
    }

    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    return tex;
}

void applyDefaultDiffuseTexture(Model &model, const std::string &relativePath) {
    for (const Mesh &mesh : model.meshes) {
        for (const Texture &tex : mesh.textures) {
            if (tex.type == "texture_diffuse" && tex.id != 0) {
                return;
            }
        }
    }

    const GLuint texId = loadModelTexture(relativePath);
    if (texId == 0) {
        return;
    }

    Texture tex;
    tex.id = texId;
    tex.type = "texture_diffuse";
    tex.path = relativePath;
    for (Mesh &mesh : model.meshes) {
        mesh.textures.push_back(tex);
    }
}
GLuint createSolidTexture(unsigned char r, unsigned char g, unsigned char b) {
    const unsigned char pixel[3] = {r, g, b};
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, pixel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

GLuint createCheckerTexture(int size = 256, int cells = 16) {
    std::vector<unsigned char> pixels(static_cast<size_t>(size * size * 3));
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool dark = ((x / (size / cells)) + (y / (size / cells))) % 2 == 0;
            const unsigned char c = dark ? 40 : 210;
            const size_t idx = static_cast<size_t>((y * size + x) * 3);
            pixels[idx + 0] = c;
            pixels[idx + 1] = c;
            pixels[idx + 2] = c;
        }
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

Vertex makeVertex(float x, float y, float z, float u, float v) {
    Vertex ver{};
    ver.Position = glm::vec3(x, y, z);
    ver.Normal = glm::vec3(0.0f, 1.0f, 0.0f);
    ver.TexCoords = glm::vec2(u, v);
    return ver;
}

Mesh buildFloorMesh(GLuint checkerTexture) {
    std::vector<Vertex> vertices = {
        makeVertex(-10.0f, 0.0f, -10.0f, 0.0f, 0.0f),
        makeVertex(10.0f, 0.0f, -10.0f, 10.0f, 0.0f),
        makeVertex(10.0f, 0.0f, 10.0f, 10.0f, 10.0f),
        makeVertex(-10.0f, 0.0f, 10.0f, 0.0f, 10.0f),
    };
    std::vector<unsigned int> indices = {0, 1, 2, 0, 2, 3};
    Texture tex;
    tex.id = checkerTexture;
    tex.type = "texture_diffuse";
    return Mesh(vertices, indices, {tex});
}

Mesh buildThinQuadMesh() {
    std::vector<Vertex> vertices = {
        makeVertex(-0.06f, 0.0f, -1.5f, 0.0f, 0.0f),
        makeVertex(0.06f, 0.0f, -1.5f, 1.0f, 0.0f),
        makeVertex(0.06f, 3.5f, -1.5f, 1.0f, 1.0f),
        makeVertex(-0.06f, 3.5f, -1.5f, 0.0f, 1.0f),
    };
    for (auto &v : vertices) {
        v.Normal = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    std::vector<unsigned int> indices = {0, 1, 2, 0, 2, 3};
    Texture tex;
    tex.id = 0;
    tex.type = "texture_diffuse";
    return Mesh(vertices, indices, {tex});
}
} // namespace

// 加载 spot、生成地板/细线几何、布置多实例（锯齿测试用）
bool AATestScene::init() {
    checkerTexture_ = createCheckerTexture();
    const GLuint whiteTexture = createSolidTexture(240, 240, 240);
    spot_ = std::make_unique<Model>(AppConfig::resourcePath("models/spot/spot.obj"));
    applyDefaultDiffuseTexture(*spot_, "models/spot/spot.png");
    floor_ = buildFloorMesh(checkerTexture_);
    thinQuads_ = buildThinQuadMesh();
    {
        Texture tex;
        tex.id = whiteTexture;
        tex.type = "texture_diffuse";
        thinQuads_.textures = {tex};
    }

    const glm::vec3 positions[] = {
        {-2.0f, 0.0f, -1.5f},
        {0.0f, 0.0f, 0.0f},
        {2.0f, 0.0f, 1.5f},
        {-1.0f, 0.0f, 2.0f},
    };

    spots_.clear();
    for (const glm::vec3 &pos : positions) {
        SceneObject obj;
        obj.model = glm::translate(glm::mat4(1.0f), pos);
        obj.model = glm::scale(obj.model, glm::vec3(AppConfig::kSpotScale));
        spots_.push_back(obj);
    }

    std::vector<float> lineVerts;
    const float extent = 8.0f;
    const float step = 0.5f;
    for (float x = -extent; x <= extent + 0.001f; x += step) {
        lineVerts.push_back(x);
        lineVerts.push_back(0.01f);
        lineVerts.push_back(-extent);
        lineVerts.push_back(x);
        lineVerts.push_back(0.01f);
        lineVerts.push_back(extent);
    }
    for (float z = -extent; z <= extent + 0.001f; z += step) {
        lineVerts.push_back(-extent);
        lineVerts.push_back(0.01f);
        lineVerts.push_back(z);
        lineVerts.push_back(extent);
        lineVerts.push_back(0.01f);
        lineVerts.push_back(z);
    }

    lineVertexCount_ = static_cast<int>(lineVerts.size() / 3);
    glGenVertexArrays(1, &lineVao_);
    glGenBuffers(1, &lineVbo_);
    glBindVertexArray(lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    glBufferData(GL_ARRAY_BUFFER, lineVerts.size() * sizeof(float), lineVerts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glBindVertexArray(0);

    return spot_ != nullptr;
}

void AATestScene::shutdown() {
    if (lineVbo_) {
        glDeleteBuffers(1, &lineVbo_);
        lineVbo_ = 0;
    }
    if (lineVao_) {
        glDeleteVertexArrays(1, &lineVao_);
        lineVao_ = 0;
    }
    if (checkerTexture_) {
        glDeleteTextures(1, &checkerTexture_);
        checkerTexture_ = 0;
    }
    spot_.reset();
}

// 绘制不透明物体：地板 + 细四边形 + spot 实例
void AATestScene::drawOpaque(Shader &shader) const {
    glm::mat4 floorModel = glm::mat4(1.0f);
    shader.setMat4("model", floorModel);
    floor_.Draw(shader);

    const glm::vec3 railPositions[] = {{-3.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 3.0f}, {3.0f, 0.0f, -3.0f}};
    for (const glm::vec3 &pos : railPositions) {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), pos);
        model = glm::rotate(model, glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        shader.setMat4("model", model);
        thinQuads_.Draw(shader);
    }

    for (const SceneObject &obj : spots_) {
        shader.setMat4("model", obj.model);
        spot_->Draw(shader);
    }
}

// 绘制网格/辅助线（GL_LINES，易暴露无 AA 时的锯齿）
void AATestScene::drawLines(Shader &lineShader) const {
    lineShader.setMat4("model", glm::mat4(1.0f));
    lineShader.setVec3("uLineColor", glm::vec3(0.05f, 0.05f, 0.05f));
    glBindVertexArray(lineVao_);
    glDrawArrays(GL_LINES, 0, lineVertexCount_);
    glBindVertexArray(0);
}
