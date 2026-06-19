#pragma once
#include <GLES3/gl3.h>
#include <vector>
#include <mutex>

class GLBindlessRenderer {
public:
    static GLBindlessRenderer& instance();

    void init(int screenW, int screenH);
    void resize(int w, int h);
    bool isReady() const { return m_ready; }
    void clear();

    void drawLine(float x1, float y1, float x2, float y2,
                  float r, float g, float b, float a, float thickness = 2.f);
    void drawRect(float x, float y, float w, float h,
                  float r, float g, float b, float a, float thickness = 2.f);

    void flush();  

private:
    GLBindlessRenderer() = default;

    struct Vertex { float x, y, r, g, b, a; };

    std::mutex m_mutex;
    std::vector<Vertex> m_pending;  
    std::vector<Vertex> m_drawing;   

    GLuint m_vao = 0, m_vbo = 0, m_program = 0;
    int m_screenW = 0, m_screenH = 0;
    bool m_ready = false;

    void createShader();
};