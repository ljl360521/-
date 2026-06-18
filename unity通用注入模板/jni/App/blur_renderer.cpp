//
// 高斯模糊 - 渲染模块实现
// GLES 3.0 双 Pass 高斯模糊（水平 + 垂直可分离卷积）
//
// 渲染流程：
// 1. 从 blur_capture 获取目标 App 帧缓冲（RGBA）
// 2. 上传为 GL 纹理（源纹理）
// 3. Pass 1: 源纹理 → 水平模糊 → FBO_A
// 4. Pass 2: FBO_A 纹理 → 垂直模糊 → FBO_B（最终模糊纹理）
// 5. 用 ImGui 的 ForegroundDrawList 画圆角矩形，纹理 = FBO_B
//
// 性能：
// - 源纹理已降采样到 1/2（blur_capture 做的）
// - FBO 尺寸 = 窗口尺寸（不存全屏，只存窗口区域）
// - 9-tap 高斯核（质量与性能平衡）
//

#include "blur_renderer.hpp"
#include "blur_capture.hpp"
#include <GLES3/gl3.h>
#include <cstring>
#include <cmath>
#include "log.h"

namespace blur_renderer {

// ============================================================================
// 状态
// ============================================================================
static bool g_inited = false;
static GLuint g_program_blur = 0;      // 模糊 shader（水平/垂直通用，用 uniform 切换方向）
static GLuint g_program_final = 0;     // 最终渲染 shader（画圆角矩形 + 纹理）
static GLuint g_source_texture = 0;    // 源纹理（目标 App 帧缓冲）
static GLuint g_fbo[2] = {0, 0};       // 双 Pass FBO
static GLuint g_fbo_texture[2] = {0, 0}; // FBO 绑定的纹理
static int g_fbo_width = 0;
static int g_fbo_height = 0;

// shader uniform locations
static GLint g_blur_loc_sourceTex = -1;
static GLint g_blur_loc_direction = -1;
static GLint g_blur_loc_texelOffset = -1;
static GLint g_final_loc_tex = -1;
static GLint g_final_loc_rectMin = -1;
static GLint g_final_loc_rectMax = -1;
static GLint g_final_loc_radius = -1;
static GLint g_final_loc_viewSize = -1;

// ============================================================================
// Shader 源码
// ============================================================================

// 9-tap 高斯模糊（可分离卷积，水平/垂直通用）
// direction: vec2(1,0)=水平, vec2(0,1)=垂直
static const char* BLUR_VERT_SRC = R"(#version 300 es
precision highp float;
layout(location=0) in vec2 a_pos;       // -1..1 全屏四边形
layout(location=1) in vec2 a_uv;        // 0..1
out vec2 v_uv;
void main() {
    v_uv = a_uv;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)";

static const char* BLUR_FRAG_SRC = R"(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 outColor;
uniform sampler2D u_sourceTex;
uniform vec2 u_direction;       // 水平(1,0) 或 垂直(0,1)
uniform vec2 u_texelOffset;     // 1/textureSize * direction
void main() {
    // 9-tap 高斯核权重（sigma≈4）
    float w[5];
    w[0] = 0.227027;
    w[1] = 0.1945946;
    w[2] = 0.1216216;
    w[3] = 0.054054;
    w[4] = 0.016216;
    vec3 sum = texture(u_sourceTex, v_uv).rgb * w[0];
    for (int i = 1; i < 5; i++) {
        sum += texture(u_sourceTex, v_uv + u_texelOffset * float(i)).rgb * w[i];
        sum += texture(u_sourceTex, v_uv - u_texelOffset * float(i)).rgb * w[i];
    }
    outColor = vec4(sum, 1.0);
}
)";

// 最终渲染：画圆角矩形，采样模糊纹理
// 使用 SDF 计算圆角
static const char* FINAL_VERT_SRC = R"(#version 300 es
precision highp float;
layout(location=0) in vec2 a_pos;       // 屏幕坐标
layout(location=1) in vec2 a_uv;        // 0..1
out vec2 v_uv;
out vec2 v_screenPos;
uniform vec2 u_viewSize;
void main() {
    v_uv = a_uv;
    v_screenPos = a_pos;
    // 屏幕坐标 → NDC（Y 翻转）
    vec2 ndc = vec2(
        (a_pos.x / u_viewSize.x) * 2.0 - 1.0,
        1.0 - (a_pos.y / u_viewSize.y) * 2.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

static const char* FINAL_FRAG_SRC = R"(#version 300 es
precision highp float;
in vec2 v_uv;
in vec2 v_screenPos;
out vec4 outColor;
uniform sampler2D u_tex;
uniform vec2 u_rectMin;
uniform vec2 u_rectMax;
uniform float u_radius;
void main() {
    // SDF 圆角矩形
    vec2 p = v_screenPos;
    vec2 half_size = (u_rectMax - u_rectMin) * 0.5;
    vec2 center = (u_rectMin + u_rectMax) * 0.5;
    vec2 d = abs(p - center) - half_size + vec2(u_radius);
    float dist = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - u_radius;
    // dist < 0 在矩形内，> 0 在外
    // 抗锯齿边缘
    float alpha = 1.0 - smoothstep(-1.0, 1.0, dist);
    vec4 texColor = texture(u_tex, v_uv);
    outColor = vec4(texColor.rgb, alpha);
}
)";

// ============================================================================
// 工具：编译 shader
// ============================================================================
static GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        LOGE("[blur_renderer] shader compile failed: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint link_program(const char* vert, const char* frag) {
    GLuint v = compile_shader(GL_VERTEX_SHADER, vert);
    GLuint f = compile_shader(GL_FRAGMENT_SHADER, frag);
    if (!v || !f) { if (v) glDeleteShader(v); if (f) glDeleteShader(f); return 0; }
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        LOGE("[blur_renderer] program link failed: %s", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

// ============================================================================
// 创建 FBO + 纹理
// ============================================================================
static void create_fbo(GLuint* fbo, GLuint* tex, int w, int h) {
    glGenFramebuffers(1, fbo);
    glGenTextures(1, tex);
    glBindTexture(GL_TEXTURE_2D, *tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ============================================================================
// 确保源纹理存在
// ============================================================================
static void ensure_source_texture() {
    if (g_source_texture == 0) {
        glGenTextures(1, &g_source_texture);
        glBindTexture(GL_TEXTURE_2D, g_source_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

// ============================================================================
// 确保 FBO 尺寸匹配窗口
// ============================================================================
static void ensure_fbo_size(int w, int h) {
    if (g_fbo_width == w && g_fbo_height == h && g_fbo[0] != 0) return;
    // 重建
    if (g_fbo[0]) { glDeleteFramebuffers(1, &g_fbo[0]); glDeleteTextures(1, &g_fbo_texture[0]); }
    if (g_fbo[1]) { glDeleteFramebuffers(1, &g_fbo[1]); glDeleteTextures(1, &g_fbo_texture[1]); }
    create_fbo(&g_fbo[0], &g_fbo_texture[0], w, h);
    create_fbo(&g_fbo[1], &g_fbo_texture[1], w, h);
    g_fbo_width = w;
    g_fbo_height = h;
    LOGI("[blur_renderer] FBO 重建: %dx%d", w, h);
}

// ============================================================================
// 全屏四边形顶点
// ============================================================================
static const float QUAD_VERTS[] = {
    // pos(-1..1)    uv(0..1)
    -1.f, -1.f,  0.f, 0.f,
     1.f, -1.f,  1.f, 0.f,
    -1.f,  1.f,  0.f, 1.f,
     1.f,  1.f,  1.f, 1.f,
};
static const unsigned short QUAD_INDICES[] = { 0, 1, 2, 2, 1, 3 };

// ============================================================================
// init
// ============================================================================
void init() {
    if (g_inited) return;

    g_program_blur = link_program(BLUR_VERT_SRC, BLUR_FRAG_SRC);
    g_program_final = link_program(FINAL_VERT_SRC, FINAL_FRAG_SRC);
    if (!g_program_blur || !g_program_final) {
        LOGE("[blur_renderer] shader 初始化失败");
        return;
    }

    g_blur_loc_sourceTex = glGetUniformLocation(g_program_blur, "u_sourceTex");
    g_blur_loc_direction = glGetUniformLocation(g_program_blur, "u_direction");
    g_blur_loc_texelOffset = glGetUniformLocation(g_program_blur, "u_texelOffset");

    g_final_loc_tex = glGetUniformLocation(g_program_final, "u_tex");
    g_final_loc_rectMin = glGetUniformLocation(g_program_final, "u_rectMin");
    g_final_loc_rectMax = glGetUniformLocation(g_program_final, "u_rectMax");
    g_final_loc_radius = glGetUniformLocation(g_program_final, "u_radius");
    g_final_loc_viewSize = glGetUniformLocation(g_program_final, "u_viewSize");

    ensure_source_texture();
    g_inited = true;
    LOGI("[blur_renderer] ✓ 初始化成功");
}

// ============================================================================
// 上传源纹理（从 blur_capture 获取帧）
// ============================================================================
static void upload_source_frame() {
    int fw = 0, fh = 0;
    const uint8_t* data = blur_capture::get_latest_frame(&fw, &fh);
    if (!data || fw <= 0 || fh <= 0) return;

    glBindTexture(GL_TEXTURE_2D, g_source_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fw, fh, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ============================================================================
// 执行一次模糊 Pass
// ============================================================================
static void do_blur_pass(GLuint src_tex, GLuint dst_fbo, int dst_w, int dst_h,
                         float dir_x, float dir_y, float texel_scale) {
    glBindFramebuffer(GL_FRAMEBUFFER, dst_fbo);
    glViewport(0, 0, dst_w, dst_h);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(g_program_blur);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src_tex);
    glUniform1i(g_blur_loc_sourceTex, 0);
    glUniform2f(g_blur_loc_direction, dir_x, dir_y);
    // texelOffset = (1/texSize) * direction * texel_scale
    float ox = (dir_x > 0 ? 1.0f / dst_w : 0.0f) * texel_scale;
    float oy = (dir_y > 0 ? 1.0f / dst_h : 0.0f) * texel_scale;
    glUniform2f(g_blur_loc_texelOffset, ox, oy);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, QUAD_VERTS);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, QUAD_VERTS + 2);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, QUAD_INDICES);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ============================================================================
// 渲染最终圆角矩形
// ============================================================================
static void render_final(GLuint tex, const ImVec2& pos, const ImVec2& size, float radius,
                         int view_w, int view_h) {
    // 构建圆角矩形四边形（屏幕坐标）
    float x1 = pos.x, y1 = pos.y;
    float x2 = pos.x + size.x, y2 = pos.y + size.y;
    float verts[] = {
        x1, y1, 0.f, 0.f,
        x2, y1, 1.f, 0.f,
        x1, y2, 0.f, 1.f,
        x2, y2, 1.f, 1.f,
    };

    glUseProgram(g_program_final);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(g_final_loc_tex, 0);
    glUniform2f(g_final_loc_rectMin, x1, y1);
    glUniform2f(g_final_loc_rectMax, x2, y2);
    glUniform1f(g_final_loc_radius, radius);
    glUniform2f(g_final_loc_viewSize, (float)view_w, (float)view_h);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, verts);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, verts + 2);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, QUAD_INDICES);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);

    glDisable(GL_BLEND);
}

// ============================================================================
// render - 主入口
// ============================================================================
void render(const ImVec2& pos, const ImVec2& size, float radius, float blur_radius, bool enabled) {
    if (!g_inited || !enabled) return;

    // 获取视口尺寸
    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    int view_w = viewport[2];
    int view_h = viewport[3];
    if (view_w <= 0 || view_h <= 0) return;

    // 窗口尺寸（FBO 尺寸）
    int fw = (int)size.x;
    int fh = (int)size.y;
    if (fw <= 0 || fh <= 0) return;

    // 上传最新帧到源纹理
    upload_source_frame();
    if (g_source_texture == 0) return;

    // 确保 FBO 尺寸匹配
    ensure_fbo_size(fw, fh);

    // 保存当前 GL 状态
    GLint prev_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    GLint prev_viewport[4];
    glGetIntegerv(GL_VIEWPORT, prev_viewport);
    GLint prev_program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_program);
    GLboolean prev_blend = glIsEnabled(GL_BLEND);
    GLint prev_blend_src, prev_blend_dst;
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prev_blend_src);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prev_blend_dst);

    // Pass 1: 源纹理 → 水平模糊 → FBO[0]
    // 源纹理尺寸是降采样的，但 FBO 是窗口尺寸，UV 映射会自动处理
    // texel_scale 控制模糊强度（blur_radius / 10）
    float texel_scale = blur_radius / 10.0f;
    do_blur_pass(g_source_texture, g_fbo[0], fw, fh, 1.0f, 0.0f, texel_scale);

    // Pass 2: FBO[0] 纹理 → 垂直模糊 → FBO[1]
    do_blur_pass(g_fbo_texture[0], g_fbo[1], fw, fh, 0.0f, 1.0f, texel_scale);

    // 恢复视口
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

    // 最终渲染：用 FBO[1] 纹理画圆角矩形
    render_final(g_fbo_texture[1], pos, size, radius, view_w, view_h);

    // 恢复 GL 状态
    glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
    glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);
    glUseProgram(prev_program);
    if (prev_blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFunc(prev_blend_src, prev_blend_dst);
}

} // namespace blur_renderer
