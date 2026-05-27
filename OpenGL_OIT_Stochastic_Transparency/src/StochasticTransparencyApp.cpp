#include "StochasticTransparencyApp.hpp"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

StochasticTransparencyApp *StochasticTransparencyApp::s_instance_ = nullptr;

// ---------------------------------------------------------------------------
// GLFW 回调（通过静态实例转发到成员状态）
// ---------------------------------------------------------------------------
void StochasticTransparencyApp::framebufferSizeCallback(GLFWwindow * /*window*/,
                                                        int width,
                                                        int height) {
  if (s_instance_) {
    s_instance_->width_ = static_cast<unsigned int>(width);
    s_instance_->height_ = static_cast<unsigned int>(height);
    glViewport(0, 0, width, height);
  }
}

void StochasticTransparencyApp::processInput(GLFWwindow *window) {
  if (!s_instance_) {
    return;
  }
  if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
    glfwSetWindowShouldClose(window, true);
  } else if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
    s_instance_->viewRotate_ -= 1.0f;
  } else if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
    s_instance_->viewRotate_ += 1.0f;
  }
}

bool StochasticTransparencyApp::init() {
  s_instance_ = this;
  if (!initWindow()) {
    return false;
  }
  if (!initShaders()) {
    return false;
  }
  if (!initScene()) {
    return false;
  }
  if (!initBuffers()) {
    return false;
  }

  glViewport(0, 0, static_cast<GLsizei>(width_),
             static_cast<GLsizei>(height_));
  glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
  glEnable(GL_MULTISAMPLE);
  glEnable(GL_SAMPLE_MASK);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_TRUE);

  return true;
}

bool StochasticTransparencyApp::initWindow() {
  glfwInit();
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_SAMPLES, 16);

  window_ = glfwCreateWindow(static_cast<int>(width_),
                             static_cast<int>(height_),
                             AppConfig::kWindowTitle, nullptr, nullptr);
  if (!window_) {
    std::cout << "Failed to create GLFW window" << std::endl;
    glfwTerminate();
    return false;
  }

  glfwMakeContextCurrent(window_);
  glfwSetFramebufferSizeCallback(window_, framebufferSizeCallback);

  if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
    std::cout << "Failed to initialize GLAD" << std::endl;
    return false;
  }

  // 获取最大的 samples 个数
  GLint maxSamples;
  glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
  std::cout << "Max supported MSAA samples: " << maxSamples << std::endl;

  return true;
}

bool StochasticTransparencyApp::initShaders() {
  const std::string root = AppConfig::kResourceRoot;
  try {
    shaderQuad_ = std::make_unique<Shader>(
        (root + "quad.vert").c_str(),
        (root + "quad.frag").c_str());
    return true;
  } catch (const std::exception &e) {
    std::cout << "Failed to load shaders: " << e.what() << std::endl;
    return false;
  }
}

bool StochasticTransparencyApp::initScene() {
  try {
    modelQuad_ = std::make_unique<Model>("./resources/models/quad/quad.obj");
    modelSpot_ = std::make_unique<Model>("./resources/models/spot/spot.obj");

    texWindowR_ =
        std::make_unique<Texture>("./resources/models/quad/window-r.png");
    texWindowG_ =
        std::make_unique<Texture>("./resources/models/quad/window-g.png");
    texWindowB_ =
        std::make_unique<Texture>("./resources/models/quad/window-b.png");
    texSpot_ = std::make_unique<Texture>("./resources/models/spot/spot.png");

    return true;
  } catch (const std::exception &e) {
    std::cout << "Failed to load scene assets: " << e.what() << std::endl;
    return false;
  }
}

bool StochasticTransparencyApp::initBuffers() {
  // 可在此初始化 VAO/VBO，目前暂不需要（由 Model 类管理）
  return true;
}

void StochasticTransparencyApp::beginFrame() {
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
  glClearDepth(1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

glm::mat4 StochasticTransparencyApp::modelMatrix(const glm::vec3 &translation,
                                                 float scale) const {
  glm::mat4 model = glm::mat4(1.0f);
  model = glm::translate(model, translation);
  model = glm::scale(model, glm::vec3(scale, scale, scale));
  return model;
}

void StochasticTransparencyApp::renderScene() {
  shaderQuad_->use();

  // 计算相机矩阵
  glm::mat4 cameraView = glm::lookAt(
      2.0f * glm::vec3(glm::sin(glm::radians(viewRotate_)), 0.0f,
                       glm::cos(glm::radians(viewRotate_))),
      glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

  glm::mat4 cameraProjection =
      glm::perspective(glm::radians(AppConfig::kFovDegrees),
                       (float)width_ / (float)height_,
                       AppConfig::kNearPlane, AppConfig::kFarPlane);

  shaderQuad_->setMat4("view", cameraView);
  shaderQuad_->setMat4("projection", cameraProjection);

  GLint maxSamples;
  glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);

  static int frameID = 0;
  int modelCnt = 4;

  // 渲染 spot
  shaderQuad_->setMat4("model", modelMatrix(glm::vec3(0.0f, 0.0f, 0.0f)));
  shaderQuad_->setInt("sampleCnt", maxSamples);
  shaderQuad_->setInt("frameID", (frameID++) % modelCnt);
  modelSpot_->Draw(*shaderQuad_, 0,
                   {{"texture_diffuse", texSpot_->id}}, {},
                   GL_TRIANGLES, {false, false});

  // 渲染 blue window
  shaderQuad_->setMat4("model",
                        modelMatrix(glm::vec3(0.3f, -0.1f, -0.8f), 0.5f));
  shaderQuad_->setInt("sampleCnt", maxSamples);
  shaderQuad_->setInt("frameID", (frameID++) % modelCnt);
  modelQuad_->Draw(*shaderQuad_, 0,
                   {{"texture_diffuse", texWindowB_->id}}, {},
                   GL_TRIANGLES, {false, false});

  // 渲染 green window
  shaderQuad_->setMat4("model",
                        modelMatrix(glm::vec3(0.6f, 0.6f, -0.6f), 0.5f));
  shaderQuad_->setInt("sampleCnt", maxSamples);
  shaderQuad_->setInt("frameID", (frameID++) % modelCnt);
  modelQuad_->Draw(*shaderQuad_, 0,
                   {{"texture_diffuse", texWindowG_->id}}, {},
                   GL_TRIANGLES, {false, false});

  // 渲染 red window
  shaderQuad_->setMat4("model",
                        modelMatrix(glm::vec3(0.0f, 0.0f, 0.0f), 0.5f));
  shaderQuad_->setInt("sampleCnt", maxSamples);
  shaderQuad_->setInt("frameID", (frameID++) % modelCnt);
  modelQuad_->Draw(*shaderQuad_, 0,
                   {{"texture_diffuse", texWindowR_->id}}, {},
                   GL_TRIANGLES, {false, false});
}

void StochasticTransparencyApp::endFrame() {
  glfwSwapBuffers(window_);
  glfwPollEvents();
}

void StochasticTransparencyApp::run() {
  while (!glfwWindowShouldClose(window_)) {
    processInput(window_);
    beginFrame();
    renderScene();
    endFrame();
  }
}

void StochasticTransparencyApp::shutdown() {
  shaderQuad_.reset();
  modelQuad_.reset();
  modelSpot_.reset();
  texWindowR_.reset();
  texWindowG_.reset();
  texWindowB_.reset();
  texSpot_.reset();

  if (window_) {
    glfwDestroyWindow(window_);
  }
  glfwTerminate();
}
