# OpenGL OIT 之 Depth Peeling 实现（下篇）：Shader 与关键代码实现

> 上篇介绍了 Depth Peeling 的原理、三 FBO 设计和双深度乒乓机制。本文进入代码实现，逐行解析关键 Shader 和 CPU 侧渲染循环。

> 源码地址：[GitHub 仓库](https://github.com/HalCG/OpenGLInstance/tree/main/OpenGL_OIT_Depth_Peeling)

---

## 1. 核心 Shader 实现

### 1.1 depth_peeling_render.frag —— 剥离层着色器

这是整个 Depth Peeling 最核心的 Shader，负责渲染当前剥离层：

```glsl
#version 430 core
layout(location = 0) out vec4 FragColor;

in vec3 vertexPos;
in vec3 vertexNor;
in vec2 textureCoord;

uniform vec3 cameraPos;
uniform vec3 lightPos;
uniform vec3 k;    // x=环境光, y=漫反射, z=高光

uniform sampler2D texture_diffuse;   // 物体颜色纹理
uniform sampler2D texture_depth;     // 上一层的深度纹理
uniform vec2 u_ScreenSize;           // 屏幕尺寸（用于归一化 gl_FragCoord）

void main() {
    // ==== 步骤 1: 归一化屏幕坐标 ====
    // gl_FragCoord.xy 是像素坐标，uv 归一化到 [0, 1]
    vec2 uv = gl_FragCoord.xy / u_ScreenSize;

    // ==== 步骤 2: 采样上一层的深度 ====
    // 从上一层的深度纹理中读取该像素的深度值
    float frontDepth = texture(texture_depth, uv).r;

    // ==== 步骤 3: 深度裁剪 —— 这是"剥离"的核心 ====
    // 如果当前片段的深度 <= 上一层的深度（即不比上一层更远），丢弃
    // 注意：这里用的是 <= 而不是 <，因为 <= 表示"当前片段在上一层的前面或同一位置"
    // 只有 gl_FragCoord.z > frontDepth 的片段才保留（即比上一层更远的片段）
    if (gl_FragCoord.z <= frontDepth) {
        discard;
    }

    // ==== 步骤 4: Blinn-Phong 光照计算 ====
    vec3 lightColor = vec3(1.0);

    float ambientStrength = k.x;
    vec3 ambient = ambientStrength * lightColor;

    float diffuseStrength = k.y;
    vec3 normalDir = normalize(vertexNor);
    vec3 lightDir = normalize(lightPos - vertexPos);
    vec3 diffuse =
        diffuseStrength * max(dot(normalDir, lightDir), 0.0) * lightColor;

    float specularStrength = k.z;
    vec3 viewDir = normalize(cameraPos - vertexPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    vec3 specular = specularStrength *
                    pow(max(dot(normalDir, halfwayDir), 0.0), 2.0) * lightColor;

    // ==== 步骤 5: 采样纹理颜色和透明度 ====
    vec3 objectColor = vec3(0.8);
    float alpha = 0.0;
    if (textureCoord.x >= 0.0 && textureCoord.y >= 0.0) {
        vec4 sampled = texture(texture_diffuse, textureCoord);
        objectColor = sampled.rgb;
        alpha = sampled.a;
    }

    // 输出带透明度的光照颜色
    FragColor = vec4((ambient + diffuse + specular) * objectColor, alpha);
}
```

**深度裁剪逻辑详解**：

```
假设当前像素有 3 个透明片段重叠:

            Camera
              ▼
    ┌─────────────────────────────────┐
    │  Fragment A: depth = 0.3       │  ← 最近
    │  Fragment B: depth = 0.5       │
    │  Fragment C: depth = 0.7       │  ← 最远
    └─────────────────────────────────┘

Layer 1 剥离:
  frontDepth = 0.0 (初始深度缓冲值)
  条件: gl_FragCoord.z <= 0.0 ?  → 都不满足
  结果: A, B, C 都通过 → 硬件深度测试 GL_LESS 保留 A (depth=0.3)
  剥离出 A

Layer 2 剥离:
  frontDepth = 0.3 (Layer 1 的 A 深度)
  条件: gl_FragCoord.z <= 0.3 ?
    A: 0.3 <= 0.3 → discard ✓
    B: 0.5 <= 0.3 → 不满足，通过 → 硬件深度测试保留 B (depth=0.5)
    C: 0.7 <= 0.3 → 不满足，通过 → 硬件深度测试被 B 遮挡
  剥离出 B

Layer 3 剥离:
  frontDepth = 0.5 (Layer 2 的 B 深度)
  条件: gl_FragCoord.z <= 0.5 ?
    A: 0.3 <= 0.5 → discard ✓
    B: 0.5 <= 0.5 → discard ✓
    C: 0.7 <= 0.5 → 不满足，通过 → 硬件深度测试保留 C (depth=0.7)
  剥离出 C

Layer 4:
  frontDepth = 0.7
  所有片段都被 discard → sampleCount = 0 → break
```

### 1.2 depth_peeling_blend.frag —— 混合着色器

这是一个极简的全屏四边形 Shader，只做颜色传递：

```glsl
#version 330 core
out vec4 FragColor;
in vec2 textureCoord;
uniform sampler2D texture_diffuse;  // 当前剥离层的颜色纹理

void main() {
    FragColor = texture(texture_diffuse, textureCoord);
}
```

**为什么这么简单？** 因为混合逻辑完全由 `glBlendFuncSeparate` 处理——Shader 只需要输出颜色，OpenGL 的混合管线自动完成 Front-to-Back 累积。

### 1.3 depth_peeling_final.frag —— 最终合成着色器

将累积的颜色与背景色混合，输出到屏幕：

```glsl
#version 430 core
layout(location = 0) out vec4 FragColor;
in vec2 textureCoord;

uniform vec3 background_color;
uniform sampler2D texture_diffuse;  // fboAccum_.color（累积颜色）

void main() {
    vec4 frontColor = texture(texture_diffuse, textureCoord);

    // 将累积颜色与背景色混合
    // frontColor.alpha 表示"剩余透明度"（即 1 - 累积不透明度）
    // 背景色按剩余透明度混合
    FragColor = frontColor + vec4(background_color, 1.0) * frontColor.a;
    FragColor.a = 1.0;  // 最终输出不透明
}
```

**混合逻辑**：
- `frontColor` 是累积缓冲区中已经混合好的颜色
- `frontColor.a` 是"剩余透明度"：在 Front-to-Back 混合中，alpha 会逐渐减小，`1 - alpha` 表示已累积的不透明度
- 背景色乘以剩余透明度，加到累积颜色上，得到最终结果

---

## 2. CPU 侧关键代码

### 2.1 初始化：FBO 和深度缓冲

```cpp
bool DepthPeelingApp::initFramebuffers() {
    const int w = static_cast<int>(width_);
    const int h = static_cast<int>(height_);

    // 创建两个乒乓 FBO（颜色 + 深度）
    fboAccum_.create(w, h, "Accumulation (FBO_0)");
    fboPeel_.create(w, h, "Peel layer (FBO_1)");

    // 创建 oitRenderFBO — 与 fboAccum_ 共享深度纹理
    texOitColor_.createColorHDR(w, h);
    glGenFramebuffers(1, &fboOit_);
    glBindFramebuffer(GL_FRAMEBUFFER, fboOit_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           texOitColor_.id, 0);
    // 关键：oitRenderFBO 的深度附件 = fboAccum_ 的深度纹理
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           fboAccum_.depth.id, 0);
    // ...
}
```

**FBO 深度纹理共享**：`fboOit_` 和 `fboAccum_` 共享同一个深度纹理，这样透明物体渲染时可以使用不透明物体的深度进行遮挡测试。

### 2.2 GlFramebuffer 辅助类

```cpp
class GlFramebuffer {
public:
    GLuint fbo = 0;
    GlTexture2D color;   // RGBA16F 颜色纹理
    GlTexture2D depth;   // DEPTH_COMPONENT32F 深度纹理

    void create(int width, int height, const char *debugName) {
        destroy();
        color.createColorHDR(width, height);
        depth.createDepth32F(width, height);

        glGenFramebuffers(1, &fbo);
        bindColorDepth(color.id, depth.id);
    }

    void bindColorDepth(GLuint colorTex, GLuint depthTex) const {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, colorTex, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                               GL_TEXTURE_2D, depthTex, 0);
        // ...
    }
};

class GlTexture2D {
public:
    GLuint id = 0;

    void createColorHDR(int width, int height) {
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0,
                     GL_RGBA, GL_HALF_FLOAT, nullptr);
        // ... 设置 wrap/filter 参数
    }

    void createDepth32F(int width, int height) {
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        // ...
    }
};
```

**关键设计**：`bindColorDepth` 允许动态切换深度附件，这是乒乓机制的基础——在剥离循环中，每层都会将不同的深度纹理绑定到 `fboPeel_` 的深度附件上。

### 2.3 剥离前的初始化

```cpp
void DepthPeelingApp::initPeelBuffers() {
    // 清空 FBO_0（累积缓冲）：颜色(0,0,0,1)，深度 0.0（最近）
    fboAccum_.bindColorDepth(fboAccum_.color.id, fboAccum_.depth.id);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepth(0.0f);   // ← 关键：深度清除为 0.0（最近）
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 清空 FBO_1（剥离层）：颜色(0,0,0,0)，深度 0.0
    fboPeel_.bindColorDepth(fboPeel_.color.id, fboPeel_.depth.id);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepth(0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 全局深度状态
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);     // 深度值小于当前值的片段通过
    glDepthMask(GL_TRUE);     // 允许深度写入

    // 初始化乒乓索引
    inputDepthIndex_ = 0;   // fboAccum_.depth
    outputDepthIndex_ = 1;  // fboPeel_.depth
}
```

**为什么初始深度清除为 0.0？** 因为第 1 层剥离时，Shader 中的条件是 `gl_FragCoord.z <= frontDepth`，`frontDepth = 0.0`（初始值）。OpenGL 的深度范围是 [0, 1]，0 表示最近。任何片段的 `gl_FragCoord.z` 都 >= 0，所以第 1 层所有片段都会通过裁剪，由硬件深度测试 `GL_LESS` 选出每个像素上最近的那个片段。

### 2.4 剥离循环：核心实现

```cpp
void DepthPeelingApp::peelAndBlend() {
    for (int layer = 0; layer < AppConfig::kMaxDepthPeelLayers; ++layer) {

        // ===== Step A: 准备当前层的渲染目标 =====
        glBindFramebuffer(GL_FRAMEBUFFER, fboPeel_.fbo);
        // 动态绑定深度附件：使用 outputDepthIndex 对应的深度纹理
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                               GL_TEXTURE_2D,
                               depthTexture(outputDepthIndex_), 0);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);  // 透明背景
        glClearDepth(1.0f);                     // 深度清除为最远
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ===== Step B: 渲染场景 → 剥离一层 =====
        glBeginQuery(GL_SAMPLES_PASSED, queryId_);
        drawSceneLayer(*shaderPeel_, fboPeel_.fbo,
                       inputDepthIndex_, outputDepthIndex_);
        glEndQuery(GL_SAMPLES_PASSED);

        const GLuint sampleCount = waitSampleCount();

        // ===== Step C: 将剥离层混合到累积缓冲 =====
        shaderBlend_->use();
        glEnable(GL_BLEND);
        glDepthMask(GL_FALSE);     // 混合时不写深度
        glDisable(GL_DEPTH_TEST);  // 混合时不测试深度
        glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE,          // RGB
                            GL_ZERO, GL_ONE_MINUS_SRC_ALPHA); // Alpha

        // 全屏四边形：将 fboPeel_.color 混合到 fboAccum_.fbo
        modelQuad_->Draw(*shaderBlend_, fboAccum_.fbo,
                        {{"texture_diffuse", fboPeel_.color.id}}, {},
                        GL_TRIANGLES, {false, true});

        // 恢复状态
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);

        // ===== Step D: 交换深度索引 =====
        inputDepthIndex_ = (inputDepthIndex_ + 1) % 2;
        outputDepthIndex_ = (outputDepthIndex_ + 1) % 2;

        // ===== Step E: 提前终止检查 =====
        if (sampleCount <= 0) {
            break;  // 没有片段通过 → 没有更多层
        }
    }
}
```

### 2.5 场景绘制函数

```cpp
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

    // Lambda 复用绘制逻辑
    auto draw = [&](Model &model, const glm::vec3 &pos, GLuint diffuseId) {
        shader.setMat4("model", modelMatrix(pos));
        shader.setMat4("view", view);
        shader.setMat4("projection", projection);
        // 传入上一层的深度纹理作为裁剪参考
        model.Draw(shader, targetFbo,
                   {{"texture_diffuse", diffuseId},
                    {"texture_depth", inputDepth}},
                   {}, GL_TRIANGLES, {false, false});
    };

    // 绘制所有物体（不透明 + 透明，每个层都完整绘制一遍场景）
    draw(*modelSpot_, glm::vec3(0.0f, 0.0f, 0.0f), texSpot_->id);
    draw(*modelQuad_, glm::vec3(-0.5f, 0.0f, 0.8f), texWindowR_->id);
    draw(*modelQuad_, glm::vec3(0.2f, -0.5f, -1.0f), texWindowG_->id);
    draw(*modelQuad_, glm::vec3(0.2f, 0.0f, -0.5f), texWindowB_->id);
}
```

**注意**：每个剥离层都需要**完整绘制一遍场景中的所有物体**——这是 Depth Peeling 性能开销的主要来源。如果场景有 100 个物体，10 层剥离意味着需要绘制 1000 次。

### 2.6 最终合成

```cpp
void DepthPeelingApp::compositeToScreen() {
    shaderFinal_->use();
    shaderFinal_->setVec3("background_color", AppConfig::backgroundColor());
    // 全屏四边形：将累积颜色混合背景色，输出到默认帧缓冲
    modelQuad_->Draw(*shaderFinal_, 0,  // 0 = 默认帧缓冲
                    {{"texture_diffuse", fboAccum_.color.id}}, {},
                    GL_TRIANGLES, {true, true});
}
```

---

## 3. 完整渲染循环

```cpp
void DepthPeelingApp::run() {
    while (!glfwWindowShouldClose(window_)) {
        beginFrame();          // 处理输入
        initPeelBuffers();     // 初始化 FBO 和深度缓冲
        peelAndBlend();        // 逐层剥离 + Front-to-Back 混合
        compositeToScreen();   // 合成到屏幕
        endFrame();            // 交换缓冲 + 事件轮询
    }
}
```

---

## 4. 总结

Depth Peeling 的核心代码要点：

| 组成 | 关键逻辑 |
|------|---------|
| **剥离 Shader** | `if (gl_FragCoord.z <= frontDepth) discard` — 用上一层的深度裁剪当前层 |
| **混合 Shader** | 极简——颜色传递，混合由 `glBlendFuncSeparate` 完成 |
| **最终合成** | `frontColor + background * frontColor.a` — 累积颜色混合背景 |
| **乒乓机制** | `inputDepthIndex/outputDepthIndex` 交替使用两个深度纹理 |
| **提前终止** | `GL_SAMPLES_PASSED` 查询 → `sampleCount == 0` → break |
| **深度清除** | 初始化 `0.0`（最近），每层 `1.0`（最远） |
| **Front-to-Back** | `glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA)` |

与 Linked List 方案相比，Depth Peeling 的实现更"传统"——它完全依赖 OpenGL 标准管线（深度测试、混合），不需要任何 GPU 端数据结构（SSBO、Image Texture、Atomic Counter），因此兼容性极好，但代价是多 Pass 带来的性能开销。

> 源码地址：[GitHub 仓库](https://github.com/user/OpenGL_OIT_Depth_Peeling)（请替换为实际仓库地址）