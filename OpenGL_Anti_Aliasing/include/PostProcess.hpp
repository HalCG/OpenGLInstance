#ifndef POST_PROCESS_HPP
#define POST_PROCESS_HPP

#include <glad/glad.h>

// 后处理工具：全屏 blit、FXAA、MSAA resolve
class PostProcess {
public:
    bool init();    // 创建全屏 quad 与 blit/fxaa shader
    void shutdown();
    void resize(int width, int height); // 更新 uTexelSize 等依赖分辨率的状态

    void blitTexture(GLuint colorTexture) const;                          // 直通 blit 到默认 framebuffer
    void applyFxaa(GLuint colorTexture) const;                            // FXAA 全屏 pass 输出到屏幕
    void resolveMsaaToScreen(GLuint msaaFbo, int width, int height) const; // 从 MSAA FBO resolve 到屏幕

private:
    void drawFullscreen() const; // 绘制覆盖 NDC [-1,1] 的全屏三角形/四边形

    GLuint quadVao_ = 0;
    GLuint quadVbo_ = 0;
    GLuint blitShader_ = 0;
    GLuint fxaaShader_ = 0;
    mutable GLint blitLocInput_ = -1;
    mutable GLint fxaaLocInput_ = -1;
    mutable GLint fxaaLocTexelSize_ = -1;
    int width_ = 0;
    int height_ = 0;
};

#endif
