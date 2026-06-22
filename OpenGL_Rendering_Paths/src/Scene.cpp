#include "Scene.hpp"

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

void forceDiffuseTexture(Model &model, const std::string &relativePath) {
    const std::string fullPath = AppConfig::resourcePath(relativePath);
    const GLuint texId = loadModelTexture(relativePath);
    if (texId == 0) {
        std::cerr << "Failed to assign model texture: " << fullPath << std::endl;
        return;
    }

    Texture tex;
    tex.id = texId;
    tex.type = "texture_diffuse";
    tex.path = relativePath;
    for (Mesh &mesh : model.meshes) {
        mesh.textures.clear();
        mesh.textures.push_back(tex);
    }
}

GLuint createWhiteTexture() {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    const unsigned char white[] = {255, 255, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
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

Mesh buildFloorMesh(GLuint whiteTexture) {
    std::vector<Vertex> vertices = {
        makeVertex(-8.0f, 0.0f, -8.0f, 0.0f, 0.0f),
        makeVertex(8.0f, 0.0f, -8.0f, 8.0f, 0.0f),
        makeVertex(8.0f, 0.0f, 8.0f, 8.0f, 8.0f),
        makeVertex(-8.0f, 0.0f, 8.0f, 0.0f, 8.0f),
    };
    std::vector<unsigned int> indices = {0, 1, 2, 0, 2, 3};
    Texture floorTex;
    floorTex.id = whiteTexture;
    floorTex.type = "texture_diffuse";
    return Mesh(vertices, indices, {floorTex});
}
} // namespace

// 加载 spot 模型、棋盘地板、布置场景实例
bool Scene::init() {
    const GLuint whiteTexture = createWhiteTexture();
    spot_ = std::make_unique<Model>(AppConfig::resourcePath("models/spot/spot.obj"));
    forceDiffuseTexture(*spot_, "models/spot/spot.png");
    floor_ = buildFloorMesh(whiteTexture);

    const int grid = static_cast<int>(std::sqrt(AppConfig::kSpotInstanceCount));
    objects_.clear();
    for (int z = 0; z < grid; ++z) {
        for (int x = 0; x < grid; ++x) {
            if (static_cast<int>(objects_.size()) >= AppConfig::kSpotInstanceCount) {
                break;
            }
            SceneObject obj;
            obj.model = glm::mat4(1.0f);
            obj.model = glm::translate(obj.model,
                                       glm::vec3((x - grid / 2) * AppConfig::kSpotGridSpacing, 0.0f,
                                                 (z - grid / 2) * AppConfig::kSpotGridSpacing));
            obj.model = glm::scale(obj.model, glm::vec3(AppConfig::kSpotScale));
            objects_.push_back(obj);
        }
    }
    return spot_ != nullptr;
}

void Scene::shutdown() {
    spot_.reset();
}

void Scene::drawSpotMeshes(Shader &shader) const {
    for (const SceneObject &obj : objects_) {
        shader.setMat4("model", obj.model);
        spot_->Draw(shader);
    }
}

void Scene::drawFloor(Shader &shader) const {
    const glm::mat4 model = glm::mat4(1.0f);
    shader.setMat4("model", model);
    floor_.Draw(shader);
}
