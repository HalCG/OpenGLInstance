# OpenGL OIT 之 Linked List 实现（下篇）：Shader 与渲染 Pass 代码实现

> 上篇回顾了 Linked List OIT 的整体架构、四种特殊缓冲区和 GL 状态管理。本文直接进入代码，逐行解析关键 Shader 和 CPU 侧渲染 Pass 的实现。

---

## 1. 缓冲区初始化（CPU 侧）

在进入渲染循环之前，需要初始化四个 OIT 专用缓冲区。下面逐一解析。

### 1.1 原子计数器缓冲区

```cpp
GLuint zero = 0;
GLint nodeSize = 5 * sizeof(GLfloat) + sizeof(GLuint);
// nodeSize = 5*4 + 4 = 24 bytes
// 对应 Shader 中的 NodeType { vec4 color; float depth; uint next; }

maxNodes_ = width_ * height_ * 20;  // 800 * 600 * 20 = 9,600,000

// 创建原子计数器缓冲区
glGenBuffers(1, &atomicBuffer_);
glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, atomicBuffer_);
glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(GLuint), nullptr, GL_DYNAMIC_DRAW);
glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &zero);
```

**关键点**：
- `glBindBufferBase` 而非 `glBindBuffer`：前者将 buffer 绑定到特定的 **indexed binding point**（binding = 0），这是 Shader 中 `layout(binding = 0)` 所要求的
- `GL_DYNAMIC_DRAW`：每帧都要通过 `glBufferSubData` 更新，所以使用动态标记
- 初始化为 0：`glBufferSubData` 写入 `zero`

### 1.2 链表存储缓冲区 (SSBO)

```cpp
glGenBuffers(1, &linkedListBuffer_);
glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, linkedListBuffer_);
glBufferData(GL_SHADER_STORAGE_BUFFER, maxNodes_ * nodeSize, nullptr, GL_DYNAMIC_DRAW);
```

**关键点**：
- `glBufferData` 最后一个参数 `nullptr`：只分配空间，不初始化数据。每帧 Shader 会覆盖写入，初始值无意义
- `std430` 布局：Shader 中 `layout(std430)` 保证内存布局紧凑，`vec4` 16 字节 + `float` 4 字节 + `uint` 4 字节 = 24 字节，无 padding

### 1.3 头指针 Image Texture

```cpp
glGenTextures(1, &headPtrTexture_);
glBindTexture(GL_TEXTURE_2D, headPtrTexture_);
glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32UI, width_, height_);
glBindImageTexture(0, headPtrTexture_, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
```

**关键点**：
- `glTexStorage2D` 而非 `glTexImage2D`：`glTexStorage2D` 分配不可变的纹理存储，是 OpenGL 4.2+ 的推荐方式
- `GL_R32UI`：每个像素一个 `uint32`，存储头指针索引
- `glBindImageTexture`：将纹理绑定到 Image Unit 0，Shader 中通过 `layout(binding = 0, r32ui) uniform uimage2D` 访问

### 1.4 清空 PBO

```cpp
std::vector<GLuint> headPtrClearBuf(width_ * height_, 0xffffffff);
glGenBuffers(1, &clearBuf_);
glBindBuffer(GL_PIXEL_UNPACK_BUFFER, clearBuf_);
glBufferData(GL_PIXEL_UNPACK_BUFFER, headPtrClearBuf.size() * sizeof(GLuint),
             headPtrClearBuf.data(), GL_STATIC_COPY);
```

**关键点**：
- `0xFFFFFFFF`：这是链表尾哨兵值，在 Shader 中 `while(idx != 0xffffffff)` 判断链表是否结束
- `GL_STATIC_COPY`：数据在初始化时写入一次，之后只读。`COPY` 表示数据从 CPU 复制到 GPU，之后由 GPU 使用
- PBO 通过 `glTexSubImage2D(..., nullptr)` 使用，`nullptr` 表示数据源来自当前绑定的 `GL_PIXEL_UNPACK_BUFFER`

---

## 2. Pass 2 Shader：oitRender.frag 逐行解析

这是整个 OIT 系统最核心的 Shader，负责将每个透明片段插入到对应像素的链表中。

### 2.1 接口与数据结构

```glsl
#version 430 core
#define MAX_FRAGMENTS 75

layout (location = 0) out vec4 FragColor;

// 顶点着色器传入
in vec3 vertexPos;
in vec3 vertexNor;
in vec2 textureCoord;

// Uniforms
uniform vec3 cameraPos;
uniform vec3 lightPos;
uniform vec3 k;
uniform uint MaxNodes;

// 普通纹理
uniform sampler2D texture_diffuse;
uniform sampler2D texture_depth;  // 来自 Pass 1 的不透明物体深度纹理

// 链表节点结构体 — 必须与 CPU 侧的 ListNode 结构体对齐
struct NodeType {
    vec4 color;   // RGBA 颜色
    float depth;  // 深度值
    uint next;    // 下一节点索引
};

// ---- 三个特殊缓冲区的 Shader 声明 ----
layout(binding = 0, r32ui) uniform uimage2D headPointers;     // 头指针纹理
layout(binding = 0, offset = 0) uniform atomic_uint nextNodeCounter;  // 原子计数器
layout(binding = 0, std430) buffer linkedLists {               // 链表 SSBO
    NodeType nodes[];
};
```

**重要**：这三个 binding 虽然都是 `binding = 0`，但它们属于不同的 target 类型（`uimage2D` / `atomic_uint` / `std430 buffer`），互不冲突。这与 CPU 侧 `glBindBufferBase` 使用不同的 target 参数（`GL_ATOMIC_COUNTER_BUFFER` / `GL_SHADER_STORAGE_BUFFER`）和 `glBindImageTexture` 对应。

### 2.2 主逻辑

```glsl
void main() {
    // ==== 步骤 1: 深度遮挡剔除 ===
    // 将当前片段的屏幕坐标归一化到 [0,1] 范围
    vec2 uv = gl_FragCoord.xy / vec2(800, 600);

    // 从 Pass 1 的不透明深度纹理中采样
    float depth = texture(texture_depth, uv).r;

    // 如果当前片段的深度大于不透明物体的深度（即被遮挡），丢弃
    if (gl_FragCoord.z > depth + 0.0001) {
        discard;
    }

    // ==== 步骤 2: 原子分配节点索引 ===
    // atomicCounterIncrement 原子地返回旧值并自增 1
    // 返回值是当前片段可以使用的节点索引
    uint atomic_buffer = atomicCounterIncrement(nextNodeCounter);

    // ==== 步骤 3: 节点索引越界检查 ===
    if (atomic_buffer < MaxNodes) {

        // ==== 步骤 4: 头插法将新节点插入链表 ===
        // imageAtomicExchange 原子地：
        //   1. 读取 headPointers[x][y] 的旧值 → preHead
        //   2. 将 atomic_buffer 写入 headPointers[x][y]
        uint preHead = imageAtomicExchange(
            headPointers,
            ivec2(gl_FragCoord.xy),  // 像素坐标
            atomic_buffer             // 新头指针值
        );

        // 采样 diffuse 纹理获取片段颜色
        vec4 color = texture(texture_diffuse, textureCoord);

        // 写入链表节点
        nodes[atomic_buffer].color = color;
        nodes[atomic_buffer].depth = gl_FragCoord.z;
        nodes[atomic_buffer].next = preHead;  // 指向旧的头节点
    }

    // ==== 步骤 5: 输出到 oitRenderFBO ===
    // 这个输出本身不重要（颜色已经被写入链表），但 FBO 需要写入
    FragColor = texture(texture_diffuse, textureCoord);
}
```

### 2.3 头插法链表构建图解

假设像素 `(x, y)` 已经有 2 个节点，现在插入第 3 个：

```
插入前:
headPointers[x][y] = 5  ──→  Node[5]  ──→  Node[2]  ──→ END
                              (depth=0.7)    (depth=0.5)

插入 Node[8] (depth=0.3) 后:

1. atomicCounterIncrement 返回 8
2. imageAtomicExchange 返回 5（旧头指针）
3. 设置 Node[8]:
   nodes[8].color = 红色
   nodes[8].depth = 0.3
   nodes[8].next = 5   ← 指向旧头节点

headPointers[x][y] = 8  ──→  Node[8]  ──→  Node[5]  ──→  Node[2]  ──→ END
                              (depth=0.3)    (depth=0.7)    (depth=0.5)

注意：链表未排序，新节点总是在头部插入。排序在 Pass 3 的 composite.frag 中进行。
```

### 2.4 两个深度剔除机制

这个 Shader 有**两层**深度剔除——在 Pass 2 中，oitRenderFBO 的深度附件与 opaqueFBO 共享同一个 `opaqueDepthTexture`：

1. **硬件深度测试**（`glEnable(GL_DEPTH_TEST)`）：由 GPU 固定管线执行，比较 `gl_FragCoord.z` 与 depth buffer 中的值
2. **Shader 手动采样比较**（`texture(texture_depth, uv).r`）：在 Fragment Shader 中手动采样深度纹理并比较

因为有 `glDepthMask(GL_FALSE)`，透明片段的深度不会写入，所以硬件深度测试比较的是之前不透明物体写入的深度。Shader 中的手动比较是额外的安全措施。

---

## 3. Pass 3 Shader：composite.frag 逐行解析

全屏四边形 Shader，负责遍历当前像素的链表、排序、混合。

### 3.1 接口

```glsl
#version 430 core
#define MAX_FRAGMENTS 75

layout (location = 0) out vec4 FragColor;
in vec2 textureCoord;

uniform sampler2D texture_opaque;  // Pass 1 的不透明颜色纹理

// 与 oitRender.frag 相同的三个缓冲声明
struct NodeType { vec4 color; float depth; uint next; };
layout(binding = 0, r32ui) uniform uimage2D headPointers;
layout(binding = 0, offset = 0) uniform atomic_uint nextNodeCounter;
layout(binding = 0, std430) buffer linkedLists { NodeType nodes[]; };

const float EPSILON = 0.0001;
```

### 3.2 主逻辑

```glsl
void main() {

    // ==================== 步骤 1: 定义局部数组 ====================
    // 将链表中的数据复制到固定大小的数组中，方便排序
    NodeType frags[MAX_FRAGMENTS];  // 最多 75 个片段

    int count = 0;

    // ==================== 步骤 2: 遍历链表 ====================
    // 获取当前像素的头指针
    uint idx = imageLoad(headPointers, ivec2(gl_FragCoord.xy)).r;

    // 遍历链表，将节点复制到数组中
    while (idx != 0xffffffff && count < MAX_FRAGMENTS) {
        NodeType node = nodes[idx];

        // ---- 去重逻辑 ----
        // 问题：一个三角形面片的两个相邻三角形共享边，
        // 其边界上的片段会被光栅化两次，产生两个深度相同的片段
        // 解决：检查深度是否与已有片段相同，相同则跳过
        bool isDuplicate = false;
        for (int i = 0; i < count; i++) {
            if (abs(frags[i].depth - node.depth) < EPSILON) {
                isDuplicate = true;
                break;
            }
        }
        if (!isDuplicate) {
            frags[count] = node;
            count++;
        }

        idx = node.next;  // 移动到下一个节点
    }

    // ==================== 步骤 3: 插入排序（从远到近） ====================
    // 排序后：frags[0] 最深（最远），frags[count-1] 最浅（最近）
    for (int i = 1; i < count; i++) {
        int j = i;
        NodeType toInsertNode = frags[i];
        // 比较 depth：depth 越大越远 → 排在前面
        while (j > 0 && toInsertNode.depth > frags[j - 1].depth) {
            frags[j] = frags[j - 1];
            j--;
        }
        frags[j] = toInsertNode;
    }

    // ==================== 步骤 4: Back-to-Front Over 混合 ====================
    // 从远到近混合，使用标准的 over 运算
    vec4 color = texture(texture_opaque, textureCoord);  // 从背景颜色开始

    for (int i = 0; i < count; i++) {
        // over 运算：
        // C_result = C_src * A_src + C_dst * (1 - A_src)
        // A_result = A_src + A_dst * (1 - A_src)
        color.rgb = color.rgb * (1.0 - frags[i].color.a)
                  + frags[i].color.rgb * frags[i].color.a;
        color.a = color.a + frags[i].color.a * (1.0 - color.a);
    }

    FragColor = color;
}
```

### 3.3 排序方向说明

排序后的数组 `frags[]` 是**从远到近**排列的（`[0]` 最远，`[count-1]` 最近）：

```glsl
while (j > 0 && toInsertNode.depth > frags[j - 1].depth) {
    // depth 越大 → 越远 → 排在前面
```

这意味着 `frags[0]` 的 depth 最大（最远），先混合；`frags[count-1]` 的 depth 最小（最近），最后混合。这正是 Back-to-Front 混合的正确顺序。

### 3.4 Over 运算详解

Over 运算（Porter-Duff "A over B"）是透明混合的标准公式：

```
给定：
  C_src = 源颜色（当前要混合的片段颜色）
  A_src = 源 alpha
  C_dst = 目标颜色（已累计的颜色）
  A_dst = 目标 alpha

结果：
  C_result = C_src * A_src + C_dst * (1 - A_src)
  A_result = A_src + A_dst * (1 - A_src)
```

循环中，`color` 初始为不透明物体颜色（alpha=1.0），然后依次与每个透明片段混合。由于是 Back-to-Front 顺序，每个片段的背景就是它后面所有已混合的颜色。

### 3.5 去重逻辑的必要性

去重逻辑解决的是光栅化边界问题。考虑一个四边形由两个三角形组成：

```
    A ───── B
    │     ╱ │
    │   ╱   │
    │ ╱     │
    C ───── D
```

对角线 `B-C` 上的像素同时属于两个三角形，会被光栅化两次。如果不去重，链表中会有两个深度几乎相同的片段，混合时该边界会比周围更亮，产生可见的接缝。

通过比较 `abs(frags[i].depth - node.depth) < EPSILON`，可以识别并丢弃重复片段。

---

## 4. CPU 侧渲染 Pass 代码

### 4.1 Pass 1：不透明物体

```cpp
void LinkedListOITApp::renderOpaquePass() {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);       // 允许深度写入 — 关键！
    glDisable(GL_CULL_FACE);

    blinnPhongShader_->use();
    blinnPhongShader_->setVec3("cameraPos", cameraPos_);
    blinnPhongShader_->setVec3("lightPos", lightPos_);
    blinnPhongShader_->setVec3("k", k_);

    // 设置 MVP 矩阵
    glm::mat4 view = glm::lookAt(
        2.0f * glm::vec3(glm::sin(glm::radians(viewRotate_)),
                          0.0f,
                          glm::cos(glm::radians(viewRotate_))),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 projection = glm::perspective(...);

    blinnPhongShader_->setMat4("model", modelMatrix({0.0f, 0.0f, 0.0f}, 0.5f));
    blinnPhongShader_->setMat4("view", view);
    blinnPhongShader_->setMat4("projection", projection);

    // 绘制到 opaqueFBO（颜色 + 深度）
    spot_->Draw(*blinnPhongShader_, opaqueFBO_,
                {{"diffuse_texture", textureSpot_->id}}, {},
                GL_TRIANGLES, {true, true});
}
```

**输出**：`opaqueFBO_` 包含 `opaqueTexture_`（颜色附件）和 `opaqueDepthTexture_`（深度附件）

### 4.2 Pass 2：透明物体收集

```cpp
void LinkedListOITApp::renderTransparentPass() {
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);      // 禁止深度写入 — 关键！

    // ---- 重置 OIT 缓冲区 ----
    // 1. 原子计数器归零
    GLuint zero = 0;
    glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, atomicBuffer_);
    glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &zero);

    // 2. 重新绑定 SSBO
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, linkedListBuffer_);

    // 3. 清空头指针纹理（全部设为 0xFFFFFFFF = 空链表）
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, clearBuf_);
    glBindTexture(GL_TEXTURE_2D, headPtrTexture_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_,
                    GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);

    // ---- 绘制三个透明物体 ----
    oitRenderShader_->use();
    oitRenderShader_->setUint("MaxNodes", maxNodes_);
    // ... 设置 view, projection ...

    // 红色透明方块
    glm::mat4 model = modelMatrix({-0.5f, 0.0f, 0.8f}, 0.5f);
    oitRenderShader_->setMat4("model", model);
    quad_->Draw(*oitRenderShader_, oitRenderFBO_,
                {{"diffuse_texture", textureWindowR_->id},
                 {"texture_depth", opaqueDepthTexture_}},  // 传入不透明深度纹理
                {}, GL_TRIANGLES, {true, false});

    // 绿色透明方块
    model = modelMatrix({0.2f, -0.5f, -1.0f}, 0.5f);
    oitRenderShader_->setMat4("model", model);
    quad_->Draw(*oitRenderShader_, oitRenderFBO_,
                {{"diffuse_texture", textureWindowG_->id},
                 {"texture_depth", opaqueDepthTexture_}},
                {}, GL_TRIANGLES, {false, false});

    // 蓝色透明方块
    model = modelMatrix({0.2f, 0.0f, -0.5f}, 0.5f);
    oitRenderShader_->setMat4("model", model);
    quad_->Draw(*oitRenderShader_, oitRenderFBO_,
                {{"diffuse_texture", textureWindowB_->id},
                 {"texture_depth", opaqueDepthTexture_}},
                {}, GL_TRIANGLES, {false, false});
}
```

**注意**：三个透明方块绘制时，`oitRenderFBO_` 的深度附件是 `opaqueDepthTexture_`（与 `opaqueFBO_` 共享），而 `glDepthMask(GL_FALSE)` 确保透明物体不会修改这个深度缓冲。

### 4.3 Pass 3：合成输出

```cpp
void LinkedListOITApp::renderCompositePass() {
    // 确保 GPU 完成了 Pass 2 的所有 SSBO/Image/Atomic 写入
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                    GL_SHADER_STORAGE_BARRIER_BIT |
                    GL_ATOMIC_COUNTER_BARRIER_BIT);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    compositeShader_->use();
    // 传入不透明颜色纹理，作为混合的起始背景
    quad_->Draw(*compositeShader_, 0,  // framebuffer = 0 = 默认帧缓冲
                {{"texture_opaque", opaqueTexture_}}, {},
                GL_TRIANGLES, {true, true});
}
```

**`glMemoryBarrier` 的必要性**：Pass 2 中 Fragment Shader 写入 SSBO 和 Image Texture 的写入可能还在 GPU 缓存中，`glMemoryBarrier` 强制刷新，确保 Pass 3 读取到最新数据。没有这步会导致随机闪烁或丢失片段。

---

## 5. FBO 设计总结

本项目有两个 FBO，它们共享一个深度纹理：

```
┌─────────────────────────────────────────────────────┐
│  opaqueFBO                                          │
│  ┌─────────────────────┐  ┌───────────────────────┐ │
│  │ opaqueTexture       │  │ opaqueDepthTexture    │ │
│  │ RGBA16F (color)     │  │ DEPTH_COMPONENT32F    │ │
│  └─────────────────────┘  └───────────┬───────────┘ │
└───────────────────────────────────────┼─────────────┘
                                        │ 共享
┌───────────────────────────────────────┼─────────────┐
│  oitRenderFBO                         │             │
│  ┌─────────────────────┐             │             │
│  │ oitTexture          │  ◄──────────┘             │
│  │ RGBA16F (color)     │                            │
│  └─────────────────────┘                            │
└─────────────────────────────────────────────────────┘
```

设计意图：
- `opaqueFBO` 写入不透明物体的颜色和深度
- `oitRenderFBO` 写入透明物体的颜色，但**共享**不透明物体的深度纹理
- 这样 Pass 2 中透明物体的深度测试会比较不透明物体的深度，被遮挡的透明片段被正确丢弃
- 同时 `glDepthMask(GL_FALSE)` 确保透明物体不会污染共享的深度缓冲

---

## 6. 附：Blinn-Phong 光照 Shader

Pass 1 使用标准 Blinn-Phong 光照模型，计算代码在 `blinnPhong.frag` 中：

```glsl
#version 430 core
layout (location = 0) out vec4 FragColor;
in vec3 vertexPos;
in vec3 vertexNor;
in vec2 textureCoord;

uniform vec3 cameraPos;
uniform vec3 lightPos;
uniform vec3 k;  // k.x=环境光, k.y=漫反射, k.z=高光

void main() {
    vec3 lightColor = vec3(1.0);
    vec3 normalDir = normalize(vertexNor);
    vec3 lightDir = normalize(lightPos - vertexPos);
    vec3 viewDir = normalize(cameraPos - vertexPos);

    // 环境光
    vec3 ambient = k.x * lightColor;

    // 漫反射
    vec3 diffuse = k.y * max(dot(normalDir, lightDir), 0.0) * lightColor;

    // Blinn-Phong 高光
    vec3 halfwayDir = normalize(lightDir + viewDir);
    vec3 specular = k.z * pow(max(dot(normalDir, halfwayDir), 0.0), 2) * lightColor;

    vec3 objectColor = texture(texture_diffuse, textureCoord).xyz;
    FragColor = vec4((ambient + diffuse + specular) * objectColor, 1.0);
}
```

---

## 7. 总结

下篇逐行解析了 Linked List OIT 的核心 Shader 实现：

| 文件 | 核心功能 |
|------|---------|
| `oitRender.frag` | 头插法将透明片段插入逐像素链表，使用 `atomicCounterIncrement` 分配节点 ID，`imageAtomicExchange` 原子更新头指针 |
| `composite.frag` | 遍历链表 → 去重 → 插入排序 → Back-to-Front over 混合 |
| `blinnPhong.frag` | 标准 Blinn-Phong 光照计算不透明物体 |

以及 CPU 侧三个 Pass 的 GL 状态管理细节和 FBO 共享深度纹理的设计思路。

> 源码地址：[GitHub 仓库](https://github.com/user/OpenGL_OIT_Linked_list)（请替换为实际仓库地址）