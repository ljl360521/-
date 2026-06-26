#pragma once
#include <GLES3/gl3.h>
#include <vector>
#include <mutex>

// 轻量 GL 线条/图元绘制器，不依赖 ImGui
class GLBindlessRenderer {
public:
    static GLBindlessRenderer& instance();

    void init(int screenW, int screenH);  // 首次在 GL 线程调用
    void resize(int w, int h);
    bool isReady() const { return m_ready; }
    void clear();

    // 线程安全：游戏线程提交，GL 线程消费
    void drawLine(float x1, float y1, float x2, float y2,
                  float r, float g, float b, float a, float thickness = 2.f);
    void drawRect(float x, float y, float w, float h,
                  float r, float g, float b, float a, float thickness = 2.f);

    void flush();  // 在 eglSwapBuffers hook 里调用，真正提交 GL

private:
    GLBindlessRenderer() = default;

    struct Vertex { float x, y, r, g, b, a; };

    std::mutex m_mutex;
    std::vector<Vertex> m_pending;   // 提交缓冲
    std::vector<Vertex> m_drawing;   // 当前帧绘制

    GLuint m_vao = 0, m_vbo = 0, m_program = 0;
    int m_screenW = 0, m_screenH = 0;
    bool m_ready = false;

    void createShader();
};