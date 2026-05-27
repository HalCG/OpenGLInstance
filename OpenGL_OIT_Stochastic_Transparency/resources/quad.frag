#version 420 core
out vec4 FragColor;
// out int gl_SampleMask[];  // sample mask 输出
in vec2 textureCoord;

uniform int frameID; // 随机数种子
uniform int sampleCnt; // 最大的 sample 数
uniform sampler2D texture_diffuse;
void main() { 
    vec4 color = texture(texture_diffuse, textureCoord);
    
    uint randMask = 0u;
    float coverage = color.w;
    for(int i=0; i<sampleCnt; i++){
        // 简单随机数生成
        vec2 seed = vec2(i, frameID);
        float r = fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);
        if (r < coverage) {
            randMask |= (1u << i); // 保留 sample
        }

    }
    gl_SampleMask[0] = int(randMask);  // 写入 sample mask
    FragColor = color;
}

/*
fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);，这行代码**每次只生成一个随机数**。

## 随机数范围

范围是 **`[0.0, 1.0)`**（大于等于 0，小于 1）。

因为 `fract()` 取的是小数部分，而 `sin()` 输出范围是 `[-1, 1]`，乘以一个大的常数后取小数，结果始终落在 `[0, 1)` 之间。

## 为什么说"仅生成一个随机数"

这行代码是**确定性伪随机**，对同一个 `seed` 输入，输出总是相同的值。严格来说它不生成"多个"随机数，而是**由输入 seed 决定的一个确定输出**。

如果你需要多个随机数，通常是通过改变 `seed` 来实现。以 GLSL 中常见的模式为例：

```glsl
// 输入一个二维坐标，输出一个随机值
float random(vec2 st) {
    return fract(sin(dot(st, vec2(12.9898, 78.233))) * 43758.5453);
}

// 用不同的输入得到不同的"随机"值
float r1 = random(gl_FragCoord.xy);       // 基于像素坐标
float r2 = random(gl_FragCoord.xy + 1.0); // 偏移后得到另一个值
float r3 = random(gl_FragCoord.yx);       // 交换坐标得到另一个值
```

所以如果你看到这段代码在 Shader 里逐像素执行，那每个像素都会算出一个属于它的随机数（像素之间不同），但单个像素的这行代码自己只产生一个值。
*/