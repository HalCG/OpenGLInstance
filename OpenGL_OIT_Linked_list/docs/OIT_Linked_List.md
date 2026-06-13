# OIT Linked List 项目问答复习笔记

> 本文档整理自对 `OpenGL_OIT_Linked_list` 项目的学习问答，便于后续复习与查阅。  
> 对应源码：`LinkedListOITApp.cpp`、`resources/OpenGL_OIT_Linked_list/*.frag`

---

## 目录

1. [头指针纹理：`glTexStorage2D`](#1-头指针纹理gltexstorage2d)
2. [Image 绑定：`glBindImageTexture`](#2-image-绑定glbindimagetexture)
3. [OIT FBO 初始化收尾（249–256 行）](#3-oit-fbo-初始化收尾249256-行)
4. [光照：半透明物体与世界包围盒](#4-光照半透明物体与世界包围盒)
5. [Memory Barrier：`GL_SHADER_IMAGE_ACCESS_BARRIER_BIT`](#5-memory-barriergl_shader_image_access_barrier_bit)
6. [SSBO 是什么？还能用在哪里？](#6-ssbo-是什么还能用在哪里)
7. [GLSL 原子函数是内置的吗？](#7-glsl-原子函数是内置的吗)
8. [SSBO 节点写入：需要 new Node 吗？](#8-ssbo-节点写入需要-new-node-吗)

---

## 1. 头指针纹理：`glTexStorage2D`

**代码位置：** `LinkedListOITApp.cpp` 第 179 行

```cpp
glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32UI, width_, height_);
```

### 在做什么

为 **Linked List OIT** 分配 **头指针纹理（head pointer texture）** 的不可变存储。

| 参数 | 值 | 含义 |
|------|-----|------|
| `target` | `GL_TEXTURE_2D` | 2D 纹理 |
| `levels` | `1` | 仅 1 级 mipmap |
| `internalformat` | `GL_R32UI` | 单通道 32 位无符号整数 |
| `width` / `height` | `width_`, `height_` | 与窗口同分辨率 |

与 `glTexImage2D` 不同，`glTexStorage2D` **固定**纹理尺寸和格式，之后不可再改，但可用 `glTexSubImage2D` 写入数据。

### 在 OIT 中的作用

- 每个屏幕像素对应一个 `uint32`
- 存储该像素透明片元链表的 **头节点索引**（指向 `linkedListBuffer_`）
- 空链表哨兵值为 `0xffffffff`（见初始化 PBO 清空逻辑）

### 数据流（简化）

```
屏幕像素 (x, y)
    ↓
headPtrTexture_[x,y]  →  链表头 index（uint）
    ↓
linkedListBuffer_     →  节点 { color, depth, next }
    ↓
atomicBuffer_         →  全局节点计数器
```

---

## 2. Image 绑定：`glBindImageTexture`

**代码位置：** `LinkedListOITApp.cpp` 第 180 行

```cpp
glBindImageTexture(0, headPtrTexture_, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
```

### 在做什么

将 `headPtrTexture_` 绑定到 **Image Unit 0**，使 shader 能以 **`uimage2D`** 方式随机读写，而非普通 `sampler2D` 采样。

### 与普通纹理的区别

| | `sampler2D` | `uimage2D` |
|---|---|---|
| 访问 | `texture(uv)`，可过滤 | `imageLoad(ivec2)`，按像素精确访问 |
| 写入 | 一般只读 | `imageStore` / `imageAtomic*` |
| 原子操作 | 不支持 | 支持 |
| 用途 | 贴图 | 数据结构（链表头指针） |

### 参数对应

| 参数 | 值 | 含义 |
|------|-----|------|
| unit | `0` | 对应 shader `layout(binding = 0)` |
| texture | `headPtrTexture_` | 头指针纹理 |
| level | `0` | mipmap 层级 |
| access | `GL_READ_WRITE` | shader 可读可写 |
| format | `GL_R32UI` | 按 R32UI 解释 |

### Shader 侧

```glsl
layout(binding = 0, r32ui) uniform uimage2D headPointers;
uint preHead = imageAtomicExchange(headPointers, ivec2(gl_FragCoord.xy), newIndex);
```

`binding = 0` 在 atomic counter、SSBO 上也会出现，但 **不同类型有独立 binding 空间**，互不冲突。

---

## 3. OIT FBO 初始化收尾（249–256 行）

**代码位置：** `LinkedListOITApp.cpp` `initFramebuffers()` 末尾

```cpp
GLenum drawBuffersOIT[] = {GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT};
glDrawBuffers(2, drawBuffersOIT);
if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) ...
glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
glBindFramebuffer(GL_FRAMEBUFFER, 0);
```

### 上下文

`oitRenderFBO_` 是 **Pass 2（透明物体）** 的渲染目标：

| 附件 | 纹理 | 作用 |
|------|------|------|
| `GL_COLOR_ATTACHMENT0` | `oitTexture_` | 颜色输出（RGBA16F） |
| `GL_DEPTH_ATTACHMENT` | `opaqueDepthTexture_` | **复用** Pass 1 深度，做深度测试 |

### 逐行说明

- **`glDrawBuffers`**：指定哪些 **颜色附件** 接收片元颜色。通常只需 `{GL_COLOR_ATTACHMENT0}`。深度写入由 `GL_DEPTH_ATTACHMENT` 绑定 + 深度测试完成，不应放进 `glDrawBuffers`（此处写法与 opaque FBO 类似，属常见复制粘贴问题，多数驱动仍能工作）。
- **`glCheckFramebufferStatus`**：检查 FBO 是否完整。
- **`glClear`**：清空 FBO 初始内容。
- **`glBindFramebuffer(0)`**：解绑，回到默认帧缓冲。

---

## 4. 光照：半透明物体与世界包围盒

### 半透明物体：**没有做光照**

CPU 侧在 `renderTransparentPass()` 传了 `lightPos`、`cameraPos`、`k`，但 `oitRender.frag` **未使用**：

```glsl
vec4 color = texture(texture_diffuse, textureCoord);
nodes[atomic_buffer].color = color;  // 直接存贴图色
```

Pass 3 `composite.frag` 也只做 alpha 混合，不重新打光。

| 物体 | Pass | 光照 |
|------|------|------|
| spot 牛（不透明） | Pass 1 | Blinn-Phong ✓ |
| 红/绿/蓝透明 quad | Pass 2 | 仅贴图 ✗ |
| 背景 | Pass 3 | 来自 opaqueTexture，非天空盒 |

### 世界包围盒 / 天空盒：**未渲染**

- 项目有 `SkyBox.hpp`，但 `LinkedListOITApp` **未使用**
- `quadShader_` 已加载但 **从未调用**
- 背景是不透明 FBO 内容 + 清屏色，不是环境贴图天空盒

> 本项目侧重 OIT 算法演示，Pass 2 简化了光照。若需透明物体受光，应在 `oitRender.frag` 写入链表前计算 Blinn-Phong，或在 `composite.frag` 混合时重新着色。

---

## 5. Memory Barrier：`GL_SHADER_IMAGE_ACCESS_BARRIER_BIT`

**代码位置：** `renderCompositePass()` Pass 3 开始前

```cpp
glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                GL_SHADER_STORAGE_BARRIER_BIT |
                GL_ATOMIC_COUNTER_BARRIER_BIT);
```

### 三个 bit 各对应什么

| Barrier bit | 内存类型 | 本项目对象 | Pass 2 写 | Pass 3 读 |
|-------------|----------|------------|-----------|-----------|
| `GL_SHADER_IMAGE_ACCESS_BARRIER_BIT` | Image Texture | `headPtrTexture_` | `imageAtomicExchange` | `imageLoad` |
| `GL_SHADER_STORAGE_BARRIER_BIT` | SSBO | `linkedListBuffer_` | `nodes[i] = ...` | 遍历 `nodes[]` |
| `GL_ATOMIC_COUNTER_BARRIER_BIT` | Atomic Counter | `atomicBuffer_` | `atomicCounterIncrement` | （Pass 3 通常只读 SSBO/image） |

### 为什么需要

Pass 2 与 Pass 3 是不同 draw call，GPU 可能并行。不加 barrier，Pass 3 可能读到 Pass 2 尚未写完的数据，链表遍历会出错。

---

## 6. SSBO 是什么？还能用在哪里？

### 定义

**SSBO（Shader Storage Buffer Object）** 是 OpenGL 4.3+ 的 **GPU 可读写的通用大缓冲区**，绑定 `GL_SHADER_STORAGE_BUFFER`，GLSL 中用 `layout(std430) buffer` 声明。

本项目：

```cpp
glBufferData(GL_SHADER_STORAGE_BUFFER, maxNodes_ * nodeSize, ...);
```

```glsl
layout(binding = 0, std430) buffer linkedLists { NodeType nodes[]; };
```

### 与其他缓冲对比

| 类型 | 用途 | Shader | 大小 | 原子 |
|------|------|--------|------|------|
| UBO | 相机、光照参数 | 只读 | 较小 | ✗ |
| SSBO | 大量动态数据 | 读写 | 很大 | ✓ |
| Atomic Counter | 计数器 | 原子增减 | 很小 | ✓ |
| Image Texture | 按像素读写 | 2D 网格 | 纹理尺寸 | ✓ |

### 常见应用场景

- **OIT 链表节点**（本项目）
- Compute Shader：粒子、物理、图像处理
- 实例化：`mat4` 变换数组
- Clustered / Tiled Lighting 光源列表
- GPU 剔除、间接绘制命令缓冲
- GPU 哈希表、排序、BVH 等数据结构

### 注意事项

1. Pass 间读写需 `glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT)`
2. 并发写同一位置需原子操作，或像本项目用 counter 分配唯一 index
3. CPU `ListNode` 与 GLSL `NodeType` 必须 **内存对齐一致**（`std430`）

---

## 7. GLSL 原子函数是内置的吗？

**代码位置：** `oitRender.frag` 第 44、48 行

```glsl
uint atomic_buffer = atomicCounterIncrement(nextNodeCounter);
uint preHead = imageAtomicExchange(headPointers, ivec2(gl_FragCoord.xy), atomic_buffer);
```

### 结论

- **是 GLSL 语言内置函数**（规范定义，无需自己实现）
- **不是** OpenGL C API（不是 `glXxx`）
- 由 **GPU 硬件原子单元** 执行，驱动负责编译

### 分层理解

```
oitRender.frag 调用 GLSL 内置函数
    → 驱动/编译器
    → GPU 原子指令
    → 显存中的 buffer / texture
```

### CPU 侧前置条件

| Shader | CPU 绑定 |
|--------|----------|
| `atomic_uint nextNodeCounter` | `glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, atomicBuffer_)` |
| `uimage2D headPointers` | `glBindImageTexture(0, headPtrTexture_, ...)` |

### 版本要求

- `atomicCounterIncrement`：OpenGL **4.2+**
- `imageAtomicExchange`：OpenGL **4.2+**
- 本项目 `#version 430 core`，完全支持

---

## 8. SSBO 节点写入：需要 new Node 吗？

**代码位置：** `oitRender.frag` 第 53–55 行

```glsl
nodes[atomic_buffer].color = color;
nodes[atomic_buffer].depth = gl_FragCoord.z;
nodes[atomic_buffer].next = preHead;
```

### 不需要 `new Node()`，也没有空指针

GPU 侧是 **预分配数组 + 整数索引**，不是 CPU 堆上的指针链表。

### 初始化（对象池）

```cpp
maxNodes_ = width_ * height_ * 20;
glBufferData(GL_SHADER_STORAGE_BUFFER, maxNodes_ * nodeSize, ...);
```

等价于：

```cpp
Node nodes[maxNodes_];  // 整块内存，槽位已全部存在
```

### 分配流程

| 步骤 | 机制 | 说明 |
|------|------|------|
| 预分配 | `glBufferData` | 创建 `maxNodes_` 个槽位 |
| 领编号 | `atomicCounterIncrement` | 返回 0, 1, 2, … 唯一 index |
| 越界保护 | `if (atomic_buffer < MaxNodes)` | 池满则丢弃 |
| 写数据 | `nodes[atomic_buffer] = ...` | 填第 N 个槽 |
| 串链表 | `next = preHead` | **uint 索引**，非指针 |

### 为何不怕空指针

1. GLSL **没有指针**；`next` 是 `uint`，链表结束用 `0xffffffff`
2. `atomic_buffer < MaxNodes` 保证不越界
3. 每个片元通过 counter 拿到 **不同 index**，写冲突少
4. 头指针在 image texture 中，用 `imageAtomicExchange` 原子更新

### 真正需要担心的

| 风险 | 处理 |
|------|------|
| 节点池满 | `atomic_buffer < MaxNodes` |
| Pass 间不同步 | `glMemoryBarrier` |
| struct 对齐 | CPU/GPU 结构体一致 |
| 同像素并发 | `imageAtomicExchange` |

---

## 附录：三 Pass 与缓冲对照

```
Pass 1  opaqueFBO          Blinn-Phong → opaqueTexture + opaqueDepthTexture
Pass 2  oitRenderFBO        oitRender.frag → SSBO 链表 + headPtrTexture + atomicBuffer
Pass 3  默认帧缓冲         composite.frag → 读链表排序混合 + opaqueTexture
```

| 缓冲 | 类型 | Shader 变量 | 主要操作 |
|------|------|-------------|----------|
| `atomicBuffer_` | Atomic Counter | `atomic_uint nextNodeCounter` | Pass 2 递增分配 index |
| `linkedListBuffer_` | SSBO | `nodes[]` | Pass 2 写，Pass 3 读 |
| `headPtrTexture_` | Image (R32UI) | `uimage2D headPointers` | Pass 2 原子交换，Pass 3 imageLoad |
| `opaqueDepthTexture_` | 深度纹理 | `sampler2D texture_depth` | Pass 2 深度遮挡测试 |

---

## 相关文档

- [OIT_Linked_List_上篇_原理与缓冲区设计.md](./OIT_Linked_List_上篇_原理与缓冲区设计.md)
- [OIT_Linked_List_下篇_Shader与渲染Pass实现.md](./OIT_Linked_List_下篇_Shader与渲染Pass实现.md)
