# OpenGL OIT 之 Stochastic Transparency 实现（下篇）：Shader 与关键代码实现

> 源码地址：[GitHub 仓库]((https://github.com/HalCG/OpenGLInstance/tree/main/OpenGL_OIT_Stochastic_Transparency))

> 上篇介绍了 Stochastic Transparency 的原理——用 MSAA 的 `gl_SampleMask` 将 alpha 值转化为随机样本覆盖率，一个 Shader 一个 Pass 完成透明渲染。本文进入代码实现。

---

## 1. 核心 Shader 实现

### 1.1 quad.vert —— 顶点着色器

```glsl
#version 430 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNor;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 vertexPos;
out vec3 vertexNor;
out vec2 textureCoord;

void main() {
    textureCoord = aTexCoord;
    gl_Position = projection * view * model * vec4(aPos, 1.0f);
    vertexPos = (model * vec4(aPos, 1.0f)).xyz;
    vertexNor = mat3(transpose(inverse(model))) * aNor;
}
```

标准的 MVP 变换，输出世界空间坐标和法线（虽然本实现中 Fragment Shader 未使用光照，但保持了接口完整性）。

### 1.2 quad.frag —— 片段着色器（核心）

这是整个 Stochastic Transparency 的**唯一关键 Shader**，只有 26 行，完成了全部透明渲染逻辑：

```glsl
#version 420 core
out vec4 FragColor;

in vec2 textureCoord;

uniform int frameID;       // 随机数种子（每个对象不同）
uniform int sampleCnt;     // MSAA 样本数（如 16）
uniform sampler2D texture_diffuse;

void main() {
    vec4 color = texture(texture_diffuse, textureCoord);

    // ==== 步骤 1: 获取 alpha 值作为覆盖率 ====
    float coverage = color.w;

    // ==== 步骤 2: 为每个 MSAA 样本生成随机掩码 ====
    uint randMask = 0u;
    for (int i = 0; i < sampleCnt; i++) {
        // 伪随机数生成：基于样本索引 i 和对象 ID frameID
        vec2 seed = vec2(i, frameID);
        float r = fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);

        // 如果随机数 < 覆盖率，则启用该样本
        if (r < coverage) {
            randMask |= (1u << i);
        }
    }

    // ==== 步骤 3: 写入 sample mask ====
    gl_SampleMask[0] = int(randMask);

    // ==== 步骤 4: 输出颜色（硬件 MSAA 自动解析） ====
    FragColor = color;
}
```

---

## 2. 逐行解析

### 2.1 `gl_SampleMask[0]` —— 逐样本控制

```glsl
gl_SampleMask[0] = int(randMask);
```

这是整个 Stochastic Transparency 的**核心 API**。`gl_SampleMask` 是 GLSL 内建输出变量：

| 项目 | 说明 |
|------|------|
| 类型 | `out int gl_SampleMask[]` |
| 可用版本 | GLSL 4.0+（需 `#version 400` 或更高） |
| 含义 | 每个 bit 控制一个 MSAA 样本是否被当前片段覆盖 |
| 范围 | `gl_SampleMask[0]` 的 bit 0-31 对应样本 0-31 |

**工作原理图解**：

```
假设 MSAA 16x，alpha = 0.5

  样本编号:  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
  随机数 r: 0.3 0.7 0.1 0.9 0.4 0.6 0.2 0.8 0.5 0.3 0.9 0.1 0.7 0.4 0.6 0.2
  r < 0.5?   Y   N   Y   N   Y   N   Y   N   Y   Y   N   Y   N   Y   N   Y
  randMask:  1   0   1   0   1   0   1   0   1   1   0   1   0   1   0   1
                                      ↑
                              bit 8 = 1 → 样本 8 被覆盖

  16 个样本中约 8 个被覆盖（≈ 50%），与 alpha 值匹配
```

### 2.2 伪随机数生成

```glsl
vec2 seed = vec2(i, frameID);
float r = fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);
```

这是一个经典的 GLSL 伪随机数生成器（常用于 ShaderToy）：

| 步骤 | 计算 | 说明 |
|------|------|------|
| 1 | `dot(seed, vec2(12.9898, 78.233))` | 点积，将二维种子映射为一维值 |
| 2 | `sin(...)` | 三角函数，输出在 [-1, 1] |
| 3 | `* 43758.5453` | 放大（大常数放大波动） |
| 4 | `fract(...)` | 取小数部分，结果在 [0, 1) |

**为什么用 `vec2(i, frameID)` 作为种子？**
- `i`：样本索引（0..15），保证 16 个样本的随机数序列不同
- `frameID`：对象 ID（0..3），保证不同对象的随机数序列不同

### 2.3 覆盖率决策

```glsl
float coverage = color.w;  // alpha 值
if (r < coverage) {
    randMask |= (1u << i);
}
```

`(1u << i)` 将 bit i 置为 1。例如：

| i | 1u << i | 二进制 |
|---|---------|--------|
| 0 | 1       | 0000000000000001 |
| 1 | 2       | 0000000000000010 |
| 2 | 4       | 0000000000000100 |
| ... | ... | ... |
| 15 | 32768   | 1000000000000000 |

---

## 3. CPU 侧关键代码

### 3.1 初始化 —— MSAA 状态设置

```cpp
bool StochasticTransparencyApp::init() {
    // ... 窗口初始化 ...

    // 核心：启用 MSAA 和 Sample Mask
    glEnable(GL_MULTISAMPLE);   // 启用 MSAA 多重采样
    glEnable(GL_SAMPLE_MASK);   // 允许 Shader 写入 gl_SampleMask

    // 深度测试配置
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);     // 通过测试：深度 <= 当前深度
    glDepthMask(GL_TRUE);       // 写入深度缓冲

    return true;
}
```

**`GL_MULTISAMPLE` vs `GL_SAMPLE_MASK` 的区别**：

| 状态 | 作用 |
|------|------|
| `GL_MULTISAMPLE` | 启用 MSAA 硬件管线——每个像素维护多个样本 |
| `GL_SAMPLE_MASK` | 允许 Fragment Shader 通过 `gl_SampleMask` 控制每个样本的写入 |

**两者必须同时启用**。如果只启用 `GL_MULTISAMPLE` 而不启用 `GL_SAMPLE_MASK`，Shader 中的 `gl_SampleMask` 赋值将被忽略，所有样本都被覆盖。

### 3.2 窗口创建 —— MSAA 样本数配置

```cpp
bool StochasticTransparencyApp::initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // 关键：请求 16x MSAA
    glfwWindowHint(GLFW_SAMPLES, 16);

    window_ = glfwCreateWindow(/* ... */);

    // 确认实际支持的样本数
    GLint maxSamples;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    std::cout << "Max supported MSAA samples: " << maxSamples << std::endl;
}
```

**几点说明**：
- `GLFW_SAMPLES` 是窗口级别的提示，实际样本数取决于 GPU 硬件支持
- `glGetIntegerv(GL_MAX_SAMPLES)` 查询 GPU 支持的最大 MSAA 样本数
- 样本数越多，随机近似越精确，但性能开销也越大

### 3.3 renderScene() —— 渲染循环

```cpp
void StochasticTransparencyApp::renderScene() {
    shaderQuad_->use();

    // 设置相机矩阵
    glm::mat4 cameraView = glm::lookAt(/* ... */);
    glm::mat4 cameraProjection = glm::perspective(/* ... */);
    shaderQuad_->setMat4("view", cameraView);
    shaderQuad_->setMat4("projection", cameraProjection);

    // 获取 MSAA 样本数
    GLint maxSamples;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);

    static int frameID = 0;
    int modelCnt = 4;

    // ===== 绘制 Spot（不透明） =====
    shaderQuad_->setMat4("model", modelMatrix(glm::vec3(0.0f, 0.0f, 0.0f)));
    shaderQuad_->setInt("sampleCnt", maxSamples);
    shaderQuad_->setInt("frameID", (frameID++) % modelCnt);
    modelSpot_->Draw(*shaderQuad_, 0,  // 0 = 直接渲染到默认帧缓冲
                    {{"texture_diffuse", texSpot_->id}}, {},
                    GL_TRIANGLES, {false, false});

    // ===== 绘制 Blue Window（透明） =====
    shaderQuad_->setMat4("model", modelMatrix(glm::vec3(0.3f, -0.1f, -0.8f), 0.5f));
    shaderQuad_->setInt("sampleCnt", maxSamples);
    shaderQuad_->setInt("frameID", (frameID++) % modelCnt);
    modelQuad_->Draw(*shaderQuad_, 0,
                    {{"texture_diffuse", texWindowB_->id}}, {},
                    GL_TRIANGLES, {false, false});

    // ===== 绘制 Green Window（透明） =====
    shaderQuad_->setMat4("model", modelMatrix(glm::vec3(0.6f, 0.6f, -0.6f), 0.5f));
    shaderQuad_->setInt("sampleCnt", maxSamples);
    shaderQuad_->setInt("frameID", (frameID++) % modelCnt);
    modelQuad_->Draw(*shaderQuad_, 0,
                    {{"texture_diffuse", texWindowG_->id}}, {},
                    GL_TRIANGLES, {false, false});

    // ===== 绘制 Red Window（透明） =====
    shaderQuad_->setMat4("model", modelMatrix(glm::vec3(0.0f, 0.0f, 0.0f), 0.5f));
    shaderQuad_->setInt("sampleCnt", maxSamples);
    shaderQuad_->setInt("frameID", (frameID++) % modelCnt);
    modelQuad_->Draw(*shaderQuad_, 0,
                    {{"texture_diffuse", texWindowR_->id}}, {},
                    GL_TRIANGLES, {false, false});
}
```

**关键观察**：
1. **直接渲染到默认帧缓冲**（`targetFbo = 0`）——不需要任何离屏 FBO
2. **深度测试自动处理遮挡**——硬件 MSAA 对每个样本独立进行深度测试
3. **frameID 递增**——每个对象有独立的随机种子

### 3.4 完整渲染循环

```cpp
void StochasticTransparencyApp::run() {
    while (!glfwWindowShouldClose(window_)) {
        processInput(window_);
        beginFrame();      // 清空颜色 + 深度缓冲
        renderScene();     // 单 Pass 渲染所有对象
        endFrame();        // glfwSwapBuffers（硬件 MSAA 解析）
    }
}
```

与 Linked List（3 Pass）和 Depth Peeling（N Pass）相比，Stochastic Transparency 的渲染循环**极其简洁**——只有一个渲染 Pass，没有 FBO 切换，没有纹理绑定。

---

## 4. 完整数据流

```
┌──────────────┐    ┌──────────────────┐    ┌─────────────────────────┐
│  CPU 侧      │    │  顶点着色器       │    │  片段着色器（核心）       │
│              │    │                  │    │                         │
│  frameID ────┼────┼──────────────────┼────┤→ 随机种子               │
│  sampleCnt ──┼────┼──────────────────┼────┤→ 循环次数               │
│              │    │                  │    │                         │
│  model ──────┼────┤→ MVP 变换       │    │                         │
│  view ───────┼────┤→ worldPos       │    │                         │
│  projection ─┼────┤→ gl_Position    │    │                         │
│              │    │                  │    │                         │
│              │    │                  │    │  texture → color.a      │
│              │    │                  │    │         ↓               │
│              │    │                  │    │  coverage = color.a     │
│              │    │                  │    │         ↓               │
│              │    │                  │    │  for i in 0..sampleCnt: │
│              │    │                  │    │    r = random(i,frameID)│
│              │    │                  │    │    if r < coverage:     │
│              │    │                  │    │      randMask |= 1<<i   │
│              │    │                  │    │         ↓               │
│              │    │                  │    │  gl_SampleMask[0] = ... │
│              │    │                  │    │  FragColor = color      │
└──────────────┘    └──────────────────┘    └───────────┬─────────────┘
                                                        │
                                                        ▼
                                          ┌─────────────────────────┐
                                          │  硬件 MSAA 管线           │
                                          │                         │
                                          │  每个样本独立深度测试     │
                                          │  每个样本按 mask 覆盖     │
                                          │  resolve: 样本平均 → 像素 │
                                          └───────────┬─────────────┘
                                                      │
                                                      ▼
                                          ┌─────────────────────────┐
                                          │  默认帧缓冲              │
                                          │  glfwSwapBuffers → 屏幕  │
                                          └─────────────────────────┘
```

---

## 5. 总结

| 组成 | 说明 |
|------|------|
| **vertex shader** | 标准 MVP 变换，无特殊逻辑 |
| **fragment shader** | 唯一核心：alpha → 随机样本掩码 → `gl_SampleMask[0]` |
| **随机数** | `fract(sin(dot(seed, ...)) * 43758.5453)` 经典 ShaderToy 随机 |
| **frameID** | 区分不同对象的随机种子，`(frameID++) % modelCnt` 循环 |
| **GL 状态** | `GL_MULTISAMPLE` + `GL_SAMPLE_MASK` + `GL_DEPTH_TEST` |
| **渲染目标** | 直接渲染到默认帧缓冲，无 FBO |
| **Pass 数量** | 1 个 Pass |

Stochastic Transparency 的代码量是三种 OIT 方案中最少的——核心 Shader 只有 26 行。它用概率论替代了精确排序，用 MSAA 硬件替代了手动混合，是"less is more"的典范。代价是结果的随机噪声，但对于许多应用场景（游戏、实时预览等），这种近似是完全可接受的。
