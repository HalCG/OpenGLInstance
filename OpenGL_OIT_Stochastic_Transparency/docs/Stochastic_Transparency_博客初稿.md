# 用 MSAA + 随机 Sample Mask 实现无序半透明渲染：Stochastic Transparency 实践笔记

> **项目仓库**：`OpenGL_OIT_Stochastic_Transparency`  
> **技术栈**：OpenGL 4.3、GLFW、GLAD、Assimp、GLM  
> **关键词**：OIT、Order-Independent Transparency、Stochastic Transparency、MSAA、`gl_SampleMask`

---

## 摘要

半透明物体若使用传统的 Alpha 混合，通常需要按深度从远到近排序，否则叠加结果会错误。当场景中存在大量交叉重叠的透明面时，排序成本高，且与延迟渲染、早期深度优化等管线设计相冲突。

本文记录一个基于 **Stochastic Transparency（随机透明度）** 思路的 OpenGL 教学 Demo：在 **16× MSAA** 下，片元着色器根据纹理 Alpha 对每个子采样做随机保留/丢弃，通过 `gl_SampleMask` 控制写入，再配合深度测试，在 **无需对透明物体排序** 的前提下，近似得到正确的叠加效果。

演示场景包含一个 Spot 模型与红/绿/蓝三块半透明「窗户」平面，故意在空间上交叉摆放，可用键盘旋转相机观察效果。

---

## 1. 为什么半透明这么麻烦？

### 1.1 画家算法与 Alpha 混合

经典半透明渲染流程：

1. 绘制所有不透明物体（开启深度测试）。
2. 将透明物体按与相机的距离 **从远到近排序**。
3. 关闭深度写入（或仅测试），按顺序做 **Alpha 混合**：

$$
C_{\text{final}} = C_{\text{src}} \cdot \alpha + C_{\text{dst}} \cdot (1 - \alpha)
$$

问题在于：

- **排序本身昂贵**：对象多、三角面多、动态场景时要每帧排序。
- **无法处理「循环重叠」**：A 透过 B，B 又透过 A 时，不存在全局正确排序。
- **与深度缓冲的协作困难**：透明片元若随意写深度，会挡住本应先绘制的更远透明层。

### 1.2 OIT：不排序还能画对？

**Order-Independent Transparency（OIT，无序透明）** 指一类 **不依赖物体绘制顺序** 的透明渲染技术，常见代表包括：

| 方法 | 思路 | 典型代价 |
|------|------|----------|
| Depth Peeling | 多 Pass 逐层剥离最近表面 | Pass 数 = 层数 |
| Weighted Blended OIT | 单 Pass 加权近似混合 | 近似，有亮度偏差 |
| Linked List OIT | 每像素链表存储所有片元 | 显存 + 原子操作 |
| **Stochastic Transparency** | 用 MSAA 子采样 + 随机 mask 近似 | 噪声，受采样数限制 |

本项目实现的是最后一类：**实现短、概念清晰，非常适合学习和写 Demo**。

---

## 2. Stochastic Transparency 在做什么？

核心思想来自 McGuire & Bavoil 等工作（如 HPG 2013 *Stochastic Transparency*）：**不要把 Alpha 当作「混合比例」，而当作「该子采样被保留的概率」**。

### 2.1 直觉图示：子采样掷骰子
# Stochastic Transparency（精简解读）

本文仅保留原理与关键实现，剔除工程与构建细节。目标：快速理解如何用 MSAA + `gl_SampleMask` 实现无需排序的近似透明混合，以及程序中哪些 GL 状态在何阶段起作用。

## 要点速览
- 思路：把纹理 alpha 视为「每个 MSAA 子采样被保留的概率」，在片元着色器为每个子采样做一次伯努利试验（确定性伪随机），用 `gl_SampleMask` 标记要写入的子采样，借助子采样级深度测试与硬件 Resolve 得到最终像素颜色。无需对透明对象排序。
- 依赖：MSAA（多子采样）、`gl_SampleMask`（片元控制写入）、子采样级深度测试、硬件 Resolve（平均子采样颜色）。

## 核心片元逻辑（伪代码）

```glsl
vec4 color = texture(texture_diffuse, uv);
float coverage = color.a; // alpha
uint mask = 0u;
for (int i = 0; i < sampleCnt; ++i) {
  float r = hash(float(i), float(frameID));
  if (r < coverage) mask |= (1u << i);
}
gl_SampleMask[0] = int(mask);
FragColor = color;
```

说明：`sampleCnt` 通常取 `GL_MAX_SAMPLES`；`frameID` 用作种子以避免不同对象/帧重用相同随机图案。

## 为什么这能工作（简述）
- 每个像素在 MSAA 下有 N 个子采样。对每个子采样独立决定是否由当前片元“占领”。
- 若某子采样被保留并通过深度测试，则写入该 sample 的颜色与深度；更远片元在该 sample 上被挡住。最终 Resolve 对亮着的子采样平均，近似恢复 alpha 所期望的透明度。

## 在代码中传递了哪些参数（关键）
- `sampleCnt`：片元循环的上限，通常由 CPU 从 `GL_MAX_SAMPLES` 读取并传给 shader。决定随机掷骰子的分辨率（采样数越多，近似越平滑）。
- `frameID`：随机种子的一部分，每个物体绘制时递增（或与物体索引相关），用于避免重叠物体在相同帧使用完全相同的随机掩码，减少系统性条纹。

## `src/StochasticTransparencyApp.cpp` 中的五行 GL 状态（详述）
代码位置：[src/StochasticTransparencyApp.cpp](src/StochasticTransparencyApp.cpp#L53-L57)

- `glEnable(GL_MULTISAMPLE)`：启用 MSAA，多子采样是本方法的基础；没有多采样，就无法在子采样粒度上选择保留/丢弃。
- `glEnable(GL_SAMPLE_MASK)`：允许片元着色器写入 `gl_SampleMask`。写入掩码只有在启用此状态时才会生效。
- `glEnable(GL_DEPTH_TEST)`：开启深度测试。配合多采样时，深度测试会在子采样级别决定哪个片元赢得该 sample 的写入权。
- `glDepthFunc(GL_LEQUAL)`：深度比较函数（<=）。深度测试在写入前进行，用于判定是否通过并写入颜色/深度。
- `glDepthMask(GL_TRUE)`：允许写入深度缓冲（每个通过的 sample 会写入其深度）。在本 Demo 中这是必要选择：成功写入深度会阻止更远的片元在同一 sample 上写入，从而实现近处优先的局部遮挡语义。

这些调用在初始化阶段设置全局行为，之后每帧的片元着色器通过写 `gl_SampleMask` 决定当次片元哪些 sample 可以写入，随后深度测试与深度写入按上述状态在 sample 级别执行。

## 设计权衡与建议（简短）
- 噪声：采样数有限会出现颗粒；可用更多 MSAA、时间累积或 TAA 来平滑。
- 精确度：为教学型近似法，若需精确混合请使用 Linked List OIT 或 Depth Peeling。
- 兼容性：不同驱动对 per-sample shading 行为可能不同，必要时启用 `GL_SAMPLE_SHADING`（`glMinSampleShading(1.0f)`）。
