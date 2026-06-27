#include "gl_bindless_renderer.h"
#include "logger.h"
#include <cstring>

GLBindlessRenderer& GLBindlessRenderer::instance() {
    static GLBindlessRenderer s;
    return s;
}

static const char* kVS = R"(#version 300 es
uniform vec2 u_resolution;
layout(location=0) in vec2 a_pos;
layout(location=1) in vec4 a_color;
out vec4 v_color;
void main(){
    vec2 ndc = (a_pos / u_resolution) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_color = a_color;
})";

static const char* kFS = R"(#version 300 es
precision mediump float;
in vec4 v_color;
out vec4 fragColor;
void main(){ fragColor = v_color; })";

void GLBindlessRenderer::createShader() {
    auto compile = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        return s;
    };
    GLuint vs = compile(GL_VERTEX_SHADER, kVS);
    GLuint fs = compile(GL_FRAGMENT_SHADER, kFS);
    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glLinkProgram(m_program);
    glDeleteShader(vs);
    glDeleteShader(fs);
}

void GLBindlessRenderer::init(int w, int h) {
    if (m_ready) return;
    m_screenW = w; m_screenH = h;
    createShader();
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(2*sizeof(float)));
    glBindVertexArray(0);
    m_ready = true;
    LOGI("[GLRenderer] init %dx%d", w, h);
    LOGI("[GLRenderer] init: width=%d, height=%d", w, h);
}

void GLBindlessRenderer::resize(int w, int h) {
    m_screenW = w; m_screenH = h;
}

void GLBindlessRenderer::drawLine(float x1, float y1, float x2, float y2,
                                   float r, float g, float b, float a, float) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_pending.push_back({x1, y1, r, g, b, a});
    m_pending.push_back({x2, y2, r, g, b, a});
}

void GLBindlessRenderer::drawRect(float x, float y, float w, float h,
                                    float r, float g, float b, float a, float) {
    drawLine(x, y, x+w, y, r, g, b, a);
    drawLine(x+w, y, x+w, y+h, r, g, b, a);
    drawLine(x+w, y+h, x, y+h, r, g, b, a);
    drawLine(x, y+h, x, y, r, g, b, a);
}

void GLBindlessRenderer::flush() {
    if (!m_ready) return;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_drawing.swap(m_pending);
        m_pending.clear();
    }
    if (m_drawing.empty()) return;

    // 保存 GL 状态
    GLint prevProgram, prevVao, prevVbo;
    GLboolean prevBlend, prevDepth;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevVbo);
    prevBlend = glIsEnabled(GL_BLEND);
    prevDepth = glIsEnabled(GL_DEPTH_TEST);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(m_program);
    GLint loc = glGetUniformLocation(m_program, "u_resolution");
    glUniform2f(loc, (float)m_screenW, (float)m_screenH);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, m_drawing.size()*sizeof(Vertex), m_drawing.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_LINES, 0, (GLsizei)m_drawing.size());
    glBindVertexArray(0);

    // 恢复 GL 状态
    glUseProgram(prevProgram);
    glBindVertexArray(prevVao);
    glBindBuffer(GL_ARRAY_BUFFER, prevVbo);
    if (!prevBlend) glDisable(GL_BLEND);
    if (prevDepth) glEnable(GL_DEPTH_TEST);
LOGI("[GLRenderer] flush: drawing size = %zu", m_drawing.size());

    m_drawing.clear();
}

void GLBindlessRenderer::clear() {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_pending.clear();
    m_drawing.clear();
}
