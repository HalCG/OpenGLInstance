#include "DepthPeelingApp.hpp"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

DepthPeelingApp *DepthPeelingApp::s_instance_ = nullptr;

// ---------------------------------------------------------------------------
// GLFW 回调（通过静态实例转发到成员状态）
// ---------------------------------------------------------------------------
void DepthPeelingApp::framebufferSizeCallback(GLFWwindow * /*window*/,
                                              int width, int height) {
  if (s_instance_) {
    s_instance_->width_ = static_cast<unsigned int>(width);
    s_instance_->height_ = static_cast<unsigned int>(height);
    s_instance_->camera_.setAspectFromViewport(
        s_instance_->width_, s_instance_->height_);
    glViewport(0, 0, width, height);
  }
}

void DepthPeelingApp::processInput(GLFWwindow *window) {
  if (!s_instance_) {
    return;
  }
  if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
    glfwSetWindowShouldClose(window, true);
  } else if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
    s_instance_->camera_.orbitAngleDeg += 1.0f;
  } else if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
    s_instance_->camera_.orbitAngleDeg -= 1.0f;
  }
}

bool DepthPeelingApp::init() {
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
  if (!initFramebuffers()) {
    return false;
  }

  glGenQueries(1, &queryId_);
  glViewport(0, 0, static_cast<GLsizei>(width_), static_cast<GLsizei>(height_));
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  return true;
}

bool DepthPeelingApp::initWindow() {
  if (!glfwInit()) {
    std::cout << "Failed to initialize GLFW" << std::endl;
    return false;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_SAMPLES, 4);

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
    glfwDestroyWindow(window_);
    window_ = nullptr;
    glfwTerminate();
    return false;
  }

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glEnable(GL_MULTISAMPLE);

  camera_.orbitAngleDeg = AppConfig::kInitialOrbitAngle;
  camera_.setAspectFromViewport(width_, height_);
  return true;
}

bool DepthPeelingApp::initShaders() {
  const std::string root = AppConfig::kResourceRoot;
  shaderInit_ = std::make_unique<Shader>(
      (root + "depth_peeling_init.vert").c_str(),
      (root + "depth_peeling_init.frag").c_str());
  shaderPeel_ = std::make_unique<Shader>(
      (root + "depth_peeling_render.vert").c_str(),
      (root + "depth_peeling_render.frag").c_str());
  shaderBlend_ = std::make_unique<Shader>(
      (root + "depth_peeling_blend.vert").c_str(),
      (root + "depth_peeling_blend.frag").c_str());
  shaderFinal_ = std::make_unique<Shader>(
      (root + "depth_peeling_final.vert").c_str(),
      (root + "depth_peeling_final.frag").c_str());
  shaderQuad_ = std::make_unique<Shader>((root + "quad.vert").c_str(),
                                          (root + "quad.frag").c_str());
  return true;
}

bool DepthPeelingApp::initScene() {
  const std::string root = AppConfig::kResourceRoot;
  modelQuad_ = std::make_unique<Model>(root + "models/quad/quad.obj");
  modelSpot_ = std::make_unique<Model>(root + "models/spot/spot.obj");

  texWindowR_ = std::make_unique<Texture>(root + "models/quad/window-r.png");
  texWindowG_ = std::make_unique<Texture>(root + "models/quad/window-g.png");
  texWindowB_ = std::make_unique<Texture>(root + "models/quad/window-b.png");
  texSpot_ = std::make_unique<Texture>(root + "models/spot/spot.png");
  return true;
}

bool DepthPeelingApp::initFramebuffers() {
  const int w = static_cast<int>(width_);
  const int h = static_cast<int>(height_);

  fboAccum_.create(w, h, "Accumulation (FBO_0)");
  fboPeel_.create(w, h, "Peel layer (FBO_1)");

  texOitColor_.createColorHDR(w, h);
  glGenFramebuffers(1, &fboOit_);
  glBindFramebuffer(GL_FRAMEBUFFER, fboOit_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         texOitColor_.id, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                         fboAccum_.depth.id, 0);
  const GLenum bufs[] = {GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT};
  glDrawBuffers(2, bufs);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::cout << "ERROR::FRAMEBUFFER:: OIT framebuffer is not complete!"
              << std::endl;
  }
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return true;
}

void DepthPeelingApp::run() {
  if (!window_) {
    return;
  }

  while (!glfwWindowShouldClose(window_)) {
    beginFrame();
    initPeelBuffers();
    peelAndBlend();
    compositeToScreen();
    endFrame();
  }
}

void DepthPeelingApp::shutdown() {
  if (queryId_ != 0) {
    glDeleteQueries(1, &queryId_);
    queryId_ = 0;
  }
  if (fboOit_ != 0) {
    glDeleteFramebuffers(1, &fboOit_);
    fboOit_ = 0;
  }
  texOitColor_.destroy();
  fboAccum_.destroy();
  fboPeel_.destroy();
  if (window_) {
    glfwDestroyWindow(window_);
    window_ = nullptr;
  }
  glfwTerminate();
  s_instance_ = nullptr;
}

void DepthPeelingApp::beginFrame() {
  processInput(window_);
}

void DepthPeelingApp::initPeelBuffers() {
  // FBO_0：累积颜色 + 深度（深度初值 0，供后续剥离比较）
  fboAccum_.bindColorDepth(fboAccum_.color.id, fboAccum_.depth.id);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClearDepth(0.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // FBO_1：当前剥离层
  fboPeel_.bindColorDepth(fboPeel_.color.id, fboPeel_.depth.id);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClearDepth(0.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  glEnable(GL_DEPTH_TEST);  //开启深度测试；片元是否写入颜色/深度，要先和深度缓冲比较；用深度测试保证每个像素只保留当前层里最近的那一片
  glDepthFunc(GL_LESS);     //深度测试函数，小于当前深度值的片元会被丢弃
  glDepthMask(GL_TRUE);     //通过测试的片元会 写入 深度缓冲（更新本层深度纹理）

  inputDepthIndex_ = 0;
  outputDepthIndex_ = 1;
}

GLuint DepthPeelingApp::depthTexture(int index) const {
  return index ? fboPeel_.depth.id : fboAccum_.depth.id;
}

glm::mat4 DepthPeelingApp::modelMatrix(const glm::vec3 &translation,
                                       float scale) const {
  glm::mat4 model(1.0f);
  model = glm::translate(model, translation);
  model = glm::rotate(model, glm::radians(0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
  model = glm::rotate(model, glm::radians(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
  model = glm::rotate(model, glm::radians(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
  return glm::scale(model, glm::vec3(scale));
}

void DepthPeelingApp::drawSceneLayer(Shader &shader, GLuint targetFbo,
                                     int inputDepthIndex,
                                     int outputDepthIndex) {
  const glm::mat4 view = camera_.view();
  const glm::mat4 projection = camera_.projection();
  const GLuint inputDepth = depthTexture(inputDepthIndex);

  shader.use();
  shader.setVec3("cameraPos", cameraPos_);
  shader.setVec3("lightPos", lightPos_);
  shader.setVec3("k", lightCoeffs_);
  shader.setVec2("u_ScreenSize",
                 glm::vec2(static_cast<float>(width_),
                           static_cast<float>(height_)));

  auto draw = [&](Model &model, const glm::vec3 &pos, GLuint diffuseId) {
    shader.setMat4("model", modelMatrix(pos));
    shader.setMat4("view", view);
    shader.setMat4("projection", projection);
    model.Draw(shader, targetFbo,
               {{"texture_diffuse", diffuseId},
                {"texture_depth", inputDepth}},
               {}, GL_TRIANGLES, {false, false});
  };

  draw(*modelSpot_, glm::vec3(0.0f, 0.0f, 0.0f), texSpot_->id);
  draw(*modelQuad_, glm::vec3(-0.5f, 0.0f, 0.8f), texWindowR_->id);
  draw(*modelQuad_, glm::vec3(0.2f, -0.5f, -1.0f), texWindowG_->id);
  draw(*modelQuad_, glm::vec3(0.2f, 0.0f, -0.5f), texWindowB_->id);

  (void)outputDepthIndex;
}

GLuint DepthPeelingApp::waitSampleCount() {
  GLint available = 0;
  while (!available) {
    glGetQueryObjectiv(queryId_, GL_QUERY_RESULT_AVAILABLE, &available);
  }
  GLuint sampleCount = 0;
  glGetQueryObjectuiv(queryId_, GL_QUERY_RESULT, &sampleCount);
  std::cout << "Samples passed: " << sampleCount << std::endl;
  return sampleCount;
}

void DepthPeelingApp::peelAndBlend() {
  for (int layer = 0; layer < AppConfig::kMaxDepthPeelLayers; ++layer) {
    (void)layer;

    glBindFramebuffer(GL_FRAMEBUFFER, fboPeel_.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           depthTexture(outputDepthIndex_), 0);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClearDepth(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glBeginQuery(GL_SAMPLES_PASSED, queryId_);
    drawSceneLayer(*shaderPeel_, fboPeel_.fbo, inputDepthIndex_,
                   outputDepthIndex_);
    glEndQuery(GL_SAMPLES_PASSED);

    const GLuint sampleCount = waitSampleCount();

    shaderBlend_->use();
    glEnable(GL_BLEND);           //开启混合；将当前层颜色 front-to-back 混合到累积缓冲
    glDepthMask(GL_FALSE);        //禁用深度写入；当前层颜色混合到累积缓冲时，不更新深度缓冲
    glDisable(GL_DEPTH_TEST);     //禁用深度测试；当前层颜色混合到累积缓冲时，不进行深度测试
    glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE, GL_ZERO, //混合函数；将当前层颜色混合到累积缓冲
                        GL_ONE_MINUS_SRC_ALPHA);

    modelQuad_->Draw(*shaderBlend_, fboAccum_.fbo,
                    {{"texture_diffuse", fboPeel_.color.id}}, {},
                    GL_TRIANGLES, {false, true});

    inputDepthIndex_ = (inputDepthIndex_ + 1) % 2;
    outputDepthIndex_ = (outputDepthIndex_ + 1) % 2;

    glDisable(GL_BLEND);        //关闭混合；将当前层颜色混合到累积缓冲时，不进行深度测试
    glDepthMask(GL_TRUE);       //开启深度写入；当前层颜色混合到累积缓冲时，更新深度缓冲
    glEnable(GL_DEPTH_TEST);    //开启深度测试；当前层颜色混合到累积缓冲时，进行深度测试

    if (sampleCount <= 0) {
      break;
    }
  }
}

void DepthPeelingApp::compositeToScreen() {
  shaderFinal_->use();
  shaderFinal_->setVec3("background_color", AppConfig::backgroundColor());
  modelQuad_->Draw(*shaderFinal_, 0,
                  {{"texture_diffuse", fboAccum_.color.id}}, {}, GL_TRIANGLES,
                  {true, true});
}

void DepthPeelingApp::endFrame() {
  glfwSwapBuffers(window_);
  glfwPollEvents();
}
