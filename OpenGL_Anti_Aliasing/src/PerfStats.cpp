#include "PerfStats.hpp"

#include <GLFW/glfw3.h>

// 创建 GL_TIME_ELAPSED 双缓冲 query
void GpuTimer::init() {
    glGenQueries(kQueryCount, queries_);
}

void GpuTimer::shutdown() {
    if (queries_[0]) {
        glDeleteQueries(kQueryCount, queries_);
        queries_[0] = 0;
        queries_[1] = 0;
    }
}

void GpuTimer::begin() {
    if (!enabled_ || !queries_[0]) {
        return;
    }
    glBeginQuery(GL_TIME_ELAPSED, queries_[writeIndex_]);
    active_ = true;
}

// 结束 query 并读取上一帧 GPU 耗时（毫秒），避免同步 stall
float GpuTimer::endMs() {
    if (!enabled_ || !queries_[0] || !active_) {
        return lastMs_;
    }
    glEndQuery(GL_TIME_ELAPSED);
    active_ = false;

    // Read the previous frame's query without blocking; avoids CPU/GPU pipeline stalls.
    const int readIndex = 1 - writeIndex_;
    GLint available = 0;
    glGetQueryObjectiv(queries_[readIndex], GL_QUERY_RESULT_AVAILABLE, &available);
    if (available) {
        GLuint64 elapsed = 0;
        glGetQueryObjectui64v(queries_[readIndex], GL_QUERY_RESULT, &elapsed);
        lastMs_ = static_cast<float>(elapsed) / 1000000.0f;
    }

    writeIndex_ = readIndex;
    return lastMs_;
}

void PerfStats::beginFrame() {
    lastFrameTime_ = glfwGetTime();
}

// 由 glfwGetTime 计算帧时间，并指数平滑 FPS
// 新 FPS = 90% × 上一帧平滑值 + 10% × 本帧瞬时值
// 相当于对 FPS 做低通滤波：标题栏不会每帧乱跳，但会稍滞后于真实帧率变化。
void PerfStats::endFrame() {
    const double now = glfwGetTime();
    const float frameMs = static_cast<float>((now - lastFrameTime_) * 1000.0);
    latest_.totalFrameMs = frameMs;
    const float instantFps = frameMs > 0.0f ? 1000.0f / frameMs : 0.0f;
    smoothedFps_ = smoothedFps_ <= 0.0f ? instantFps : smoothedFps_ * 0.9f + instantFps * 0.1f;
    latest_.fps = smoothedFps_;
}
