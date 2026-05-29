# 用 MSAA + 随机 Sample Mask 实现无序半透明渲染

> **项目**：`OpenGL_OIT_Stochastic_Transparency`  
> **关键词**：Stochastic Transparency、OIT、MSAA、`gl_SampleMask`、深度测试

---

## 摘要

传统半透明需要按深度排序再 Alpha 混合。本文说明一种 **无需排序** 的近似做法：**Stochastic Transparency**——在 MSAA 的每个子采样上按纹理 Alpha「掷骰子」，用 `gl_SampleMask` 决定写入哪些子采样，再配合 **子采样级深度测试**，最后由硬件 **Resolve** 平均得到屏幕像素。

下文聚焦 **原理、关键着色器实现**，以及本项目中 `StochasticTransparencyApp::init()` 里五行 OpenGL 状态在 **何时、何阶段** 起作用。

---

## 1. 背景：为什么需要 OIT？

**画家算法**：透明物体从远到近绘制，每片元做

$$C_{\text{final}} = C_{\text{src}} \cdot \alpha + C_{\text{dst}} \cdot (1 - \alpha)$$

痛点：排序贵、无法处理循环重叠、与深度缓冲难协作。

**OIT（Order-Independent Transparency）** 不依赖绘制顺序。本项目采用 **Stochastic Transparency**：把 Alpha 当作「每个 MSAA 子采样被保留的概率」，而非混合权重。

| 方法 | 排序 | 本项 |
|------|------|------|
| Alpha 混合 | 需要 | — |
| Stochastic Transparency | **不需要** | **采用** |

---

## 2. 原理：子采样掷骰子

核心思想（McGuire & Bavoil, HPG 2013）：**Alpha = coverage = 子采样保留概率**。

### 2.1 三步直觉

**① 每个子采样掷骰子（片元着色器）**

```
  coverage = 0.6（纹理 Alpha）示意 8 个子采样

  子采样:    s0    s1    s2    s3    s4    s5    s6    s7
  随机数 r:  0.23  0.71  0.45  0.88  0.12  0.55  0.39  0.94
  r < 0.6?   ✓     ✗     ✓     ✗     ✓     ✓     ✓     ✗
  gl_SampleMask: 1  0  1  0  1  1  1  0   → 约 60% 位为 1
```

**② 深度测试：同一子采样上，近的赢**

```
  子采样 s2：远片元 A 先写 → 近片元 B 后写
  B 深度更近 → 在 s2 上覆盖 A（无需对物体排序）
```

**③ MSAA Resolve：对「亮着」的子采样求平均 → 近似半透明**

```
  [R][--][R][--][R][R][R][--]  →  Resolve  →  约 0.5×红色
```

### 2.2 一帧内数据流（与本项目对应）

```
  initWindow: GLFW_SAMPLES=16     → 创建多采样帧缓冲（前置条件）
       ↓
  init(): 五行 GL 状态            → 整段渲染过程的全局规则（见 §4）
       ↓
  beginFrame: glClear 颜色+深度   → 每帧清空 MSAA FBO
       ↓
  renderScene: 4 次 Draw（无序）  → 片元写 gl_SampleMask + 颜色
       │                            深度测试/写入在固定管线阶段执行
       ↓
  SwapBuffers                     → MSAA Resolve → 显示器
```

---

## 3. 关键实现：片元着色器

`resources/quad.frag` 是算法核心：

```glsl
vec4 color = texture(texture_diffuse, textureCoord);
float coverage = color.w;

uint randMask = 0u;
for (int i = 0; i < sampleCnt; i++) {
    vec2 seed = vec2(i, frameID);
    float r = fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);
    if (r < coverage)
        randMask |= (1u << i);
}
gl_SampleMask[0] = int(randMask);
FragColor = color;
```

| 符号 | 来源 | 作用 |
|------|------|------|
| `coverage` | 纹理 Alpha | 伯努利试验成功概率 |
| `sampleCnt` | CPU 传 `GL_MAX_SAMPLES` | 掷骰子次数 = MSAA 采样数 |
| `frameID` | 每物体递增 `% 4` | 随机种子，避免重叠面 mask 完全相同 |
| `gl_SampleMask` | 片元输出 | 位为 1 的子采样才允许写入颜色/深度 |

CPU 侧每帧对 Spot、蓝/绿/红窗各 `Draw` 一次，**不排序**；`beginFrame` 只清一次屏，物体之间 **不清深度**（`clearColorDepth = {false,false}`），深度在四次绘制间累积。

---

## 4. 五行 GL 状态：在流程中何时、扮演什么角色

以下代码位于 `StochasticTransparencyApp::init()`，在 **首帧绘制之前调用一次**，之后 **每帧、每个片元** 的固定管线都受这些状态约束，直到被 `glDisable` 改掉（本项目不会关掉）。

```cpp
glEnable(GL_MULTISAMPLE);
glEnable(GL_SAMPLE_MASK);
glEnable(GL_DEPTH_TEST);
glDepthFunc(GL_LEQUAL);
glDepthMask(GL_TRUE);
```

另有一处 **必须先于上述状态生效** 的配置（`initWindow`）：

```cpp
glfwWindowHint(GLFW_SAMPLES, 16);  // 创建 16× MSAA 默认帧缓冲
```

没有多采样缓冲，后面五行中的「子采样」概念不存在。下文按 **OpenGL 管线时间顺序** 说明每一项。

### 4.1 总览：状态 × 管线阶段

| 状态 / 配置 | 主要生效阶段 | 一句话 |
|-------------|--------------|--------|
| `GLFW_SAMPLES=16` | 上下文/ FBO 创建 | 提供 N 个子采样槽位 |
| `GL_MULTISAMPLE` | 光栅化 → Resolve | 打开多采样路径 |
| `GL_SAMPLE_MASK` | 片元后、写入前 | 允许片元用 mask 筛子采样 |
| `GL_DEPTH_TEST` | 片元后、写入前 | 按深度决定能否写入某 sample |
| `glDepthFunc(GL_LEQUAL)` | 深度测试瞬间 | 通过条件：新深度 ≤ 旧深度 |
| `glDepthMask(GL_TRUE)` | 深度测试通过后 | 允许更新深度缓冲 |

### 4.2 `glEnable(GL_MULTISAMPLE)` —— 多采样路径的总开关

**何时设置**：`init()`，窗口已带 `GLFW_SAMPLES` 创建完毕之后。

**在哪些阶段起作用**：

1. **光栅化**：三角形覆盖一个像素时，不是只影响 1 个点，而是影响该像素的 **N 个子采样**（本项 N≤16）。
2. **片元着色**：在 MSAA 模式下，片元与 **子采样** 关联（具体是否「每子采样跑一次片元」取决于驱动与是否开启 sample shading；本 Demo 未开 `GL_SAMPLE_SHADING`，但 mask/深度仍按子采样语义工作）。
3. **写入**：颜色、深度写入 **多采样颜色/深度缓冲**（每个像素 N 份）。
4. **`SwapBuffers`（Resolve）**：硬件把 N 个子采样 **平均** 成 1 个显示像素——Stochastic Transparency 的「混合」 largely 发生在这里。

**若关闭**：退化为单采样，无法「按子采样保留/丢弃」，本算法失效。

### 4.3 `glEnable(GL_SAMPLE_MASK)` —— 允许片元改写「写入资格」

**何时设置**：`init()`，且必须在片元里写 `gl_SampleMask` **之前** 启用。

**在哪些阶段起作用**：

- 发生在 **片元着色器执行完毕之后、颜色/深度实际写入 framebuffer 之前** 的「样本遮罩」阶段。
- 片元里 `gl_SampleMask[0] = randMask`：只有 mask 中为 1 的 bit，该子采样才 **允许** 接收本片元的 `FragColor` 和深度。
- 与 `coverage` 掷骰子直接对应：**先** 用随机决定哪些 sample「有资格写」，**再** 对这些 sample 做深度测试。

**若关闭**：`gl_SampleMask` 写入被忽略，所有子采样都会尝试写入 → 半透明变成「全不透明片元」，失去随机透明度。

**依赖关系**：依赖 `GL_MULTISAMPLE`；单采样下无意义。

### 4.4 `glEnable(GL_DEPTH_TEST)` —— 子采样上的前后关系

**何时设置**：`init()`；每帧 `beginFrame` 里 **不清** 深度开关，只 `glClear(DEPTH)`。

**在哪些阶段起作用**：

- **每个片元、每个通过 Sample Mask 的子采样**，将该子采样上的片元深度与 **多采样深度缓冲** 中对应 sample 的已存深度比较。
- 本 Demo 连续画 4 个物体：**同一子采样** 上，后绘制且更近的片元可以赢；远的被挡——这是在 **子采样粒度** 实现「谁在前」，从而 **无需对网格排序**。

**与 Stochastic 的配合**：

```
  片元到达 → Sample Mask 筛 sample → 深度测试筛 sample → 通过的 sample 写颜色+深度
```

**若关闭**：所有片元都写入，远近错乱，重叠透明完全错误。

### 4.5 `glDepthFunc(GL_LEQUAL)` —— 深度比较规则

**何时设置**：`init()`，与 `GL_DEPTH_TEST` 同时生效。

**在哪些阶段起作用**：仅在 **深度测试执行的那一瞬间**。

- `GL_LEQUAL`：新片元深度 **≤** 缓冲中深度 → **通过**。
- 相等深度可通过（对共面或同一几何重复绘制更宽容）。
- 本 Demo 每帧从 `glClearDepth(1.0)` 开始，近处深度小，远处大。

**角色**：定义「什么叫 nearer」。Stochastic 只决定 **哪些 sample 参与竞争**；**谁赢** 由深度测试决定。

### 4.6 `glDepthMask(GL_TRUE)` —— 是否写入深度缓冲

**何时设置**：`init()`。

**在哪些阶段起作用**：深度测试 **通过之后** 的写入阶段。

- `GL_TRUE`：通过的子采样 **更新** 多采样深度缓冲。
- 之后同一子采样上 **更远** 的片元会因深度测试失败而无法写入颜色。

**在本项目中的角色**：使「近处透明片元占住该 sample」在 **后续 Draw** 中仍成立（四物体共用同一深度缓冲、中间不清深度）。这是 **无序绘制** 仍能近似正确的前后关系的关键之一。

**若改为 `GL_FALSE`**：只测不写，后续片元无法被挡住，多层透明叠加会乱（传统透明常在对透明 pass 关深度写，本算法路径不同）。

### 4.7 单行代码在「一帧四物体」中的时间线

```
帧开始
  glClear 颜色+深度                    ← 深度缓冲置远平面
  ─────────────────────────────────────────────────────────
  Draw Spot
    片元: 掷骰子 → gl_SampleMask       ← GL_SAMPLE_MASK + 片元 shader
          深度测试 LEQUAL              ← GL_DEPTH_TEST + glDepthFunc
          通过则写色+写深              ← GL_MULTISAMPLE 缓冲 + glDepthMask TRUE
  ─────────────────────────────────────────────────────────
  Draw 蓝窗 (frameID+1, 新随机 mask)
    同上；与 Spot 在重叠像素的同一 sample 上比深度
  ─────────────────────────────────────────────────────────
  Draw 绿窗、红窗 …
  ─────────────────────────────────────────────────────────
  SwapBuffers → MSAA Resolve           ← GL_MULTISAMPLE 解析到屏幕
帧结束
```

### 4.8 五行与着色器分工（对照表）

| 层次 | 谁负责 | 做什么 |
|------|--------|--------|
| 窗口 | `GLFW_SAMPLES` | 创建 N 子采样缓冲 |
| 全局状态 | `GL_MULTISAMPLE` | 走多采样 + Resolve |
| 全局状态 | `GL_SAMPLE_MASK` | 允许片元筛 sample |
| 片元 shader | `gl_SampleMask = randMask` | 按 Alpha 随机保留 sample |
| 全局状态 | `GL_DEPTH_TEST` + `LEQUAL` | 近的赢 |
| 全局状态 | `glDepthMask(TRUE)` | 赢的 sample 写下深度，挡住远的 |
| 硬件 | Resolve | 子采样平均 ≈ 透明感 |

---

## 5. 设计取舍（简短）

- **噪声**：采样数有限（16）会有颗粒；可时间累积或 TAA。
- **近似**：非物理精确混合；要精确需 Linked List / Depth Peeling。
- **Per-sample shading**：未显式 `glMinSampleShading(1.0)`，极端情况下驱动行为需实机验证。

---

## 6. 总结

| 问题 | 答案 |
|------|------|
| 原理是什么？ | Alpha = 子采样保留概率；mask + 深度 + Resolve |
| 关键代码在哪？ | `quad.frag` 中 `gl_SampleMask` 循环 |
| 五行 GL 状态何时设？ | `init()` 一次，作用于之后每帧整条管线 |
| 各自管什么？ | MSAA 提供 sample；MASK 筛 sample；深度测/写决定远近 |
| 为何能不排序？ | 前后关系在 **每个子采样** 上由深度解决，透明度由 **随机 mask + Resolve 平均** 近似 |

---

## 参考

- McGuire & Bavoil, *Stochastic Transparency*, HPG 2013  
- 本项目：`src/StochasticTransparencyApp.cpp`（init 53–57 行）、`resources/quad.frag`
