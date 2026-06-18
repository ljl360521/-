//
// 高斯模糊 - 渲染模块实现（重写版）
// GLES 3.0 双 Pass 高斯模糊
//
// 关键改进：
// 1. 用 ImDrawList::AddCallback 在 ImGui 渲染管线内执行原生 GL 绘制
//    确保在 ImGui 画窗口内容之前画模糊背景，且不被 glClear 清掉
// 2. 模糊结果纹理通过 ImGui 纹理系统绘制（AddImage），与 ImGui 完全兼容
// 3. 完整的 GL 状态保存/恢复
//
// 渲染流程：
// 1. 从 blur_capture 获取目标 App 帧缓冲（RGBA 数据）
// 2. 上传为 GL 纹理（在 ImGui 上下文）
// 3. Pass 1: 源纹理 → 水平模糊 → FBO_A
// 4. Pass 2: FBO_A → 垂直模糊 → FBO_B（最终模糊纹理）
// 5. 用 ImGui::GetBackgroundDrawList()->AddImage 画圆角矩形背景
//    （在窗口内容之下，作为背景）
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
static GLuint g_program_blur = 0;
static GLuint g_source_texture = 0;
static GLuint g_fbo[2] = {0, 0};
static GLuint g_fbo_texture[2] = {0, 0};
static int g_fbo_width = 0;
static int g_fbo_height = 0;

// shader uniform locations
static GLint g_blur_loc_sourceTex = -1;
static GLint g_blur_loc_direction = -1;
static GLint g_blur_loc_texelOffset = -1;

// ============================================================================
// Shader 源码（9-tap 高斯模糊，可分离卷积）
// ============================================================================
static const char* BLUR_VERT_SRC = R"(#version 300 es
precision highp float;
layout(location=0) in vec2 a_pos;
layout(location=1) in vec2 a_uv;
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
uniform vec2 u_texelOffset;
void main() {
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
// 确保 FBO 尺寸匹配窗口
// ============================================================================
static void ensure_fbo_size(int w, int h) {
    if (g_fbo_width == w && g_fbo_height == h && g_fbo[0] != 0) return;
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
    if (!g_program_blur) {
        LOGE("[blur_renderer] shader 初始化失败");
        return;
    }

    g_blur_loc_sourceTex = glGetUniformLocation(g_program_blur, "u_sourceTex");
    g_blur_loc_direction = glGetUniformLocation(g_program_blur, "u_direction");
    g_blur_loc_texelOffset = glGetUniformLocation(g_program_blur, "u_texelOffset");

    if (g_source_texture == 0) {
        glGenTextures(1, &g_source_texture);
        glBindTexture(GL_TEXTURE_2D, g_source_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    g_inited = true;
    LOGI("[blur_renderer] ✓ 初始化成功");
}

// ============================================================================
// 上传源纹理（从 blur_capture 获取帧）
// ============================================================================
static bool upload_source_frame() {
    int fw = 0, fh = 0;
    const uint8_t* data = blur_capture::get_latest_frame(&fw, &fh);
    if (!data || fw <= 0 || fh <= 0) return false;

    glBindTexture(GL_TEXTURE_2D, g_source_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fw, fh, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

// ============================================================================
// 执行一次模糊 Pass
// ============================================================================
static void do_blur_pass(GLuint src_tex, GLuint dst_fbo, int dst_w, int dst_h,
                         float offset_x, float offset_y) {
    glBindFramebuffer(GL_FRAMEBUFFER, dst_fbo);
    glViewport(0, 0, dst_w, dst_h);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(g_program_blur);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src_tex);
    glUniform1i(g_blur_loc_sourceTex, 0);
    glUniform2f(g_blur_loc_direction, offset_x > 0 ? 1.0f : 0.0f, offset_y > 0 ? 1.0f : 0.0f);
    glUniform2f(g_blur_loc_texelOffset, offset_x, offset_y);

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
// render - 主入口
// 关键改进：直接在 ImGui 渲染流程中调用，用 ForegroundDrawList 画结果
// ============================================================================
void render(const ImVec2& pos, const ImVec2& size, float radius, float blur_radius, bool enabled) {
    if (!g_inited || !enabled) return;

    int fw = (int)size.x;
    int fh = (int)size.y;
    if (fw <= 0 || fh <= 0) return;

    // 1. 上传最新帧到源纹理
    if (!upload_source_frame()) return;

    // 2. 确保 FBO 尺寸匹配
    ensure_fbo_size(fw, fh);

    // 3. 保存当前 GL 状态
    GLint prev_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    GLint prev_viewport[4];
    glGetIntegerv(GL_VIEWPORT, prev_viewport);
    GLint prev_program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_program);
    GLboolean prev_blend = glIsEnabled(GL_BLEND);
    GLint prev_active_tex = GL_TEXTURE0;
    glGetIntegerv(0x84E0, &prev_active_tex); // GL_ACTIVE_TEXTURE
    GLint prev_tex2d = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex2d);
    GLboolean prev_cull = glIsEnabled(GL_CULL_FACE);
    GLboolean prev_depth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prev_scissor = glIsEnabled(GL_SCISSOR_TEST);

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);

    // 4. 模糊强度 → texel offset（控制模糊半径）
    // texel_offset = (blur_radius / 10) * (1/fbo_size)
    float scale = blur_radius / 10.0f;
    float offset_x = (1.0f / fw) * scale;
    float offset_y = (1.0f / fh) * scale;

    // Pass 1: 源纹理 → 水平模糊 → FBO[0]
    do_blur_pass(g_source_texture, g_fbo[0], fw, fh, offset_x, 0.0f);
    // Pass 2: FBO[0] → 垂直模糊 → FBO[1]
    do_blur_pass(g_fbo_texture[0], g_fbo[1], fw, fh, 0.0f, offset_y);

    // 5. 恢复 GL 状态
    glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
    glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);
    glUseProgram(prev_program);
    glActiveTexture(prev_active_tex);
    glBindTexture(GL_TEXTURE_2D, prev_tex2d);
    if (prev_blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (prev_cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (prev_depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prev_scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);

    // 6. 用 ImGui 的 BackgroundDrawList 画模糊背景（圆角矩形）
    // BackgroundDrawList 在所有窗口之下，作为背景层
    // FBO[1] 的纹理作为 ImGui 纹理，画到窗口位置
    // 注意：ImTextureID 在 ImGui 1.92 是 ImU64，纹理 ID 直接转
    ImTextureID blur_tex = (ImTextureID)(intptr_t)g_fbo_texture[1];
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    // 画圆角矩形模糊背景（在窗口内容之下，作为背景层）
    // UV 的 Y 翻转：FBO 纹理原点在左下，ImGui 在左上，所以 v0=1, v1=0
    draw_list->AddImageRounded(blur_tex, pos, ImVec2(pos.x + size.x, pos.y + size.y),
                               ImVec2(0, 1), ImVec2(1, 0), IM_COL32(255, 255, 255, 255),
                               radius, ImDrawFlags_RoundCornersAll);
}

} // namespace blur_renderer
