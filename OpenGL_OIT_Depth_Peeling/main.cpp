#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "Mesh.hpp"
#include "Model.hpp"
#include "Shader.hpp"
#include "Sphere.hpp"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/fwd.hpp"
#include "glm/trigonometric.hpp"
#include "stb_image.h"
#include "GLFW/glfw3.h"

void framebuffer_size_callback(GLFWwindow *window, int width, int height);

void processInput(GLFWwindow *window);

unsigned int SCR_WIDTH = 800;
unsigned int SCR_HEIGHT = 600;
unsigned int maxNodes = SCR_WIDTH * SCR_HEIGHT * 20;

float view_rotate = 45.0f;
// camera view matrix
glm::vec3 cameraPos = glm::vec3(0.0f, 0.0f, 2.0f);
glm::vec3 cameraLookAt = glm::vec3(0.0f, 0.0f, 0.0f);
glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);

// camera perspective matrix
float cameraZoom = 90.0f;
float cameraNearPlane = 0.1f; // near
float cameraFarPlane = 10.0f; // far
// light
glm::vec3 lightPos = glm::vec3(2.0f, 2.0f, 0.0f);
// k: ambient, diffues, specular
glm::vec3 k = glm::vec3(0.4f, 0.4f, 0.2f);

struct ListNode {
  glm::vec4 color;
  GLfloat depth;
  GLuint next;
};

int main() {

  glfwInit();
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  glfwWindowHint(GLFW_SAMPLES, 4);

  GLFWwindow *window =
      glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "LearnOpenGL", NULL, NULL);
  if (window == NULL) {
    std::cout << "Failed to create GLFW window" << std::endl;
    glfwTerminate();
    return -1;
  }

  glfwMakeContextCurrent(window);

  glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    std::cout << "Failed to initialize GLAD" << std::endl;
    return -1;
  }

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glEnable(GL_MULTISAMPLE);

  // 路径相对于 exe 所在目录；CMake 构建时会把 resources/ 复制到输出目录
  const char *res = "resources/";
  Shader depthPeelingInitShader((std::string(res) + "depth_peeling_init.vert").c_str(),
                                (std::string(res) + "depth_peeling_init.frag").c_str());

  Shader depthPeelingRenderShader((std::string(res) + "depth_peeling_render.vert").c_str(),
                                  (std::string(res) + "depth_peeling_render.frag").c_str());

  Shader depthPeelingBlendShader((std::string(res) + "depth_peeling_blend.vert").c_str(),
                                 (std::string(res) + "depth_peeling_blend.frag").c_str());

  Shader depthPeelingFinalShader((std::string(res) + "depth_peeling_final.vert").c_str(),
                                 (std::string(res) + "depth_peeling_final.frag").c_str());

  Shader quadShader((std::string(res) + "quad.vert").c_str(),
                    (std::string(res) + "quad.frag").c_str());

  Model quad = Model(std::string(res) + "models/quad/quad.obj");
  Model spot = Model(std::string(res) + "models/spot/spot.obj");

  Texture texture_window_r = Texture(std::string(res) + "models/quad/window-r.png");
  Texture texture_window_g = Texture(std::string(res) + "models/quad/window-g.png");
  Texture texture_window_b = Texture(std::string(res) + "models/quad/window-b.png");
  Texture texture_spot = Texture(std::string(res) + "models/spot/spot.png");

  // FBO_0
  unsigned int FBO_0;
  glGenFramebuffers(1, &FBO_0);

  // FBO_1
  unsigned int FBO_1;
  glGenFramebuffers(1, &FBO_1);
  // coolor_texture_0
  unsigned int color_texture_0;
  glGenTextures(1, &color_texture_0);
  glBindTexture(GL_TEXTURE_2D, color_texture_0);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGBA,
               GL_HALF_FLOAT, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  // texture_color_1
  unsigned int color_texture_1;
  glGenTextures(1, &color_texture_1);
  glBindTexture(GL_TEXTURE_2D, color_texture_1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGBA,
               GL_HALF_FLOAT, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  //
  unsigned int depth_texture_0;
  glGenTextures(1, &depth_texture_0);
  glBindTexture(GL_TEXTURE_2D, depth_texture_0);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, SCR_WIDTH, SCR_HEIGHT,
               0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);

  unsigned int depth_texture_1;
  glGenTextures(1, &depth_texture_1);
  glBindTexture(GL_TEXTURE_2D, depth_texture_1);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, SCR_WIDTH, SCR_HEIGHT,
               0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);

  glBindFramebuffer(GL_FRAMEBUFFER, FBO_0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         color_texture_0, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                         depth_texture_0, 0);
  GLenum drawBuffers[] = {GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT};
  glDrawBuffers(2, drawBuffers);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    std::cout << "ERROR::FRAMEBUFFER:: Opaque framebuffer is not complete!"
              << std::endl;

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  glBindFramebuffer(GL_FRAMEBUFFER, FBO_1);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         color_texture_1, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                         depth_texture_1, 0);
  // drawBuffers[] = {GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT};
  glDrawBuffers(2, drawBuffers);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    std::cout << "ERROR::FRAMEBUFFER:: Opaque framebuffer is not complete!"
              << std::endl;

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  unsigned int oitTexture;
  glGenTextures(1, &oitTexture);
  glBindTexture(GL_TEXTURE_2D, oitTexture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGBA,
               GL_HALF_FLOAT, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);

  unsigned int oitRenderFBO;
  glGenFramebuffers(1, &oitRenderFBO);
  glBindFramebuffer(GL_FRAMEBUFFER, oitRenderFBO);

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         oitTexture, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                         depth_texture_0, 0);

  GLenum drawBuffersOIT[] = {GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT};
  glDrawBuffers(2, drawBuffersOIT);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    std::cout << "ERROR::FRAMEBUFFER:: Opaque framebuffer is not complete!"
              << std::endl;
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  auto cameraModel = glm::mat4(1.0f);
  auto cameraView = glm::mat4(1.0f);
  auto cameraProjection = glm::mat4(1.0f);

  glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
  // glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  while (!glfwWindowShouldClose(window)) {

    // view_rotate += 1.0f;
    // input
    // -----
    processInput(window);

    /* Pass 1 */
    // 初始化
    // depth0, -> 1.0 输入输出 (1.0)
    // color0, -> 被剥离的颜色

    // depth1, -> 1.0 输入输出 (0.0)
    // color1, -> 混合的结果 (0,0,0,1.0)
    // in: 0, 1, 0
    // out: 1, 0, 1

    glBindFramebuffer(GL_FRAMEBUFFER, FBO_0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           color_texture_0, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           depth_texture_0, 0);

    glClearColor(0, 0, 0, 1.0); //
    glClearDepth(0.0f);         // 深度缓冲初始化值
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, FBO_1);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           color_texture_1, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           depth_texture_1, 0);

    glClearColor(0, 0, 0, 1.0); //
    glClearDepth(0.0f);         // 深度缓冲初始化值
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    // glDisable(GL_CULL_FACE);
    // glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);

    cameraView = glm::mat4(1.0f);
    cameraView =
        glm::lookAt(2.0f * glm::vec3(glm::sin(glm::radians(view_rotate)), 0.0f,
                                     glm::cos(glm::radians(view_rotate))),
                    glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    cameraProjection = glm::perspective(
        glm::radians(90.0f), (float)(SCR_WIDTH) / (float)(SCR_HEIGHT), 0.1f,
        100.0f);

    // // model, view, projection
    // cameraModel = glm::mat4(1.0f);
    // cameraModel = glm::translate(cameraModel, glm::vec3(-0.5f, 0.0f, 0.8f));
    // cameraModel = glm::rotate(cameraModel, glm::radians(0.0f),
    //                           glm::vec3(1.0f, 0.0f, 0.0f));
    // cameraModel = glm::rotate(cameraModel, glm::radians(0.0f),
    //                           glm::vec3(0.0f, 1.0f, 0.0f));
    // cameraModel = glm::rotate(cameraModel, glm::radians(0.0f),
    //                           glm::vec3(0.0f, 0.0f, 1.0f));
    // cameraModel = glm::scale(cameraModel, glm::vec3(0.5f, 0.5f, 0.5f));

    // PASS 2
    int input_depth_texture = 0;
    int output_depth_texture = 1;
    GLuint queryId;
    glGenQueries(1, &queryId);
    GLint available = 0;
    GLuint sampleCount = 0;

    int max_depth_peeling_layer = 10;
    for (int i = 0; i < max_depth_peeling_layer; i++) {

      // FBO1 用来接收 输出
      glBindFramebuffer(GL_FRAMEBUFFER, FBO_1);
      glFramebufferTexture2D(
          GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
          output_depth_texture ? depth_texture_1 : depth_texture_0, 0);

      glClearColor(0.0, 0.0, 0.0, 0.0);
      glClearDepth(1.0f); // 深度缓冲初始化值
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);

      glBeginQuery(GL_SAMPLES_PASSED, queryId);

      // GL_SAMPLES_PASSED_ARB
      depthPeelingRenderShader.use();
      depthPeelingRenderShader.setVec3("cameraPos", cameraPos);
      depthPeelingRenderShader.setVec3("lightPos", lightPos);
      depthPeelingRenderShader.setVec3("k", k);

      // model, view, projection

      cameraView = glm::mat4(1.0f);
      cameraView = glm::lookAt(
          2.0f * glm::vec3(glm::sin(glm::radians(view_rotate)), 0.0f,
                           glm::cos(glm::radians(view_rotate))),
          glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
      cameraProjection = glm::perspective(
          glm::radians(90.0f), (float)(SCR_WIDTH) / (float)(SCR_HEIGHT), 0.1f,
          100.0f);

      cameraModel = glm::mat4(1.0f);
      cameraModel = glm::translate(cameraModel, glm::vec3(0.0f, 0.0f, 0.0f));
      cameraModel = glm::rotate(cameraModel, glm::radians(0.0f),
                                glm::vec3(1.0f, 0.0f, 0.0f));
      cameraModel = glm::rotate(cameraModel, glm::radians(0.0f),
                                glm::vec3(0.0f, 1.0f, 0.0f));
      cameraModel = glm::rotate(cameraModel, glm::radians(0.0f),
                                glm::vec3(0.0f, 0.0f, 1.0f));
      cameraModel = glm::scale(cameraModel, glm::vec3(0.5f, 0.5f, 0.5f));

      depthPeelingRenderShader.setMat4("model", cameraModel);
      depthPeelingRenderShader.setMat4("view", cameraView);
      depthPeelingRenderShader.setMat4("projection", cameraProjection);

      spot.Draw(depthPeelingRenderShader, FBO_1,
                {{"texture_diffuse", texture_spot.id},
                 {"texture_depth",
                  input_depth_texture ? depth_texture_1 : depth_texture_0}},
                {}, GL_TRIANGLES, {false, false});

      // model, view, projection
      cameraModel = glm::mat4(1.0f);
      cameraModel = glm::translate(cameraModel, glm::vec3(-0.5f, 0.0f, 0.8f));
      cameraModel = glm::rotate(cameraModel, glm::radians(0.0f),
                                glm::vec3(1.0f, 0.0f, 0.0f));
      cameraModel = glm::rotate(cameraModel, glm::radians(0.0f),
                                glm::vec3(0.0f, 1.0f, 0.0f));
      cameraModel = glm::rotate(cameraModel, glm::radians(0.0f),
                                glm::vec3(0.0f, 0.0f, 1.0f));
      cameraModel = glm::scale(cameraModel, glm::vec3(0.5f, 0.5f, 0.5f));

      depthPeelingRenderShader.setMat4("model", cameraModel);
      depthPeelingRenderShader.setMat4("view", cameraView);
      depthPeelingRenderShader.setMat4("projection", cameraProjection);

      quad.Draw(depthPeelingRenderShader, FBO_1,
                {{"texture_diffuse", texture_window_r.id},
                 {"texture_depth",
                  input_depth_texture ? depth_texture_1 : depth_texture_0}},
                {}, GL_TRIANGLES, {false, false});

      // model, view, projection
      cameraModel = glm::mat4(1.0f);
      cameraModel = glm::translate(cameraModel, glm::vec3(0.2f, -0.5f, -1.0f));
      cameraModel = glm::rotate(cameraModel, glm::radians(0.0f),
                                glm::vec3(1.0f, 0.0f, 0.0f));
      cameraModel = glm::rotate(cameraModel, glm::radians(0.0f),
                                glm::vec3(0.0f, 1.0f, 0.0f));
      cameraModel = glm::rotate(cameraModel, glm::radians(0.0f),
                                glm::vec3(0.0f, 0.0f, 1.0f));
      cameraModel = glm::scale(cameraModel, glm::vec3(0.5f, 0.5f, 0.5f));

      depthPeelingRenderShader.setMat4("model", cameraModel);
      depthPeelingRenderShader.setMat4("view", cameraView);
      depthPeelingRenderShader.setMat4("projection", cameraProjection);

      quad.Draw(depthPeelingRenderShader, FBO_1,
                {{"texture_diffuse", texture_window_g.id},
                 {"texture_depth",
                  input_depth_texture ? depth_texture_1 : depth_texture_0}},
                {}, GL_TRIANGLES, {false, false});

      // // model, view, projection
      cameraModel = glm::mat4(1.0f);
      cameraModel = glm::translate(cameraModel, glm::vec3(0.2f, 0.0f, -0.5f));
      cameraModel = glm::rotate(cameraModel, glm::radians(0.0f),
                                glm::vec3(1.0f, 0.0f, 0.0f));
      cameraModel = glm::rotate(cameraModel, glm::radians(0.0f),
                                glm::vec3(0.0f, 1.0f, 0.0f));
      cameraModel = glm::rotate(cameraModel, glm::radians(0.0f),
                                glm::vec3(0.0f, 0.0f, 1.0f));
      cameraModel = glm::scale(cameraModel, glm::vec3(0.5f, 0.5f, 0.5f));

      depthPeelingRenderShader.setMat4("model", cameraModel);
      depthPeelingRenderShader.setMat4("view", cameraView);
      depthPeelingRenderShader.setMat4("projection", cameraProjection);

      quad.Draw(depthPeelingRenderShader, FBO_1,
                {{"texture_diffuse", texture_window_b.id},
                 {"texture_depth",
                  input_depth_texture ? depth_texture_1 : depth_texture_0}},
                {}, GL_TRIANGLES, {false, false});

      glEndQuery(GL_SAMPLES_PASSED);

      while (!available) {
        glGetQueryObjectiv(queryId, GL_QUERY_RESULT_AVAILABLE, &available);
      }

      if (available) {

        glGetQueryObjectuiv(queryId, GL_QUERY_RESULT, &sampleCount);
        std::cout << "Samples passed: " << sampleCount << std::endl;
      }

      // blend
      // break;

      // 开始混合
      // src color_1
      // dst color_0
      depthPeelingBlendShader.use();
      glEnable(GL_BLEND);
      glDepthMask(GL_FALSE);
      glDisable(GL_DEPTH_TEST);

      glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE, GL_ZERO,
                          GL_ONE_MINUS_SRC_ALPHA);

      // glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);

      quad.Draw(depthPeelingBlendShader, FBO_0,
                {{"texture_diffuse", color_texture_1}}, {}, GL_TRIANGLES,
                {false, true});

      // quadShader.use();
      // quad.Draw(quadShader, FBO_0, {{"texture_diffuse", color_texture_0}},
      // {},
      //           GL_TRIANGLES, {false, true});

      input_depth_texture = (input_depth_texture + 1) % 2;
      output_depth_texture = (output_depth_texture + 1) % 2;
      glDisable(GL_BLEND);
      glDepthMask(GL_TRUE);
      glEnable(GL_DEPTH_TEST);

      if (sampleCount <= 0) {
        break;
      }
    };

    // cout << "c:" << c << "\n";
    // // blend background
    depthPeelingFinalShader.use();
    depthPeelingFinalShader.setVec3("background_color",
                                    glm::vec3(0.2f, 0.3f, 0.3f));

    // // glDisable(GL_DEPTH_TEST);
    quad.Draw(depthPeelingFinalShader, 0,
              {{"texture_diffuse", color_texture_0}}, {}, GL_TRIANGLES,
              {true, true});

    // quadShader.use();
    // quad.Draw(quadShader, 0, {{"texture_diffuse", color_texture_0}}, {},
    //           GL_TRIANGLES, {true, true});

    // quadShader.use();
    // quad.Draw(quadShader, 0, {{"texture_diffuse", depth_texture_0}}, {},
    //           GL_TRIANGLES, {true, true});
    glfwSwapBuffers(window);
    glfwPollEvents();
  }

  /************************************/
  glfwTerminate();
  /************************************/
  return 0;
}

void framebuffer_size_callback(GLFWwindow *window, int width, int height) {
  SCR_WIDTH = width;
  SCR_HEIGHT = height;
  glViewport(0, 0, width, height);
}

void processInput(GLFWwindow *window) {

  if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
    glfwSetWindowShouldClose(window, true);
  } else if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
    view_rotate += 1.0f;
  } else if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
    view_rotate -= 1.0f;
  }
}