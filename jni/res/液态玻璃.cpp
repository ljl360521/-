// ==================== 液态玻璃（Apple Liquid Glass）展示标签页 ====================
//
// 设计要点（与本工程既有约束对齐）：
//  1) 本 ImGui 覆盖层是盖在游戏之上的独立透明 GLSurfaceView，GL 上下文读不到游戏画面像素，
//     所以“折射的素材”由本标签页自己画：一层流动的彩色渐变 + 缓慢移动的光斑 + 细网格。
//  2) 自定义 GL 绘制通过 ImDrawList::AddCallback 插进 ImGui 的绘制流程；
//     回调内完整保存/恢复 GL 状态，回调后再补一个 ImDrawCallback_ResetRenderState
//     让预编译后端把自己那份（拿不到句柄的临时 VAO/uniform）也还原回去。
//  3) 对“当前绑定的是哪个 framebuffer”保持中立：防录屏会把 UI 渲到另一个 EGL surface，
//     ImGenie 捕获会渲到离屏 FBO，因此一律 glGetIntegerv 查询后原样绑回，绝不硬编码 0；
//     尺寸一律取 glGetIntegerv(GL_VIEWPORT)，不用 screenWidth/screenHeight。
//  4) 回调一帧可能被执行 2~3 次（离屏捕获 + 主 surface / secure surface），因此回调必须幂等：
//     动画时间在 UI 构建期算好，经 userdata 传入，回调内绝不推进任何动画状态。
//  5) 着色器源码里只允许 ASCII（GLSL ES 的字符集不含中文，部分驱动会因注释里的中文直接编译失败），
//     中文解释一律写在 C++ 侧注释里。
//
// 降级策略：
//  - 着色器编译/链接失败 -> 记一次日志并永久停用 GL 路径，标签页退化成纯 ImDrawList 的
//    半透明圆角矩形（依然可交互，不会黑屏、不会崩）。
//  - 只是离屏背景 FBO 创建失败 -> 玻璃着色器改走“程序化重算背景 + Vogel 盘多抽样”分支，
//    视觉几乎一致，只是模糊开销略高。
//  - EGL 上下文重建 -> 句柄整体作废重建，且绝不对已死上下文调用 glDelete*。

#define IMGUI_DEFINE_MATH_OPERATORS
#include "液态玻璃.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include <android/log.h>

#include <cmath>
#include <cstring>
#include <type_traits>

#define LG_LOG_TAG "LiquidGlass"

namespace {

// ============================================================================
//  一、着色器源码（纯 ASCII）
// ============================================================================

// 共用顶点着色器：不需要 VBO，用 gl_VertexID 生成一个四边形（TRIANGLE_STRIP 4 顶点）。
// uRectPx 是目标矩形的“GL 窗口坐标”(x, y, w, h)，y 向上；
// uViewport 是当前 glViewport 的 (x, y, w, h)，用它把窗口坐标换算到 NDC，
// 这样即使 viewport 原点不是 (0,0) 也完全正确。
static const char* kQuadVS =
    "#version 300 es\n"
    "precision highp float;\n"
    "uniform vec4 uRectPx;\n"
    "uniform vec4 uViewport;\n"
    "void main()\n"
    "{\n"
    "    vec2 c = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));\n"
    "    vec2 px = uRectPx.xy + c * uRectPx.zw;\n"
    "    gl_Position = vec4((px - uViewport.xy) / max(uViewport.zw, vec2(1.0)) * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

// 动态背景函数：被“背景程序”和“玻璃程序”共同包含（玻璃程序在没有背景纹理时用它现场重算）。
// 输入 uv 是背景矩形内的归一化坐标（y 向上），aspect 用于让光斑保持圆形。
static const char* kBackgroundGLSL =
    "vec3 LG_Blob(vec2 q, vec2 c, float r, vec3 col)\n"
    "{\n"
    "    vec2 v = q - c;\n"
    "    return col * exp(-dot(v, v) / max(r * r, 1e-5));\n"
    "}\n"
    "\n"
    "vec3 LG_Background(vec2 uv, float aspect, float t)\n"
    "{\n"
    "    vec2 q = vec2(uv.x * aspect, uv.y);\n"
    "\n"
    "    float g = uv.x * 0.35 + uv.y * 0.55\n"
    "            + 0.16 * sin(uv.x * 2.7 + t * 0.23)\n"
    "            + 0.12 * cos(uv.y * 3.3 - t * 0.19);\n"
    "    g = clamp(g, 0.0, 1.0);\n"
    "    vec3 col = mix(vec3(0.014, 0.026, 0.060), vec3(0.055, 0.175, 0.315), g);\n"
    "    col = mix(col, vec3(0.115, 0.050, 0.205), 0.35 * smoothstep(0.10, 0.95, 1.0 - g));\n"
    "\n"
    "    float a0 = t * 0.17, a1 = t * 0.13, a2 = t * 0.21, a3 = t * 0.11, a4 = t * 0.27;\n"
    "    col += LG_Blob(q, vec2(0.28 * aspect + 0.16 * cos(a0),       0.30 + 0.13 * sin(a0 * 1.3)), 0.30, vec3(0.09, 0.40, 0.70));\n"
    "    col += LG_Blob(q, vec2(0.72 * aspect + 0.13 * sin(a1 * 1.7), 0.68 + 0.15 * cos(a1)),       0.26, vec3(0.34, 0.11, 0.56));\n"
    "    col += LG_Blob(q, vec2(0.52 * aspect + 0.20 * sin(a2),       0.22 + 0.10 * cos(a2 * 0.8)), 0.20, vec3(0.05, 0.44, 0.43));\n"
    "    col += LG_Blob(q, vec2(0.14 * aspect + 0.10 * cos(a3 * 1.9), 0.80 + 0.09 * sin(a3)),       0.22, vec3(0.42, 0.19, 0.29));\n"
    "    col += LG_Blob(q, vec2(0.88 * aspect + 0.09 * sin(a4),       0.16 + 0.08 * cos(a4 * 1.4)), 0.17, vec3(0.15, 0.32, 0.64));\n"
    "\n"
    "    float band = uv.y - 0.52 - 0.17 * sin(uv.x * 3.4 + t * 0.37) - 0.06 * cos(uv.x * 7.1 - t * 0.21);\n"
    "    col += vec3(0.10, 0.30, 0.42) * exp(-band * band * 42.0);\n"
    "\n"
    "    vec2 gw = abs(fract(q * 9.0) - 0.5);\n"
    "    float grid = 1.0 - smoothstep(0.012, 0.055, min(gw.x, gw.y));\n"
    "    col += vec3(0.05, 0.14, 0.20) * grid * 0.55;\n"
    "\n"
    "    vec2 vg = uv - 0.5;\n"
    "    col *= 1.0 - 0.55 * dot(vg, vg);\n"
    "    return max(col, vec3(0.0));\n"
    "}\n";

// 背景程序的片元着色器头 + 主体。
static const char* kBgFSHead =
    "#version 300 es\n"
    "precision highp float;\n"
    "uniform vec4  uRectPx;\n"
    "uniform vec4  uViewport;\n"
    "uniform float uAspect;\n"
    "uniform float uTime;\n"
    "out vec4 FragColor;\n";

static const char* kBgFSBody =
    "void main()\n"
    "{\n"
    "    vec2 uv = (gl_FragCoord.xy - uRectPx.xy) / max(uRectPx.zw, vec2(1.0));\n"
    "    FragColor = vec4(LG_Background(uv, uAspect, uTime), 1.0);\n"
    "}\n";

// 玻璃程序的片元着色器头。
// 位置数学必须 highp：1080p/2K 的像素坐标 mediump 存不下。
static const char* kGlassFSHead =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "precision mediump sampler2D;\n"
    "\n"
    "uniform vec4  uRectPx;    // quad in GL window coords (unused in FS, kept for VS)\n"
    "uniform vec4  uViewport;  // x, y, w, h\n"
    "uniform vec4  uBgRect;    // background rect in GL window coords\n"
    "uniform vec4  uBgInfo;    // x = has texture, y = texel-per-pixel, zw = half texel\n"
    "uniform float uAspect;\n"
    "uniform float uTime;\n"
    "uniform vec4  uShape;     // xy = center (y-down), zw = half size\n"
    "uniform vec4  uParamsA;   // radius, bevel, refract, blur\n"
    "uniform vec4  uParamsB;   // specular, saturation, brightness, dispersion\n"
    "uniform vec4  uParamsC;   // wobble, press, hover, maxLod\n"
    "uniform vec4  uShadow;    // offX, offY, radius, gain\n"
    "uniform vec4  uTint;      // rgb + amount\n"
    "uniform sampler2D uBgTex;\n"
    "out vec4 FragColor;\n";

// 玻璃程序主体：
//  - 圆角矩形 / squircle SDF（Lp 范数，n = 4，苹果的连续曲率手感）
//  - 基于 SDF 梯度（= 形状外法线）的折射位移，厚度剖面取球冠 -> 位移闭式解 (1-t)^1.5
//  - 边缘色散（R/B 位移比 G 大/小一点点，中心位移恒为 0，所以色散自动只出现在边缘）
//  - 背景多抽样模糊：有背景纹理走 mip + 4 tap 旋转四边形，没有就走 10 tap Vogel 盘现算
//  - 动态镜面高光：光向缓慢旋转 + 沿轮廓滑行的亮斑，上缘亮下缘暗
//  - 饱和度/亮度提升（Lift 用 (1-c) 加权，暗部抬得多、亮部不过曝）
//  - 内侧发丝亮边 + 下缘内阴影 + 与 squircle 同形状的柔和外投影
//  - fwidth 边缘抗锯齿（求导写在任何提前返回之前，保证导数处于一致控制流）
static const char* kGlassFSBody =
    "const vec3  LG_LUMA = vec3(0.2126, 0.7152, 0.0722);\n"
    "const float LG_TAU  = 6.28318530718;\n"
    "const float LG_GOLD = 2.39996323;\n"
    "\n"
    "vec2 LG_ToWin(vec2 pixDown)\n"
    "{\n"
    "    return vec2(pixDown.x + uViewport.x, uViewport.y + uViewport.w - pixDown.y);\n"
    "}\n"
    "\n"
    "vec2 LG_BgUV(vec2 pixDown)\n"
    "{\n"
    "    return (LG_ToWin(pixDown) - uBgRect.xy) / max(uBgRect.zw, vec2(1.0));\n"
    "}\n"
    "\n"
    "float LG_Lod(float radiusPx)\n"
    "{\n"
    "    return clamp(log2(max(radiusPx * uBgInfo.y, 1.0)) - 0.5, 0.0, uParamsC.w);\n"
    "}\n"
    "\n"
    "vec3 LG_Sample1(vec2 pixDown, float lod)\n"
    "{\n"
    "    vec2 uv = LG_BgUV(pixDown);\n"
    "    if (uBgInfo.x > 0.5)\n"
    "        return textureLod(uBgTex, clamp(uv, uBgInfo.zw, vec2(1.0) - uBgInfo.zw), lod).rgb;\n"
    "    return LG_Background(uv, uAspect, uTime);\n"
    "}\n"
    "\n"
    "vec3 LG_SampleBlur(vec2 pixDown, float radiusPx, float rot)\n"
    "{\n"
    "    if (radiusPx < 0.75)\n"
    "        return LG_Sample1(pixDown, 0.0);\n"
    "\n"
    "    if (uBgInfo.x > 0.5) {\n"
    "        float lod = LG_Lod(radiusPx);\n"
    "        float rr  = radiusPx * 0.45;\n"
    "        vec3  acc = LG_Sample1(pixDown, lod) * 2.0;\n"
    "        for (int i = 0; i < 4; ++i) {\n"
    "            float a = rot + float(i) * 1.57079633;\n"
    "            acc += LG_Sample1(pixDown + vec2(cos(a), sin(a)) * rr, lod);\n"
    "        }\n"
    "        return acc * (1.0 / 6.0);\n"
    "    }\n"
    "\n"
    "    vec3  acc  = vec3(0.0);\n"
    "    float wsum = 0.0;\n"
    "    for (int i = 0; i < 10; ++i) {\n"
    "        float fi = float(i) + 0.5;\n"
    "        float rn = sqrt(fi * 0.1);\n"
    "        float a  = fi * LG_GOLD + rot;\n"
    "        float w  = exp(-2.0 * rn * rn);\n"
    "        acc  += LG_Background(LG_BgUV(pixDown + vec2(cos(a), sin(a)) * rn * radiusPx), uAspect, uTime) * w;\n"
    "        wsum += w;\n"
    "    }\n"
    "    return acc / max(wsum, 1e-4);\n"
    "}\n"
    "\n"
    "float LG_Ign(vec2 f)\n"
    "{\n"
    "    return fract(52.9829189 * fract(dot(f, vec2(0.06711056, 0.00583715))));\n"
    "}\n"
    "\n"
    "float LG_LpNorm(vec2 m)\n"
    "{\n"
    "    vec2 m2 = m * m;\n"
    "    return sqrt(sqrt(dot(m2, m2)));\n"
    "}\n"
    "\n"
    "float LG_Sd(vec2 p, vec2 b, float r)\n"
    "{\n"
    "    vec2 q = abs(p) - b + r;\n"
    "    return min(max(q.x, q.y), 0.0) + LG_LpNorm(max(q, vec2(0.0))) - r;\n"
    "}\n"
    "\n"
    "vec2 LG_SdGrad(vec2 p, vec2 b, float r)\n"
    "{\n"
    "    float dx = LG_Sd(p + vec2(1.0, 0.0), b, r) - LG_Sd(p - vec2(1.0, 0.0), b, r);\n"
    "    float dy = LG_Sd(p + vec2(0.0, 1.0), b, r) - LG_Sd(p - vec2(0.0, 1.0), b, r);\n"
    "    return vec2(dx, dy) * 0.5;\n"
    "}\n"
    "\n"
    "void main()\n"
    "{\n"
    "    vec2 pix = vec2(gl_FragCoord.x - uViewport.x, uViewport.y + uViewport.w - gl_FragCoord.y);\n"
    "    vec2 p   = pix - uShape.xy;\n"
    "\n"
    "    float press = clamp(uParamsC.y, 0.0, 1.0);\n"
    "    float hover = clamp(uParamsC.z, 0.0, 1.0);\n"
    "\n"
    "    float wob = uParamsC.x * (1.0 + 0.8 * press);\n"
    "    if (wob > 0.0) {\n"
    "        p += wob * vec2(sin(p.y * 0.014 + uTime * 0.9),\n"
    "                        cos(p.x * 0.011 - uTime * 0.7));\n"
    "    }\n"
    "\n"
    "    vec2  b   = max(uShape.zw, vec2(1.0));\n"
    "    float rad = min(max(uParamsA.x, 0.0), min(b.x, b.y));\n"
    "    float d0  = LG_Sd(p, b, rad);\n"
    "\n"
    "    vec2  g  = LG_SdGrad(p, b, rad);\n"
    "    float gn = max(length(g), 1e-4);\n"
    "    vec2  N  = g / gn;\n"
    "    float d  = d0 / gn;\n"
    "\n"
    "    float aa = clamp(fwidth(d), 0.5, 2.0);\n"
    "\n"
    "    float shadowExtent = uShadow.z + abs(uShadow.x) + abs(uShadow.y) + 2.0;\n"
    "    if (d > shadowExtent) {\n"
    "        FragColor = vec4(0.0);\n"
    "        return;\n"
    "    }\n"
    "\n"
    "    float inside = 1.0 - smoothstep(-aa * 0.5, aa * 0.5, d);\n"
    "\n"
    "    float bevel = max(uParamsA.y, 1.0);\n"
    "    float t     = clamp(-d / bevel, 0.0, 1.0);\n"
    "    float omt   = 1.0 - t;\n"
    "    float thick = sqrt(max(1.0 - omt * omt, 0.0));\n"
    "    float rim   = omt * sqrt(omt);\n"
    "\n"
    "    float disp = uParamsA.z * (1.0 + 0.35 * press) * rim;\n"
    "    vec2  base = mix(pix, uShape.xy, 0.02);\n"
    "    vec2  pG   = base - N * disp;\n"
    "\n"
    "    float blurR = uParamsA.w * mix(0.12, 1.0, thick);\n"
    "    float rot   = LG_Ign(gl_FragCoord.xy) * LG_TAU;\n"
    "\n"
    "    vec3 col = LG_SampleBlur(pG, blurR, rot);\n"
    "\n"
    "    if (disp > 0.5 && uParamsB.w > 0.001) {\n"
    "        float lod = LG_Lod(blurR);\n"
    "        col.r = LG_Sample1(base - N * (disp * (1.0 + uParamsB.w)), lod).r;\n"
    "        col.b = LG_Sample1(base - N * (disp * (1.0 - uParamsB.w)), lod).b;\n"
    "    }\n"
    "\n"
    "    float luma = dot(col, LG_LUMA);\n"
    "    col = mix(vec3(luma), col, uParamsB.y);\n"
    "    col = col * uParamsB.z;\n"
    "    col = col + 0.09 * (1.0 - col);\n"
    "    col = mix(col, uTint.rgb, clamp(uTint.a, 0.0, 1.0));\n"
    "\n"
    "    vec3  n3  = normalize(vec3(N * rim * 1.4, 1.0));\n"
    "    float ang = -1.57079633 + uTime * 0.15;\n"
    "    vec3  L   = normalize(vec3(cos(ang), sin(ang), 0.55));\n"
    "    vec3  H   = normalize(L + vec3(0.0, 0.0, 1.0));\n"
    "    float spc = pow(max(dot(n3, H), 0.0), 24.0);\n"
    "\n"
    "    float vert   = -N.y;\n"
    "    float updown = mix(0.18, 1.0, vert * 0.5 + 0.5);\n"
    "    float phi    = atan(N.y, N.x);\n"
    "    float trav   = 0.5 + 0.5 * cos(phi - uTime * 0.6);\n"
    "    float glint  = mix(0.25, 1.0, trav * trav * trav);\n"
    "\n"
    "    float specular = spc * uParamsB.x * (1.0 + 0.85 * press + 0.30 * hover) * updown * glint * rim;\n"
    "\n"
    "    float border = clamp(1.0 - smoothstep(0.0, 1.8, -d), 0.0, 1.0);\n"
    "    border = border * sqrt(border) * (0.30 + 0.28 * hover + 0.55 * press) * updown * mix(0.55, 1.0, glint);\n"
    "\n"
    "    float innerSh = (1.0 - smoothstep(0.0, bevel * 1.5, -d)) * max(-vert, 0.0);\n"
    "    col *= 1.0 - 0.22 * innerSh;\n"
    "\n"
    "    col += vec3(specular + border);\n"
    "    col += vec3(0.02, 0.05, 0.07) * hover + vec3(0.05, 0.09, 0.12) * press;\n"
    "\n"
    "    float ds = LG_Sd(p - uShadow.xy, b, rad) / gn;\n"
    "    float sh = (1.0 - smoothstep(-uShadow.z * 0.25, uShadow.z, ds)) * clamp(uShadow.w, 0.0, 1.0);\n"
    "    sh *= 1.0 - inside;\n"
    "\n"
    "    col += (LG_Ign(gl_FragCoord.xy + vec2(uTime * 61.0)) - 0.5) * (1.0 / 255.0);\n"
    "\n"
    "    float outA = clamp(inside + sh * (1.0 - inside), 0.0, 1.0);\n"
    "    vec3  outRGB = vec3(0.0);\n"
    "    if (outA > 0.0005)\n"
    "        outRGB = clamp(col * inside / outA, 0.0, 1.0);\n"
    "    FragColor = vec4(outRGB, outA);\n"
    "}\n";

// ============================================================================
//  二、GL 资源（惰性创建、可安全重复初始化、EGL 上下文换了就整体作废）
// ============================================================================

struct 背景程序位置 { GLint rect = -1, viewport = -1, aspect = -1, time = -1; };
struct 玻璃程序位置 {
    GLint rect = -1, viewport = -1, bgRect = -1, bgInfo = -1, aspect = -1, time = -1;
    GLint shape = -1, pa = -1, pb = -1, pc = -1, shadow = -1, tint = -1, tex = -1;
};

struct 液态玻璃GL {
    EGLContext owner = EGL_NO_CONTEXT;   // 这批 GL 对象所属的 EGL 上下文
    GLuint progBg    = 0;
    GLuint progGlass = 0;
    GLuint vao       = 0;                // 空 VAO：不开任何 attribute，避免污染 ImGui 的临时 VAO
    GLuint fbo       = 0;
    GLuint tex       = 0;
    int    texW      = 0;
    int    texH      = 0;
    float  maxLod    = 0.0f;
    bool   shaderDead = false;           // 着色器编译/链接失败：永久停用 GL 路径，不再每帧重试
    bool   fboDead    = false;           // 背景 FBO 失败：玻璃改走程序化重算背景分支
    bool   bgReady    = false;           // 本次 RenderDrawData 中背景纹理是否已刷新
    背景程序位置 bg;
    玻璃程序位置 gs;
};

static 液态玻璃GL g_gl;

// 是否走 GL 真玻璃（供构建期决定要不要额外画降级层的提示文字用）
static bool g_gl_available = true;

static GLuint 编译着色器(GLenum type, const char* const* srcs, int count, const char* name)
{
    GLuint sh = glCreateShader(type);
    if (sh == 0) return 0;
    glShaderSource(sh, (GLsizei)count, srcs, nullptr);
    glCompileShader(sh);

    GLint ok = GL_FALSE;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (ok == GL_FALSE) {
        char log[1024];
        GLsizei len = 0;
        glGetShaderInfoLog(sh, (GLsizei)sizeof(log) - 1, &len, log);
        if (len < 0) len = 0;
        if (len > (GLsizei)sizeof(log) - 1) len = (GLsizei)sizeof(log) - 1;
        log[len] = '\0';
        __android_log_print(ANDROID_LOG_ERROR, LG_LOG_TAG, "compile %s failed: %s", name, log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static GLuint 链接程序(const char* const* fsParts, int fsCount, const char* name)
{
    const char* vsParts[1] = { kQuadVS };
    GLuint vs = 编译着色器(GL_VERTEX_SHADER, vsParts, 1, "quad.vert");
    if (vs == 0) return 0;

    GLuint fs = 编译着色器(GL_FRAGMENT_SHADER, fsParts, fsCount, name);
    if (fs == 0) { glDeleteShader(vs); return 0; }

    GLuint prog = glCreateProgram();
    if (prog == 0) { glDeleteShader(vs); glDeleteShader(fs); return 0; }
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLint ok = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (ok == GL_FALSE) {
        char log[1024];
        GLsizei len = 0;
        glGetProgramInfoLog(prog, (GLsizei)sizeof(log) - 1, &len, log);
        if (len < 0) len = 0;
        if (len > (GLsizei)sizeof(log) - 1) len = (GLsizei)sizeof(log) - 1;
        log[len] = '\0';
        __android_log_print(ANDROID_LOG_ERROR, LG_LOG_TAG, "link %s failed: %s", name, log);
        glDeleteProgram(prog);
        prog = 0;
    }
    // 程序链接完成后 shader 对象即可释放（引用计数交给 program 持有）
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

// 惰性创建两个 program 和一个空 VAO。返回 false 表示这一帧走降级路径。
static bool 确保程序就绪()
{
    EGLContext cur = eglGetCurrentContext();
    if (cur == EGL_NO_CONTEXT) return false;

    if (g_gl.owner != cur) {
        // 上下文换了（Surface 重建 / 应用恢复）：旧句柄全部作废。
        // 绝不在这里调用 glDelete*，旧上下文已经销毁，删它是未定义行为。
        g_gl = 液态玻璃GL{};
        g_gl.owner = cur;
    }
    if (g_gl.shaderDead) return false;

    // 注意：这里刻意不做 glIsProgram 之类的存活校验。
    // 回调一帧会跑 2~3 次、一帧又有十几次回调，每次都查驱动太贵；
    // 而句柄失效的真实成因只有“EGL 上下文换了”，上面那次比对已经覆盖。

    if (g_gl.progBg == 0) {
        const char* parts[3] = { kBgFSHead, kBackgroundGLSL, kBgFSBody };
        g_gl.progBg = 链接程序(parts, 3, "background.frag");
        if (g_gl.progBg == 0) { g_gl.shaderDead = true; g_gl_available = false; return false; }
        g_gl.bg.rect     = glGetUniformLocation(g_gl.progBg, "uRectPx");
        g_gl.bg.viewport = glGetUniformLocation(g_gl.progBg, "uViewport");
        g_gl.bg.aspect   = glGetUniformLocation(g_gl.progBg, "uAspect");
        g_gl.bg.time     = glGetUniformLocation(g_gl.progBg, "uTime");
    }

    if (g_gl.progGlass == 0) {
        const char* parts[3] = { kGlassFSHead, kBackgroundGLSL, kGlassFSBody };
        g_gl.progGlass = 链接程序(parts, 3, "glass.frag");
        if (g_gl.progGlass == 0) { g_gl.shaderDead = true; g_gl_available = false; return false; }
        g_gl.gs.rect     = glGetUniformLocation(g_gl.progGlass, "uRectPx");
        g_gl.gs.viewport = glGetUniformLocation(g_gl.progGlass, "uViewport");
        g_gl.gs.bgRect   = glGetUniformLocation(g_gl.progGlass, "uBgRect");
        g_gl.gs.bgInfo   = glGetUniformLocation(g_gl.progGlass, "uBgInfo");
        g_gl.gs.aspect   = glGetUniformLocation(g_gl.progGlass, "uAspect");
        g_gl.gs.time     = glGetUniformLocation(g_gl.progGlass, "uTime");
        g_gl.gs.shape    = glGetUniformLocation(g_gl.progGlass, "uShape");
        g_gl.gs.pa       = glGetUniformLocation(g_gl.progGlass, "uParamsA");
        g_gl.gs.pb       = glGetUniformLocation(g_gl.progGlass, "uParamsB");
        g_gl.gs.pc       = glGetUniformLocation(g_gl.progGlass, "uParamsC");
        g_gl.gs.shadow   = glGetUniformLocation(g_gl.progGlass, "uShadow");
        g_gl.gs.tint     = glGetUniformLocation(g_gl.progGlass, "uTint");
        g_gl.gs.tex      = glGetUniformLocation(g_gl.progGlass, "uBgTex");
    }

    if (g_gl.vao == 0) {
        glGenVertexArrays(1, &g_gl.vao);
        if (g_gl.vao == 0) { g_gl.shaderDead = true; g_gl_available = false; return false; }
    }

    g_gl_available = true;
    return true;
}

// 按需创建/调整离屏背景纹理（半分辨率 + mipmap）。失败时置 fboDead，玻璃改走程序化背景。
// 注意：调用方必须已经保存过 FBO 绑定，本函数结束时不负责还原。
static bool 确保背景纹理(int wantW, int wantH)
{
    if (g_gl.fboDead) return false;
    if (wantW < 8) wantW = 8;
    if (wantH < 8) wantH = 8;
    if (wantW > 1024) wantW = 1024;
    if (wantH > 1024) wantH = 1024;

    if (g_gl.tex != 0 && glIsTexture(g_gl.tex) == GL_FALSE) { g_gl.tex = 0; g_gl.fbo = 0; }

    if (g_gl.tex != 0 && g_gl.texW == wantW && g_gl.texH == wantH && g_gl.fbo != 0)
        return true;

    if (g_gl.fbo != 0) { glDeleteFramebuffers(1, &g_gl.fbo); g_gl.fbo = 0; }
    if (g_gl.tex != 0) { glDeleteTextures(1, &g_gl.tex);     g_gl.tex = 0; }

    glGenTextures(1, &g_gl.tex);
    if (g_gl.tex == 0) { g_gl.fboDead = true; return false; }
    glBindTexture(GL_TEXTURE_2D, g_gl.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, wantW, wantH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &g_gl.fbo);
    if (g_gl.fbo == 0) {
        glDeleteTextures(1, &g_gl.tex); g_gl.tex = 0;
        g_gl.fboDead = true;
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, g_gl.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_gl.tex, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        __android_log_print(ANDROID_LOG_ERROR, LG_LOG_TAG,
                            "background FBO incomplete: 0x%x, fallback to procedural background", status);
        glDeleteFramebuffers(1, &g_gl.fbo); g_gl.fbo = 0;
        glDeleteTextures(1, &g_gl.tex);     g_gl.tex = 0;
        g_gl.fboDead = true;
        return false;
    }

    g_gl.texW = wantW;
    g_gl.texH = wantH;
    int m = (wantW > wantH) ? wantW : wantH;
    float lod = 0.0f;
    while (m > 1) { m >>= 1; lod += 1.0f; }
    g_gl.maxLod = lod;
    return true;
}

// ============================================================================
//  三、GL 状态守卫（照 ImGenie捕获.cpp 的严谨风格，只是更完整）
// ============================================================================

struct GL状态守卫 {
    GLint drawFbo = 0, readFbo = 0;
    GLint program = 0, vao = 0, arrayBuf = 0;
    GLint activeTex = 0, tex2d = 0, sampler = 0;
    GLint viewport[4]   = { 0, 0, 0, 0 };
    GLint scissorBox[4] = { 0, 0, 0, 0 };
    GLint bSrcRGB = 0, bDstRGB = 0, bSrcA = 0, bDstA = 0, bEqRGB = 0, bEqA = 0;
    GLboolean colorMask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
    GLboolean depthMask = GL_TRUE;
    GLboolean enBlend = GL_FALSE, enCull = GL_FALSE, enDepth = GL_FALSE;
    GLboolean enStencil = GL_FALSE, enScissor = GL_FALSE, enDither = GL_FALSE;
    GLint unpackAlign = 4;

    void 保存()
    {
        // framebuffer 一定要查询后原样绑回：防录屏会渲到 secure surface，
        // ImGenie 捕获会渲到离屏 FBO，硬编码 0 会让 UI 直接消失。
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
        glGetIntegerv(GL_CURRENT_PROGRAM,          &program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING,     &vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING,     &arrayBuf);
        // GL_ELEMENT_ARRAY_BUFFER_BINDING 属于 VAO 状态，还原 VAO 时自动带回来，无需单独保存。
        glGetIntegerv(GL_ACTIVE_TEXTURE,           &activeTex);
        glGetIntegerv(GL_VIEWPORT,     viewport);
        glGetIntegerv(GL_SCISSOR_BOX,  scissorBox);
        glGetIntegerv(GL_BLEND_SRC_RGB,        &bSrcRGB);
        glGetIntegerv(GL_BLEND_DST_RGB,        &bDstRGB);
        glGetIntegerv(GL_BLEND_SRC_ALPHA,      &bSrcA);
        glGetIntegerv(GL_BLEND_DST_ALPHA,      &bDstA);
        glGetIntegerv(GL_BLEND_EQUATION_RGB,   &bEqRGB);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &bEqA);
        glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        enBlend   = glIsEnabled(GL_BLEND);
        enCull    = glIsEnabled(GL_CULL_FACE);
        enDepth   = glIsEnabled(GL_DEPTH_TEST);
        enStencil = glIsEnabled(GL_STENCIL_TEST);
        enScissor = glIsEnabled(GL_SCISSOR_TEST);
        enDither  = glIsEnabled(GL_DITHER);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlign);

        // 我们只用 0 号纹理单元：先切到 0 号再记录它的绑定，这样恢复的正是我们污染过的那个。
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex2d);
        glGetIntegerv(GL_SAMPLER_BINDING,    &sampler);
    }

    void 恢复()
    {
        glBindTexture(GL_TEXTURE_2D, (GLuint)tex2d);
        glBindSampler(0, (GLuint)sampler);
        glActiveTexture((GLenum)activeTex);   // ResetRenderState 不管这个，必须自己还

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)drawFbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)readFbo);
        glUseProgram((GLuint)program);
        glBindVertexArray((GLuint)vao);
        glBindBuffer(GL_ARRAY_BUFFER, (GLuint)arrayBuf);

        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glScissor(scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]);
        glBlendEquationSeparate((GLenum)bEqRGB, (GLenum)bEqA);
        glBlendFuncSeparate((GLenum)bSrcRGB, (GLenum)bDstRGB, (GLenum)bSrcA, (GLenum)bDstA);
        glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
        glDepthMask(depthMask);

        设置开关(GL_BLEND,        enBlend);
        设置开关(GL_CULL_FACE,    enCull);
        设置开关(GL_DEPTH_TEST,   enDepth);
        设置开关(GL_STENCIL_TEST, enStencil);
        设置开关(GL_SCISSOR_TEST, enScissor);
        设置开关(GL_DITHER,       enDither);
        glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlign);
    }

    static void 设置开关(GLenum cap, GLboolean on)
    {
        if (on != GL_FALSE) glEnable(cap); else glDisable(cap);
    }
};

// ============================================================================
//  四、回调参数（POD，会被 ImGui memcpy 进 drawlist 内部缓冲）
// ============================================================================

enum { LG_PASS_BG = 0, LG_PASS_GLASS = 1 };

struct 玻璃参数 {
    float clipOff[2];   // draw_data->DisplayPos
    float bgRect[4];    // 动态背景覆盖矩形（ImGui 坐标，已减 DisplayPos）
    float quad[4];      // 本次绘制的四边形（ImGui 坐标，已减 DisplayPos）
    float center[2];    // 玻璃形状中心（ImGui 坐标系，y 向下）
    float half[2];      // 半宽 / 半高
    float time;         // 动画时间，构建期算好；回调内绝不推进
    float aspect;       // 背景矩形宽高比
    float radius;
    float bevel;
    float refract;
    float blur;
    float spec;
    float sat;
    float bright;
    float disp;
    float wobble;
    float press;
    float hover;
    float shadow[4];    // offX, offY, radius, gain
    float tint[4];      // rgb + amount
    int   useTex;       // 是否允许用离屏背景纹理（0 = 强制程序化重算）
    int   pass;         // LG_PASS_BG / LG_PASS_GLASS
};
static_assert(std::is_trivially_copyable<玻璃参数>::value, "userdata 必须是 POD");

// 帧内 userdata 池：地址在整帧内稳定（AddCallback 同时按 size 深拷贝，双保险）。
// 每帧在 DrawLiquidGlassTab() 开头复位。
enum { LG_POOL_MAX = 48 };
static 玻璃参数 g_pool[LG_POOL_MAX];
static int      g_pool_n = 0;

// ============================================================================
//  五、绘制回调
// ============================================================================

static void 液态玻璃_回调(const ImDrawList* parent_list, const ImDrawCmd* cmd)
{
    (void)parent_list;
    if (!cmd || !cmd->UserCallbackData) return;
    if (cmd->UserCallbackDataSize != (int)sizeof(玻璃参数)) return;   // 防御：尺寸对不上直接不画

    玻璃参数 p;
    memcpy(&p, cmd->UserCallbackData, sizeof(p));

    if (!确保程序就绪()) return;

    GL状态守卫 gd;
    gd.保存();

    const float vpX = (float)gd.viewport[0];
    const float vpY = (float)gd.viewport[1];
    const float vpW = (float)gd.viewport[2];
    const float vpH = (float)gd.viewport[3];
    if (vpW < 1.0f || vpH < 1.0f) { gd.恢复(); return; }

    // ---------- 裁剪：复刻后端的投影公式（本工程 FramebufferScale 恒为 (1,1)）----------
    // 回调分支后端不会设置 scissor box，box 是上一条非回调命令留下的残值，必须自己算。
    // 另外 ImGenie 捕获路径会把贴边的 ClipRect 撑到 ±65536，所以要夹紧到 viewport 内。
    float cx0 = cmd->ClipRect.x - p.clipOff[0];
    float cy0 = cmd->ClipRect.y - p.clipOff[1];
    float cx1 = cmd->ClipRect.z - p.clipOff[0];
    float cy1 = cmd->ClipRect.w - p.clipOff[1];
    if (cx0 < 0.0f)  cx0 = 0.0f;
    if (cy0 < 0.0f)  cy0 = 0.0f;
    if (cx1 > vpW)   cx1 = vpW;
    if (cy1 > vpH)   cy1 = vpH;

    // 背景矩形 / 四边形：ImGui 坐标（左上原点）-> GL 窗口坐标（左下原点）
    const float bgX = vpX + p.bgRect[0];
    const float bgY = vpY + vpH - p.bgRect[3];
    const float bgW = p.bgRect[2] - p.bgRect[0];
    const float bgH = p.bgRect[3] - p.bgRect[1];
    if (bgW < 1.0f || bgH < 1.0f) { gd.恢复(); return; }

    const float aspect = (p.aspect > 0.01f) ? p.aspect : (bgW / bgH);

    glBindVertexArray(g_gl.vao);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DITHER);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);

    if (p.pass == LG_PASS_BG) {
        // -------- Pass 1a：把动态背景渲进半分辨率离屏纹理，供玻璃采样（带 mipmap 做大半径模糊）--------
        g_gl.bgReady = false;
        if (p.useTex != 0) {
            int wantW = (int)(bgW * 0.5f);
            int wantH = (int)(bgH * 0.5f);
            if (确保背景纹理(wantW, wantH)) {
                glBindFramebuffer(GL_FRAMEBUFFER, g_gl.fbo);
                glDisable(GL_SCISSOR_TEST);
                glDisable(GL_BLEND);
                glViewport(0, 0, g_gl.texW, g_gl.texH);
                glUseProgram(g_gl.progBg);
                glUniform4f(g_gl.bg.rect, 0.0f, 0.0f, (float)g_gl.texW, (float)g_gl.texH);
                glUniform4f(g_gl.bg.viewport, 0.0f, 0.0f, (float)g_gl.texW, (float)g_gl.texH);
                glUniform1f(g_gl.bg.aspect, aspect);
                glUniform1f(g_gl.bg.time, p.time);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

                // 先把 FBO 解绑回原来的目标，再对纹理生成 mipmap
                // （对仍挂在当前 FBO 上的纹理生成 mipmap 行为是未定义的）
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)gd.drawFbo);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)gd.readFbo);
                glBindTexture(GL_TEXTURE_2D, g_gl.tex);
                glGenerateMipmap(GL_TEXTURE_2D);
                g_gl.bgReady = true;
            }
        }

        // -------- Pass 1b：把动态背景按全分辨率直接画到屏幕上（不透明，作为折射素材的可见层）--------
        if (cx1 > cx0 && cy1 > cy0) {
            glViewport(gd.viewport[0], gd.viewport[1], gd.viewport[2], gd.viewport[3]);
            glEnable(GL_SCISSOR_TEST);
            glScissor((GLint)cx0 + gd.viewport[0],
                      (GLint)(vpH - cy1) + gd.viewport[1],
                      (GLsizei)(cx1 - cx0), (GLsizei)(cy1 - cy0));
            glDisable(GL_BLEND);
            glUseProgram(g_gl.progBg);
            glUniform4f(g_gl.bg.rect, bgX, bgY, bgW, bgH);
            glUniform4f(g_gl.bg.viewport, vpX, vpY, vpW, vpH);
            glUniform1f(g_gl.bg.aspect, aspect);
            glUniform1f(g_gl.bg.time, p.time);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
    } else {
        // -------- Pass 2：液态玻璃合成 --------
        if (cx1 <= cx0 || cy1 <= cy0) { gd.恢复(); return; }

        const float qx = vpX + p.quad[0];
        const float qy = vpY + vpH - p.quad[3];
        const float qw = p.quad[2] - p.quad[0];
        const float qh = p.quad[3] - p.quad[1];
        if (qw < 1.0f || qh < 1.0f) { gd.恢复(); return; }

        glEnable(GL_SCISSOR_TEST);
        glScissor((GLint)cx0 + gd.viewport[0],
                  (GLint)(vpH - cy1) + gd.viewport[1],
                  (GLsizei)(cx1 - cx0), (GLsizei)(cy1 - cy0));

        // 与 ImGui 后端同款混合方程：RGB 非预乘，alpha 通道做预乘式累积。
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        const bool hasTex = (p.useTex != 0) && g_gl.bgReady && g_gl.tex != 0;

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hasTex ? g_gl.tex : 0);
        glBindSampler(0, 0);

        glUseProgram(g_gl.progGlass);
        glUniform1i(g_gl.gs.tex, 0);
        glUniform4f(g_gl.gs.rect, qx, qy, qw, qh);
        glUniform4f(g_gl.gs.viewport, vpX, vpY, vpW, vpH);
        glUniform4f(g_gl.gs.bgRect, bgX, bgY, bgW, bgH);
        if (hasTex) {
            const float texelScale = (float)g_gl.texW / bgW;
            glUniform4f(g_gl.gs.bgInfo, 1.0f, texelScale,
                        0.5f / (float)g_gl.texW, 0.5f / (float)g_gl.texH);
        } else {
            glUniform4f(g_gl.gs.bgInfo, 0.0f, 1.0f, 0.0f, 0.0f);
        }
        glUniform1f(g_gl.gs.aspect, aspect);
        glUniform1f(g_gl.gs.time, p.time);
        glUniform4f(g_gl.gs.shape, p.center[0], p.center[1], p.half[0], p.half[1]);
        glUniform4f(g_gl.gs.pa, p.radius, p.bevel, p.refract, p.blur);
        glUniform4f(g_gl.gs.pb, p.spec, p.sat, p.bright, p.disp);
        glUniform4f(g_gl.gs.pc, p.wobble, p.press, p.hover, hasTex ? g_gl.maxLod : 0.0f);
        glUniform4f(g_gl.gs.shadow, p.shadow[0], p.shadow[1], p.shadow[2], p.shadow[3]);
        glUniform4f(g_gl.gs.tint, p.tint[0], p.tint[1], p.tint[2], p.tint[3]);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    gd.恢复();
    // 返回后紧跟的 ImDrawCallback_ResetRenderState 会让后端把它自己的
    // 临时 VAO / attrib 指针 / ProjMtx uniform 也重新设置一遍。
}

// ============================================================================
//  六、构建期：参数、缓动、控件
// ============================================================================

// ---------- 模块私有可调参数（不进 全局状态.h，与 音频标签页.cpp 的做法一致）----------
static float g_lg_refract = 14.0f;   // 边界处最大折射位移（px）
static float g_lg_blur    = 22.0f;   // 最厚处模糊半径（px）
static float g_lg_radius  = 26.0f;   // 圆角半径（px）
static float g_lg_spec    = 0.55f;   // 高光强度
static float g_lg_bright  = 1.06f;   // 玻璃亮度增益
static float g_lg_sat     = 1.35f;   // 玻璃饱和度
static float g_lg_disp    = 0.045f;  // 边缘色散
static float g_lg_bevel   = 26.0f;   // 倒角带宽（“玻璃有多厚”）
static float g_lg_wobble  = 1.5f;    // 液态呼吸幅度（px）
static float g_lg_shadow  = 0.35f;   // 外投影强度
static float g_lg_tint    = 0.10f;   // 玻璃自色混合量
static bool  g_lg_use_tex = true;    // true = 离屏纹理 + mip 模糊；false = 着色器内程序化重算

// ---------- 展示控件的状态 ----------
static bool  g_lg_demo_toggle = true;
static float g_lg_demo_slider = 0.62f;
static int   g_lg_demo_dock   = 2;
static int   g_lg_demo_click  = 0;

static void 恢复默认参数()
{
    g_lg_refract = 14.0f; g_lg_blur = 22.0f;  g_lg_radius = 26.0f;
    g_lg_spec    = 0.55f; g_lg_bright = 1.06f; g_lg_sat   = 1.35f;
    g_lg_disp    = 0.045f; g_lg_bevel = 26.0f; g_lg_wobble = 1.5f;
    g_lg_shadow  = 0.35f; g_lg_tint  = 0.10f; g_lg_use_tex = true;
}

// 指数缓动：交互态（按下 / 悬停）平滑过渡，让高光和形变跟手。
// 状态存在窗口的 ImGuiStorage 里，key 由控件 ID 派生，不需要额外的全局表。
static float 缓动(ImGuiID key, float target, float speed)
{
    ImGuiStorage* store = ImGui::GetStateStorage();
    if (!store) return target;
    float cur = store->GetFloat(key, target);
    float dt = ImGui::GetIO().DeltaTime;
    if (dt < 0.0f)   dt = 0.0f;
    if (dt > 0.033f) dt = 0.033f;   // 掉帧时封顶，避免动画瞬移
    float k = 1.0f - expf(-speed * dt);
    cur += (target - cur) * k;
    if (cur < 0.0f) cur = 0.0f;
    if (cur > 1.0f) cur = 1.0f;
    store->SetFloat(key, cur);
    return cur;
}

// 当前标签页的动态背景矩形（构建期算好，所有玻璃控件共用）
static ImVec2 g_bg_min, g_bg_max;
static float  g_bg_time = 0.0f;

// 提交一次自定义 GL 绘制。userdata 取自帧内池（地址整帧稳定），
// 同时带上 userdata_size 让 ImGui 深拷贝进 drawlist 缓冲 —— 双保险。
static void 提交回调(const 玻璃参数& src)
{
    if (g_pool_n >= LG_POOL_MAX) return;
    玻璃参数& slot = g_pool[g_pool_n++];
    slot = src;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (!dl) return;
    dl->AddCallback(液态玻璃_回调, &slot, sizeof(玻璃参数));
    dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

// 填好一份公共参数（背景矩形、时间、全局可调项）
static 玻璃参数 取公共参数()
{
    玻璃参数 p;
    memset(&p, 0, sizeof(p));

    const ImVec2 off = ImGui::GetMainViewport()->Pos;
    p.clipOff[0] = off.x;
    p.clipOff[1] = off.y;

    p.bgRect[0] = g_bg_min.x - off.x;
    p.bgRect[1] = g_bg_min.y - off.y;
    p.bgRect[2] = g_bg_max.x - off.x;
    p.bgRect[3] = g_bg_max.y - off.y;

    const float bw = g_bg_max.x - g_bg_min.x;
    const float bh = g_bg_max.y - g_bg_min.y;
    p.aspect = (bh > 1.0f) ? (bw / bh) : 1.0f;
    p.time   = g_bg_time;

    p.radius  = g_lg_radius;
    p.bevel   = g_lg_bevel;
    p.refract = g_lg_refract;
    p.blur    = g_lg_blur;
    p.spec    = g_lg_spec;
    p.sat     = g_lg_sat;
    p.bright  = g_lg_bright;
    p.disp    = g_lg_disp;
    p.wobble  = g_lg_wobble;
    p.useTex  = g_lg_use_tex ? 1 : 0;

    p.tint[0] = 0.62f; p.tint[1] = 0.86f; p.tint[2] = 1.00f; p.tint[3] = g_lg_tint;
    return p;
}

// 提交一块玻璃。p0/p1 是 ImGui 屏幕坐标；radiusScale 用来做胶囊（传很大的圆角即可）。
static void 提交玻璃(const ImVec2& p0, const ImVec2& p1, float radius,
                     float press, float hover, float scale, float blurMul = 1.0f)
{
    // 按下时整块玻璃向内收一点，配合着色器里的呼吸/高光增强，交互更跟手
    const float shrink = 2.5f * scale * press;
    ImVec2 a(p0.x + shrink, p0.y + shrink);
    ImVec2 b(p1.x - shrink, p1.y - shrink);
    if (b.x - a.x < 2.0f || b.y - a.y < 2.0f) return;

    玻璃参数 p = 取公共参数();
    p.pass   = LG_PASS_GLASS;
    p.press  = press;
    p.hover  = hover;
    p.blur  *= blurMul;

    const float halfW = (b.x - a.x) * 0.5f;
    const float halfH = (b.y - a.y) * 0.5f;
    const float rad   = ImMin(radius * scale, ImMin(halfW, halfH));

    const ImVec2 off = ImGui::GetMainViewport()->Pos;
    p.center[0] = (a.x + b.x) * 0.5f - off.x;
    p.center[1] = (a.y + b.y) * 0.5f - off.y;
    p.half[0]   = halfW;
    p.half[1]   = halfH;
    p.radius    = rad;
    p.bevel     = ImMin(g_lg_bevel * scale, ImMin(halfW, halfH) * 0.95f);
    p.refract   = g_lg_refract * scale;
    p.blur     *= scale;

    const float shOffY = (9.0f + 5.0f * hover) * scale;
    const float shRad  = (30.0f + 10.0f * hover) * scale;
    p.shadow[0] = 0.0f;
    p.shadow[1] = shOffY;
    p.shadow[2] = ImMax(shRad, 1.0f);
    p.shadow[3] = g_lg_shadow;

    // 四边形要把外投影一起罩住，否则阴影会被切掉
    const float margin = shRad + shOffY + 4.0f;
    ImVec2 q0(a.x - margin, a.y - margin);
    ImVec2 q1(b.x + margin, b.y + margin);
    if (!ImGui::IsRectVisible(q0, q1)) return;   // 整块滚出视野就别提交了，省一次 draw call

    p.quad[0] = q0.x - off.x;
    p.quad[1] = q0.y - off.y;
    p.quad[2] = q1.x - off.x;
    p.quad[3] = q1.y - off.y;
    提交回调(p);
}

// ---------- 降级层：GL 不可用时这就是唯一可见的画面；GL 可用时会被不透明的 GL 背景整块盖住 ----------
static void 降级背景(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, float t)
{
    if (!dl) return;
    const float w = p1.x - p0.x, h = p1.y - p0.y;
    if (w < 2.0f || h < 2.0f) return;

    dl->AddRectFilledMultiColor(p0, p1,
        IM_COL32(6, 12, 28, 255), IM_COL32(14, 46, 82, 255),
        IM_COL32(30, 14, 54, 255), IM_COL32(6, 30, 44, 255));

    struct { float px, py, sp, r; ImU32 col; } blobs[5] = {
        { 0.28f, 0.30f, 0.17f, 0.30f, IM_COL32( 24, 108, 184, 46) },
        { 0.72f, 0.68f, 0.13f, 0.26f, IM_COL32( 88,  30, 144, 44) },
        { 0.52f, 0.22f, 0.21f, 0.20f, IM_COL32( 14, 116, 112, 40) },
        { 0.14f, 0.80f, 0.11f, 0.22f, IM_COL32(110,  50,  76, 38) },
        { 0.88f, 0.16f, 0.27f, 0.17f, IM_COL32( 40,  84, 164, 40) },
    };
    for (int i = 0; i < 5; ++i) {
        const float a = t * blobs[i].sp;
        const float cx = p0.x + (blobs[i].px + 0.14f * cosf(a)) * w;
        const float cy = p1.y - (blobs[i].py + 0.12f * sinf(a * 1.3f)) * h;
        dl->AddCircleFilled(ImVec2(cx, cy), blobs[i].r * (w < h ? w : h), blobs[i].col, 32);
    }
}

static void 降级玻璃(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, float radius, float press)
{
    if (!dl) return;
    if (p1.x - p0.x < 2.0f || p1.y - p0.y < 2.0f) return;
    const int a = 40 + (int)(30.0f * press);
    dl->AddRectFilled(p0, p1, IM_COL32(150, 205, 240, a), radius);
    dl->AddRect(p0, p1, IM_COL32(158, 230, 255, 128 + (int)(60.0f * press)), radius, 0, 1.4f);
}

// ---------- 文本工具 ----------
static void 画文本(ImDrawList* dl, const ImVec2& pos, const char* txt, ImU32 col)
{
    if (!dl || !txt) return;
    dl->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f), IM_COL32(0, 8, 18, 140), txt);
    dl->AddText(pos, col, txt);
}

static void 居中文本(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, const char* txt, ImU32 col)
{
    if (!dl || !txt) return;
    const ImVec2 ts = ImGui::CalcTextSize(txt);
    画文本(dl, ImVec2((p0.x + p1.x - ts.x) * 0.5f, (p0.y + p1.y - ts.y) * 0.5f), txt, col);
}

// ---------- 交互：一块矩形区域的 hover/active + 缓动 ----------
struct 交互态 { bool hovered = false; bool active = false; bool clicked = false; float press = 0.0f; float hover = 0.0f; };

static 交互态 玻璃交互(const char* id, const ImVec2& p0, const ImVec2& p1)
{
    交互态 st;
    const ImVec2 size(p1.x - p0.x, p1.y - p0.y);
    if (size.x <= 1.0f || size.y <= 1.0f) return st;

    const ImGuiID key = ImGui::GetID(id);
    ImGui::SetCursorScreenPos(p0);
    st.clicked = ImGui::InvisibleButton(id, size);
    st.hovered = ImGui::IsItemHovered();
    st.active  = ImGui::IsItemActive();
    // 按下要快、松开要略慢，手感更接近 iOS
    st.press = 缓动(key ^ 0x9E37u, st.active ? 1.0f : 0.0f, st.active ? 30.0f : 13.0f);
    st.hover = 缓动(key ^ 0x85EBu, st.hovered ? 1.0f : 0.0f, 15.0f);
    return st;
}

const ImU32 kTextMain = IM_COL32(230, 247, 255, 255);
const ImU32 kTextSub  = IM_COL32(168, 210, 235, 220);
const ImU32 kAccent   = IM_COL32(122, 224, 255, 255);

} // namespace

// ============================================================================
//  七、标签页正文
// ============================================================================

void DrawLiquidGlassTab()
{
    // 每帧复位帧内 userdata 池
    g_pool_n = 0;

    ImGui::BeginChild("液态玻璃_容器", ImVec2(0, 0), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 org = ImGui::GetWindowPos();
    const ImVec2 sz  = ImGui::GetWindowSize();
    const ImVec2 end(org.x + sz.x, org.y + sz.y);

    if (sz.x < 40.0f || sz.y < 40.0f || !dl) { ImGui::EndChild(); return; }

    g_bg_min  = org;
    g_bg_max  = end;
    g_bg_time = (float)ImGui::GetTime();

    // 整块内容自己裁剪一次，避免自绘内容溢出标签页
    dl->PushClipRect(org, end, true);

    // (1) 降级层先画：GL 正常时会被不透明的 GL 背景整块覆盖，GL 挂了它就是唯一画面。
    降级背景(dl, org, end, g_bg_time);

    // (2) 提交动态背景的 GL 绘制（离屏半分辨率纹理 + 屏幕全分辨率可见层）
    {
        玻璃参数 p = 取公共参数();
        p.pass = LG_PASS_BG;
        p.quad[0] = p.bgRect[0]; p.quad[1] = p.bgRect[1];
        p.quad[2] = p.bgRect[2]; p.quad[3] = p.bgRect[3];
        提交回调(p);
    }

    // ---------- 版面尺度：跟随窗口大小整体缩放，小窗口也不会挤爆 ----------
    const float s   = ImClamp(ImMin(sz.x / 720.0f, sz.y / 520.0f), 0.60f, 1.25f);
    const float pad = 14.0f * s;
    const float gap = 12.0f * s;
    const float cardH = 128.0f * s;
    const float btnH  = 58.0f * s;
    const float dockH = 76.0f * s;

    const float x0 = org.x + pad;
    const float x1 = end.x - pad;
    const float innerW = x1 - x0;
    float y = org.y + pad;

    // ==================== 玻璃卡片 + 右侧开关/滑条 ====================
    const float cardW = innerW * 0.52f;
    {
        const ImVec2 c0(x0, y);
        const ImVec2 c1(x0 + cardW, y + cardH);
        交互态 st = 玻璃交互("##lg_card", c0, c1);
        降级玻璃(dl, c0, c1, g_lg_radius * s, st.press);
        提交玻璃(c0, c1, g_lg_radius * 1.35f, st.press, st.hover, s);

        const float tx = c0.x + 18.0f * s;
        画文本(dl, ImVec2(tx, c0.y + 16.0f * s), "Liquid Glass", kTextMain);
        画文本(dl, ImVec2(tx, c0.y + 40.0f * s), "折射 / 模糊 / 色散 / 流动高光", kTextSub);
        char buf[96];
        ImFormatString(buf, sizeof(buf), "点击次数 %d   背景源 %s",
                       g_lg_demo_click, g_lg_use_tex ? "离屏纹理(mip)" : "着色器程序化");
        画文本(dl, ImVec2(tx, c0.y + cardH - 30.0f * s), buf, kTextSub);
    }

    // 右列
    const float rx0 = x0 + cardW + gap;
    const float rw  = x1 - rx0;
    if (rw > 80.0f * s) {
        // ---------- 玻璃开关 ----------
        const float togW = ImMin(104.0f * s, rw);
        const float togH = 50.0f * s;
        const ImVec2 t0(rx0, y);
        const ImVec2 t1(rx0 + togW, y + togH);
        交互态 st = 玻璃交互("##lg_toggle", t0, t1);
        if (st.clicked) g_lg_demo_toggle = !g_lg_demo_toggle;

        const ImGuiID knobKey = ImGui::GetID("##lg_toggle") ^ 0xC2B2u;
        const float on = 缓动(knobKey, g_lg_demo_toggle ? 1.0f : 0.0f, 16.0f);

        降级玻璃(dl, t0, t1, togH * 0.5f, st.press);
        提交玻璃(t0, t1, togH, st.press, st.hover, s, 1.15f);

        // 打开时轨道内透出一层青色（画在轨道玻璃之上、滑块之下）
        if (on > 0.001f) {
            dl->AddRectFilled(ImVec2(t0.x + 3.0f * s, t0.y + 3.0f * s),
                              ImVec2(t1.x - 3.0f * s, t1.y - 3.0f * s),
                              IM_COL32(60, 190, 240, (int)(70.0f * on)), togH * 0.5f);
        }

        const float knobR = togH * 0.5f - 5.0f * s;
        const float kcx = ImLerp(t0.x + knobR + 5.0f * s, t1.x - knobR - 5.0f * s, on);
        const float kcy = (t0.y + t1.y) * 0.5f;
        const ImVec2 k0(kcx - knobR, kcy - knobR);
        const ImVec2 k1(kcx + knobR, kcy + knobR);
        降级玻璃(dl, k0, k1, knobR, st.press);
        提交玻璃(k0, k1, knobR * 2.0f, st.press, st.hover, s, 0.55f);
        居中文本(dl, k0, k1, g_lg_demo_toggle ? "开" : "关", kTextMain);

        画文本(dl, ImVec2(t1.x + 10.0f * s, kcy - ImGui::GetTextLineHeight() * 0.5f),
               "玻璃开关", kTextSub);

        // ---------- 玻璃滑条 ----------
        const float sy0 = y + togH + gap;
        const float sh  = 44.0f * s;
        const ImVec2 b0(rx0, sy0);
        const ImVec2 b1(x1, sy0 + sh);
        if (b1.y < y + cardH + 2.0f && b1.x - b0.x > 60.0f * s) {
            交互态 sst = 玻璃交互("##lg_slider", b0, b1);
            if (sst.active) {
                const float mx = ImGui::GetIO().MousePos.x;
                const float t  = (mx - b0.x - 12.0f * s) / ImMax(b1.x - b0.x - 24.0f * s, 1.0f);
                g_lg_demo_slider = ImClamp(t, 0.0f, 1.0f);
            }
            降级玻璃(dl, b0, b1, sh * 0.5f, sst.press);
            提交玻璃(b0, b1, sh, sst.press, sst.hover, s, 1.10f);

            const float fillR = sh * 0.5f - 5.0f * s;
            const float fx0 = b0.x + 5.0f * s;
            const float fx1 = ImLerp(fx0, b1.x - 5.0f * s, g_lg_demo_slider);
            if (fx1 > fx0 + 2.0f) {
                dl->AddRectFilled(ImVec2(fx0, b0.y + 5.0f * s), ImVec2(fx1, b1.y - 5.0f * s),
                                  IM_COL32(102, 199, 255, 96), fillR);
            }
            const float gcx = ImClamp(fx1, b0.x + 12.0f * s, b1.x - 12.0f * s);
            const float gr  = sh * 0.5f - 7.0f * s;
            const ImVec2 g0(gcx - gr, (b0.y + b1.y) * 0.5f - gr);
            const ImVec2 g1(gcx + gr, (b0.y + b1.y) * 0.5f + gr);
            降级玻璃(dl, g0, g1, gr, sst.press);
            提交玻璃(g0, g1, gr * 2.0f, sst.press, sst.hover, s, 0.50f);

            char vbuf[32];
            ImFormatString(vbuf, sizeof(vbuf), "%.0f%%", g_lg_demo_slider * 100.0f);
            画文本(dl, ImVec2(b0.x + 12.0f * s, b0.y - ImGui::GetTextLineHeight() - 2.0f * s), vbuf, kAccent);
        }
    }
    y += cardH + gap;

    // ==================== 三个玻璃按钮 ====================
    {
        const char* labels[3] = { "液态呼吸", "随机配色", "恢复默认" };
        const float bw = (innerW - gap * 2.0f) / 3.0f;
        for (int i = 0; i < 3; ++i) {
            char id[32];
            ImFormatString(id, sizeof(id), "##lg_btn%d", i);
            const ImVec2 p0(x0 + (bw + gap) * i, y);
            const ImVec2 p1(p0.x + bw, y + btnH);
            交互态 st = 玻璃交互(id, p0, p1);
            if (st.clicked) {
                ++g_lg_demo_click;
                if (i == 0)      g_lg_wobble = (g_lg_wobble > 0.05f) ? 0.0f : 1.5f;
                else if (i == 1) g_lg_tint   = (g_lg_tint > 0.16f) ? 0.06f : 0.24f;
                else             恢复默认参数();
            }
            降级玻璃(dl, p0, p1, g_lg_radius * s, st.press);
            提交玻璃(p0, p1, g_lg_radius * 1.2f, st.press, st.hover, s);
            居中文本(dl, p0, p1, labels[i], kTextMain);
        }
    }
    y += btnH + gap;

    // ==================== 玻璃药丸 / Dock 条 ====================
    {
        const float dw = ImMin(innerW, 560.0f * s);
        const float dx0 = (org.x + end.x - dw) * 0.5f;
        const ImVec2 d0(dx0, y);
        const ImVec2 d1(dx0 + dw, y + dockH);

        交互态 dst = 玻璃交互("##lg_dock", d0, d1);
        降级玻璃(dl, d0, d1, dockH * 0.5f, dst.press);
        提交玻璃(d0, d1, dockH, dst.press * 0.35f, dst.hover, s, 1.25f);

        // 图标用 ImDrawList 画（画在药丸玻璃之上），只有选中项额外叠一块小玻璃，
        // 这样一条 Dock 只多一次自定义 draw call。
        const int   kN = 6;
        const char* icons[6] = { "折", "模", "光", "色", "波", "影" };
        const float base = dockH * 0.52f;
        const float step = dw / (float)kN;
        // 用矩形命中测试而不是 dst.hovered：下面每个图标自己也有热区，
        // 手指落在图标上时 Dock 本身就不再是 hovered，会让放大效果瞬间塌掉。
        const bool  overDock = ImGui::IsMousePosValid() && ImGui::IsMouseHoveringRect(d0, d1);
        const float mx = ImGui::GetIO().MousePos.x;

        for (int i = 0; i < kN; ++i) {
            const float cx = d0.x + step * ((float)i + 0.5f);
            const float cy = (d0.y + d1.y) * 0.5f;

            char mid[32];
            ImFormatString(mid, sizeof(mid), "##lg_dock_mag%d", i);
            float target = 0.0f;
            if (overDock) {
                const float dx = (mx - cx) / (step * 1.15f);
                target = expf(-dx * dx * 1.6f);
            }
            const float mag = 缓动(ImGui::GetID(mid), target, 18.0f);

            const float half = base * (0.5f + 0.26f * mag);
            const ImVec2 i0(cx - half, cy - half);
            const ImVec2 i1(cx + half, cy + half);

            const bool sel = (g_lg_demo_dock == i);
            if (sel) {
                提交玻璃(i0, i1, half * 1.4f, 0.0f, 0.55f + 0.45f * mag, s, 0.45f);
            } else {
                dl->AddRectFilled(i0, i1, IM_COL32(150, 210, 245, (int)(34 + 48 * mag)), half * 0.55f);
                dl->AddRect(i0, i1, IM_COL32(158, 230, 255, (int)(80 + 90 * mag)), half * 0.55f, 0, 1.2f);
            }
            居中文本(dl, i0, i1, icons[i], sel ? kAccent : kTextMain);

            // 图标自己的点击热区（放在 Dock 玻璃之后提交，保证能吃到点击）
            char bid[32];
            ImFormatString(bid, sizeof(bid), "##lg_dock_btn%d", i);
            ImGui::SetCursorScreenPos(i0);
            if (ImGui::InvisibleButton(bid, ImVec2(i1.x - i0.x, i1.y - i0.y))) {
                g_lg_demo_dock = i;
                ++g_lg_demo_click;
            }
        }
    }
    y += dockH + gap;

    // ==================== 参数面板（本身也是一块玻璃）====================
    {
        const ImVec2 q0(x0, y);
        const ImVec2 q1(x1, end.y - pad);
        if (q1.y - q0.y > 60.0f * s) {
            降级玻璃(dl, q0, q1, g_lg_radius * s, 0.0f);
            提交玻璃(q0, q1, g_lg_radius * 1.2f, 0.0f, 0.0f, s, 1.35f);

            // 子窗口的 DrawList 排在父窗口之后渲染，所以这些控件一定盖在上面那块玻璃之上。
            const float ip = 12.0f * s;
            ImGui::SetCursorScreenPos(ImVec2(q0.x + ip, q0.y + ip));
            ImGui::BeginChild("液态玻璃_参数", ImVec2(q1.x - q0.x - ip * 2.0f, q1.y - q0.y - ip * 2.0f),
                              ImGuiChildFlags_None, ImGuiWindowFlags_None);

            ImGui::PushItemWidth(ImMax(ImGui::GetContentRegionAvail().x * 0.52f, 120.0f));
            ImGui::TextColored(ImVec4(0.48f, 0.88f, 1.00f, 1.00f), "实时参数");
            ImGui::SliderFloat("折射强度##lg", &g_lg_refract, 0.0f, 40.0f, "%.1f px");
            ImGui::SliderFloat("模糊半径##lg", &g_lg_blur,    0.0f, 60.0f, "%.1f px");
            ImGui::SliderFloat("圆角##lg",     &g_lg_radius,  0.0f, 80.0f, "%.0f px");
            ImGui::SliderFloat("高光强度##lg", &g_lg_spec,    0.0f, 2.0f,  "%.2f");
            ImGui::SliderFloat("亮度##lg",     &g_lg_bright,  0.6f, 1.8f,  "%.2f");
            ImGui::SliderFloat("饱和度##lg",   &g_lg_sat,     0.0f, 2.5f,  "%.2f");
            ImGui::SliderFloat("色散##lg",     &g_lg_disp,    0.0f, 0.15f, "%.3f");
            ImGui::SliderFloat("玻璃厚度##lg", &g_lg_bevel,   4.0f, 80.0f, "%.0f px");
            ImGui::SliderFloat("液态呼吸##lg", &g_lg_wobble,  0.0f, 6.0f,  "%.2f px");
            ImGui::SliderFloat("投影##lg",     &g_lg_shadow,  0.0f, 0.8f,  "%.2f");
            ImGui::SliderFloat("玻璃自色##lg", &g_lg_tint,    0.0f, 0.45f, "%.2f");
            ImGui::PopItemWidth();

            ImGui::Checkbox("离屏纹理 + mip 模糊##lg", &g_lg_use_tex);
            ImGui::SameLine();
            if (ImGui::Button("恢复默认##lg")) 恢复默认参数();
            ImGui::SameLine();
            if (g_gl_available) {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.6f, 1.0f), "GL 玻璃已启用");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.0f, 1.0f), "着色器不可用，已降级");
            }
            ImGui::TextDisabled("提示：按住控件看形变与高光；关掉离屏纹理可对比程序化模糊。");

            ImGui::EndChild();
        }
    }

    dl->PopClipRect();
    ImGui::EndChild();
}

// ============================================================================
//  八、资源释放
// ============================================================================

void LiquidGlass_ReleaseGLObjects()
{
    EGLContext cur = eglGetCurrentContext();
    if (cur == EGL_NO_CONTEXT || cur != g_gl.owner) {
        // 上下文已经没了或者换过了：句柄整体作废即可，绝不能对死上下文调 glDelete*
        g_gl = 液态玻璃GL{};
        return;
    }
    if (g_gl.fbo != 0)       glDeleteFramebuffers(1, &g_gl.fbo);
    if (g_gl.tex != 0)       glDeleteTextures(1, &g_gl.tex);
    if (g_gl.vao != 0)       glDeleteVertexArrays(1, &g_gl.vao);
    if (g_gl.progBg != 0)    glDeleteProgram(g_gl.progBg);
    if (g_gl.progGlass != 0) glDeleteProgram(g_gl.progGlass);
    g_gl = 液态玻璃GL{};
}
