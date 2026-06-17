//
// Aura UI 模块实现
// 完美移植自 AuraNexus 项目
// 严格按原版 MainDefinition.h + imgui_widgets.cpp + MainUI.h 逐行移植
// 所有配色、动画参数、窗口结构、widget 实现与原版完全一致
//

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "aura_ui.hpp"
#include "LOGO.h"

#include <GLES3/gl3.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <cmath>
#include <unistd.h>
#include <fcntl.h>
#include <sys/sysinfo.h>

namespace aura_ui {

// ============================================================================
// 全局变量（原版 main.cpp + draw.cpp）
// ============================================================================
ImTextureID LOGO = 0;
static GLuint g_logo_texture = 0;

// 原版 main.cpp 全局变量
static float menu_expand = 0.0f;
static bool last_menu_state = false;
static bool MainAuraOne = true;
float FPSControlSize = 60.0f;
int background_mode = 0;
float ImGuiDrawESP = 0.7f;
float ImGuiDrawESP2 = 0.7f;
int Prevent = 0;

// ============================================================================
// 游戏功能 stub（原版 MainDefinition.h 第889行起 + gjc.h）
// 本项目无对应游戏功能，提供空实现以保持 UI 与原版一致
// ============================================================================
std::atomic<bool> lb_gjc_injected{false};
std::string lb_gjc_status = "\xe6\x9c\xaa\xe6\xb3\xa8\xe5\x85\xa5"; // 未注入
std::atomic<bool> lb_game_running{false};
std::atomic<bool> lb_game_ready{false};
static unsigned long lb_base_libUE4 = 0;

std::atomic<bool> lb_feature_luna{false};
std::atomic<bool> lb_feature_head{false};
std::atomic<bool> lb_feature_body{false};
std::atomic<bool> lb_feature_bullet{false};
std::atomic<bool> lb_feature_norecoil{false};
std::atomic<bool> lb_feature_thirdperson{false};
std::atomic<bool> lb_feature_neitou{false};

std::atomic<float> lb_head_val{90.0f};
std::atomic<float> lb_body_val{80.0f};

std::atomic<bool> lb_freeze_running{false};
std::atomic<float> lb_abdominal_target{99.0f};
std::thread lb_freeze_thread;

uintptr_t lb_bullet_addr = 0;
int lb_bullet_val = 505873377;
std::atomic<bool> lb_bullet_frozen{false};

uintptr_t lb_norecoil_addr = 0;
int lb_norecoil_val = -1119858432;
std::atomic<bool> lb_norecoil_frozen{false};

uintptr_t lb_thirdperson_addr = 0;
int lb_thirdperson_val = 1384120352;
std::atomic<bool> lb_thirdperson_frozen{false};
std::thread lb_thirdperson_thread;

uintptr_t lb_neitou_world_ptr = 0;
std::atomic<bool> lb_neitou_running{false};
std::thread lb_neitou_thread;
std::string lb_neitou_status = "\xe6\x9c\xaa\xe5\x90\xaf\xe5\x8a\xa8"; // 未启动

void lb_init_game() {}
void lb_clean_exit() {}
void lb_writeDword(uintptr_t, int) {}
void lb_freeze_thread_func() {}
void lb_thirdperson_thread_func() {}
void lb_neitou_thread_func() {}

// ============================================================================
// init - 加载 LOGO 纹理（原版 LoadImages）
// ============================================================================
void init() {
    if (g_logo_texture) return;
    int w, h, channels;
    unsigned char* data = stbi_load_from_memory(Logo, sizeof(Logo), &w, &h, &channels, 4);
    if (!data) return;
    glGenTextures(1, &g_logo_texture);
    glBindTexture(GL_TEXTURE_2D, g_logo_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);
    LOGO = (ImTextureID)(intptr_t)g_logo_texture;
}

// ============================================================================
// UpdateBlurWindow - 空实现
// 原版用 Vulkan + SurfaceControl 背景模糊，OpenGL 后端无法实现
// ============================================================================
void UpdateBlurWindow(const ImVec2& pos, const ImVec2& size, float radius, bool enabled, int blurStrength) {
    (void)pos; (void)size; (void)radius; (void)enabled; (void)blurStrength;
}

// ============================================================================
// DrawAuraSectionTitle（原版 MainDefinition.h 第17-41行）
// ============================================================================
void DrawAuraSectionTitle(const char* title) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

    float title_height = ImGui::CalcTextSize(title).y;
    float line_width = 4.0f;
    float spacing = 8.0f;

    ImVec2 cursor_pos = window->DC.CursorPos;

    // 左侧竖条 - Mac 蓝色
    ImVec2 line_p1(cursor_pos.x, cursor_pos.y);
    ImVec2 line_p2(cursor_pos.x + line_width, cursor_pos.y + title_height);
    window->DrawList->AddRectFilled(line_p1, line_p2, IM_COL32(0, 122, 255, 255), 2.0f);

    // 标题文字 - Mac 风格白色
    ImVec2 text_pos(cursor_pos.x + line_width + spacing, cursor_pos.y);
    window->DrawList->AddText(text_pos, IM_COL32(235, 235, 240, 255), title);

    ImGui::ItemSize(ImVec2(0, title_height + style.ItemSpacing.y), 0.0f);
}

// ============================================================================
// _FreqControl 类（原版 MainDefinition.h 第43-161行）
// CPU/GPU/RAM 使用率读取
// ============================================================================
class _FreqControl {
private:
    static std::chrono::steady_clock::time_point cpuLastTime;
    static float cpuCachedValue;
    static unsigned long long cpuLastTotalUser;
    static unsigned long long cpuLastTotalUserLow;
    static unsigned long long cpuLastTotalSys;
    static unsigned long long cpuLastTotalIdle;
    static std::chrono::steady_clock::time_point memLastTime;
    static float memCachedPercent;
    static std::chrono::steady_clock::time_point gpuLastTime;
    static int gpuCachedValue;
    static constexpr int CPU_MIN_INTERVAL = 1000;
    static constexpr int MEM_MIN_INTERVAL = 2000;
    static constexpr int GPU_MIN_INTERVAL = 1000;
public:
    static float GetCPUUsage();
    static float GetMemPercent();
    static int GetGPUUsage();
private:
    static float _ReadCPU();
    static float _ReadMemPercent();
    static int _ReadGPU();
};
std::chrono::steady_clock::time_point _FreqControl::cpuLastTime =
    std::chrono::steady_clock::now() - std::chrono::milliseconds(2000);
float _FreqControl::cpuCachedValue = 0.0f;
unsigned long long _FreqControl::cpuLastTotalUser = 0;
unsigned long long _FreqControl::cpuLastTotalUserLow = 0;
unsigned long long _FreqControl::cpuLastTotalSys = 0;
unsigned long long _FreqControl::cpuLastTotalIdle = 0;
std::chrono::steady_clock::time_point _FreqControl::memLastTime =
    std::chrono::steady_clock::now() - std::chrono::milliseconds(3000);
float _FreqControl::memCachedPercent = 0.0f;
std::chrono::steady_clock::time_point _FreqControl::gpuLastTime =
    std::chrono::steady_clock::now() - std::chrono::milliseconds(2000);
int _FreqControl::gpuCachedValue = 0;

float _FreqControl::_ReadCPU() {
    unsigned long long user, nice, sys, idle;
    FILE* f = fopen("/proc/stat", "r");
    if (!f) return cpuCachedValue;
    if (fscanf(f, "cpu %llu %llu %llu %llu", &user, &nice, &sys, &idle) != EOF) {
        if (cpuLastTotalUser != 0) {
            unsigned long long d_user = user - cpuLastTotalUser;
            unsigned long long d_nice = nice - cpuLastTotalUserLow;
            unsigned long long d_sys  = sys - cpuLastTotalSys;
            unsigned long long d_idle = idle - cpuLastTotalIdle;
            unsigned long long total = d_user + d_nice + d_sys;
            unsigned long long all   = total + d_idle;
            if (all > 0)
                cpuCachedValue = (float)total / all * 100.0f;
        }
        cpuLastTotalUser= user;
        cpuLastTotalUserLow = nice;
        cpuLastTotalSys = sys;
        cpuLastTotalIdle= idle;
    }
    fclose(f);
    return cpuCachedValue;
}
float _FreqControl::GetCPUUsage() {
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - cpuLastTime).count();
    if (ms >= CPU_MIN_INTERVAL) {
        cpuLastTime = now;
        cpuCachedValue = _ReadCPU();
    }
    return cpuCachedValue;
}
float _FreqControl::_ReadMemPercent() {
    long total = 0, avail = 0;
    FILE* f = fopen("/proc/meminfo", "r");
    if (!f) return memCachedPercent;
    char buf[256];
    while (fgets(buf, sizeof(buf), f)) {
        if (!total) sscanf(buf, "MemTotal: %ld kB", &total);
        if (sscanf(buf, "MemAvailable: %ld kB", &avail)) break;
    }
    fclose(f);
    if (total <= 0) return 0.0f;
    return 100.0f - (float)avail / total * 100.0f;
}
float _FreqControl::GetMemPercent() {
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - memLastTime).count();
    if (ms >= MEM_MIN_INTERVAL) {
        memLastTime = now;
        memCachedPercent = _ReadMemPercent();
    }
    return memCachedPercent;
}
int _FreqControl::_ReadGPU() {
    const char* paths[] = {
        "/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage",
        "/sys/devices/platform/kgsl-3d0/kgsl/kgsl-3d0/gpu_busy_percentage",
        nullptr
    };
    for (const char* p : paths) {
        if (!p) continue;
        FILE* f = fopen(p, "r");
        if (!f) continue;
        int val = 0;
        if (fscanf(f, "%d", &val) == 1) {
            fclose(f);
            return val;
        }
        fclose(f);
    }
    return gpuCachedValue;
}
int _FreqControl::GetGPUUsage() {
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - gpuLastTime).count();
    if (ms >= GPU_MIN_INTERVAL) {
        gpuLastTime = now;
        gpuCachedValue = _ReadGPU();
    }
    return gpuCachedValue;
}

// ============================================================================
// FPSCounter 类（原版 MainDefinition.h 第162-181行）
// ============================================================================
class FPSCounter {
private:
    static long long lastTick;
    static int frames;
    static int currentFPS;
public:
    static int GetFPS() {
        long long now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        frames++;
        if (now - lastTick >= 1000) {
            currentFPS = frames;
            frames = 0;
            lastTick = now;
        }
        return currentFPS;
    }
};
long long FPSCounter::lastTick = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
int FPSCounter::frames = 0;
int FPSCounter::currentFPS = 0;

int GetRealFPS() { return FPSCounter::GetFPS(); }
int GetRealCPU() { return (int)_FreqControl::GetCPUUsage(); }
int GetRealGPU() { return _FreqControl::GetGPUUsage(); }
int GetRealRAMPercent()  { return (int)_FreqControl::GetMemPercent(); }

// ============================================================================
// DrawCircleGauge（原版 MainDefinition.h 第192-204行）
// ============================================================================
void DrawCircleGauge(ImVec2 center, float radius, float progress, ImU32 color, float thickness) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;
    progress = ImClamp(progress, 0.0f, 1.0f);
    window->DrawList->PathArcTo(center, radius, 0.0f, IM_PI * 2.0f, 32);
    window->DrawList->PathStroke(IM_COL32(60, 60, 66, 100), 0, thickness);
    if (progress > 0.0f) {
        float angle_start = -IM_PI * 0.5f;
        float angle_end = angle_start + progress * IM_PI * 2.0f;
        window->DrawList->PathArcTo(center, radius, angle_start, angle_end, 32);
        window->DrawList->PathStroke(color, 0, thickness);
    }
}

// ============================================================================
// DrawSystemInfoCard（原版 MainDefinition.h 第206-369行）
// ============================================================================
bool DrawSystemInfoCard(SystemInfoType type, float value, const ImVec2& size) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

    const char* title = "";
    const char* unit = "";
    float max_value = 100.0f;

    switch (type) {
        case SystemInfoType::FPS:
            title = "FPS";
            unit = "fps";
            max_value = 240.0f;
            break;
        case SystemInfoType::CPU:
            title = "CPU";
            unit = "%";
            max_value = 100.0f;
            break;
        case SystemInfoType::GPU:
            title = "GPU";
            unit = "%";
            max_value = 100.0f;
            break;
        case SystemInfoType::RAM:
            title = "RAM";
            unit = "%";
            max_value = 100.0f;
            break;
    }

    const ImGuiID id = window->GetID(title);
    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size_final = ImGui::CalcItemSize(size, 0.0f, 0.0f);
    const ImRect bb(pos, ImVec2(pos.x + size_final.x, pos.y + size_final.y));

    ImGui::ItemSize(size_final, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, 0);

    bool* is_active = ImGui::GetStateStorage()->GetBoolRef(id, false);
    if (pressed) *is_active = !(*is_active);

    // 动画效果
    float* anim_y = ImGui::GetStateStorage()->GetFloatRef(id + 0x1000, 0.0f);
    float* anim_scale = ImGui::GetStateStorage()->GetFloatRef(id + 0x2000, 1.0f);
    const float dt = g.IO.DeltaTime;
    const float speed = 8.0f;

    float target_y = (hovered && !held) ? -2.0f : 0.0f;
    *anim_y = ImLerp(*anim_y, target_y, dt * speed);

    float target_scale = (held && hovered) ? 0.98f : 1.0f;
    *anim_scale = ImLerp(*anim_scale, target_scale, dt * speed);

    ImVec2 center = bb.GetCenter();
    ImVec2 animated_size = ImVec2(size_final.x * *anim_scale, size_final.y * *anim_scale);
    ImRect animated_bb(
        ImVec2(center.x - animated_size.x * 0.5f, center.y - animated_size.y * 0.5f + *anim_y),
        ImVec2(center.x + animated_size.x * 0.5f, center.y + animated_size.y * 0.5f + *anim_y)
    );

    // Mac 黑色毛玻璃风格配色
    ImU32 col_bg, col_border, col_text, col_sub_text;
    if (held && hovered) {
        col_bg = IM_COL32(28, 28, 32, 220);
        col_border = IM_COL32(80, 80, 90, 200);
        col_text = IM_COL32(255, 255, 255, 255);
        col_sub_text = IM_COL32(255, 255, 255, 153);
    } else if (*is_active) {
        col_bg = IM_COL32(0, 122, 255, 30);
        col_border = IM_COL32(0,122,255,255);
        col_text = IM_COL32(255, 255, 255, 255);
        col_sub_text = IM_COL32(255, 255, 255, 180);
    } else if (hovered) {
        col_bg = IM_COL32(44, 44, 48, 200);
        col_border = IM_COL32(60, 60, 66, 200);
        col_text = IM_COL32(235, 235, 240, 255);
        col_sub_text = IM_COL32(235, 235, 240, 153);
    } else {
        col_bg = IM_COL32(20, 20, 24, 160);
        col_border = IM_COL32(50, 50, 55, 120);
        col_text = IM_COL32(200, 200, 205, 255);
        col_sub_text = IM_COL32(200, 200, 205, 140);
    }

    // 绘制背景和边框
    ImGui::RenderFrame(animated_bb.Min, animated_bb.Max, col_bg, true, 10.0f);
    window->DrawList->AddRect(animated_bb.Min, animated_bb.Max, col_border, 10.0f, 0, 1.0f);
    ImGui::RenderNavCursor(animated_bb, id);

    // 计算圆环位置（居中）
    ImVec2 content_center = ImVec2(
        animated_bb.Min.x + (animated_bb.Max.x - animated_bb.Min.x) * 0.5f,
        animated_bb.Min.y + (animated_bb.Max.y - animated_bb.Min.y) * 0.5f
    );

    float gauge_radius = ImMin(size_final.x, size_final.y) * 0.30f;
    float progress = value / max_value;

    // 圆环颜色
    ImU32 gauge_color;
    switch (type) {
        case SystemInfoType::FPS:
            gauge_color = IM_COL32(52, 199, 89, 255);   // Mac 绿色
            break;
        case SystemInfoType::CPU:
            gauge_color = IM_COL32(255, 159, 10, 255);  // Mac 橙色
            break;
        case SystemInfoType::GPU:
            gauge_color = IM_COL32(191, 90, 242, 255);  // Mac 紫色
            break;
        case SystemInfoType::RAM:
            gauge_color = IM_COL32(0, 122, 255, 255);   // Mac 蓝色
            break;
        default:
            gauge_color = IM_COL32(0, 122, 255, 255);
            break;
    }

    // 绘制圆环
    DrawCircleGauge(content_center, gauge_radius, progress, gauge_color, 5.0f);

    // 绘制数值（居中，大字）
    char value_buf[16];
    if (type == SystemInfoType::FPS) {
        snprintf(value_buf, sizeof(value_buf), "%.0f", value);
    } else {
        snprintf(value_buf, sizeof(value_buf), "%.0f", value);
    }

    ImVec2 value_size = ImGui::CalcTextSize(value_buf);
    ImVec2 title_size = ImGui::CalcTextSize(title);

    // 计算文本整体高度（数值 + 标题）
    float text_spacing = 2.0f;
    float total_text_height = value_size.y + text_spacing + title_size.y;

    // 数值位置（居中偏上）
    ImVec2 value_pos = ImVec2(
        content_center.x - value_size.x * 0.5f,
        content_center.y - total_text_height * 0.5f
    );

    // 标题位置（数值下方）
    ImVec2 title_pos = ImVec2(
        content_center.x - title_size.x * 0.5f,
        value_pos.y + value_size.y + text_spacing
    );

    // 数值使用亮色
    ImU32 value_color = (*is_active || hovered || held) ? IM_COL32(255, 255, 255, 255) : IM_COL32(235, 235, 240, 255);
    window->DrawList->AddText(value_pos, value_color, value_buf);

    // 标题使用副文本颜色
    window->DrawList->AddText(title_pos, col_sub_text, title);

    return pressed;
}

// ============================================================================
// BeginGlassCard / EndGlassCard（原版 MainDefinition.h 第857-887行）
// ============================================================================
bool BeginGlassCard(const char* label, const ImVec2& size, bool border, ImGuiWindowFlags flags) {
    ImGuiStyle& style = ImGui::GetStyle();
    static ImVec4 saved_child_bg;
    static ImVec4 saved_border;
    static float saved_rounding;
    static float saved_border_size;
    static ImVec2 saved_padding;
    saved_child_bg = style.Colors[ImGuiCol_ChildBg];
    saved_border = style.Colors[ImGuiCol_Border];
    saved_rounding = style.ChildRounding;
    saved_border_size = style.ChildBorderSize;
    saved_padding = style.WindowPadding;
    style.Colors[ImGuiCol_ChildBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.5f);
    style.Colors[ImGuiCol_Border] = ImVec4(1.0f, 1.0f, 1.0f, 0.00f);
    style.ChildRounding = 40.0f;
    style.ChildBorderSize = 1.5f;
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    bool ret = ImGui::BeginChild(label, size, border, flags);
    if (ret) {
    }
    style.Colors[ImGuiCol_ChildBg] = saved_child_bg;
    style.Colors[ImGuiCol_Border] = saved_border;
    style.ChildRounding = saved_rounding;
    style.ChildBorderSize = saved_border_size;
    style.WindowPadding = saved_padding;
    return ret;
}

void EndGlassCard() {
    ImGui::EndChild();
}

// ============================================================================
// ButtonWithIcon（原版 imgui_widgets.cpp 第829-900行）
// ============================================================================
bool ButtonWithIcon(const char* label, const char* subtitle, ImTextureID icon, const ImVec2& size_arg, ImGuiButtonFlags flags)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    const ImVec2 subtitle_size = ImGui::CalcTextSize(subtitle, NULL, true);

    ImVec2 pos = window->DC.CursorPos;
    if ((flags & ImGuiButtonFlags_AlignTextBaseLine) && style.FramePadding.y < window->DC.CurrLineTextBaseOffset)
        pos.y += window->DC.CurrLineTextBaseOffset - style.FramePadding.y;

    // 计算所需尺寸
    float icon_size = 70.0f;           // 图标大小
    float left_margin = 10.0f;         // 图标左边距
    float icon_text_spacing = 10.0f;   // 图标与文本间距
    float text_width = ImMax(label_size.x, subtitle_size.x);
    float text_height = label_size.y + subtitle_size.y + 2.0f;  // 两行文本总高度

    ImVec2 size = ImGui::CalcItemSize(size_arg,
        left_margin + icon_size + icon_text_spacing + text_width + style.FramePadding.x * 2.0f,
        ImMax(icon_size, text_height) + style.FramePadding.y * 2.0f);

    const ImRect bb(pos, pos + size);
    ImGui::ItemSize(size, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id))
        return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, flags);

    ImDrawList* draw_list = window->DrawList;

    // 计算图标位置（垂直居中）
    float icon_x = bb.Min.x + left_margin;
    float icon_y = bb.Min.y + (size.y - icon_size) * 0.5f;
    ImVec2 icon_min(icon_x, icon_y);
    ImVec2 icon_max(icon_x + icon_size, icon_y + icon_size);

    // 渲染图标
    ImU32 icon_tint = hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(220, 220, 220, 255);
    draw_list->AddImage(icon, icon_min, icon_max, ImVec2(0, 0), ImVec2(1, 1), icon_tint);

    // 计算文本位置
    float text_start_x = icon_x + icon_size + icon_text_spacing;
    float total_text_height = label_size.y + subtitle_size.y + 2.0f;
    float text_start_y = bb.Min.y + (size.y - total_text_height) * 0.5f;

    // 渲染主文本（第一行）
    ImVec2 label_pos(text_start_x, text_start_y);
    ImU32 label_color = ImGui::GetColorU32(ImGuiCol_Text);
    if (hovered)
        label_color = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
    draw_list->AddText(label_pos, label_color, label);

    // 渲染副文本（第二行，灰色）
    ImVec2 subtitle_pos(text_start_x, text_start_y + label_size.y + 2.0f);
    ImU32 subtitle_color = IM_COL32(128, 128, 128, 255);  // 灰色
    if (hovered)
        subtitle_color = IM_COL32(160, 160, 160, 255);    // 悬停时稍亮
    draw_list->AddText(subtitle_pos, subtitle_color, subtitle);

    if (g.LogEnabled)
        ImGui::LogSetNextTextDecoration("[", "]");

    return pressed;
}

// ============================================================================
// ButtonTab（原版 imgui_widgets.cpp 第1855-1970行）
// ============================================================================
bool ButtonTab(const char* label, const ImVec2& size_arg, ImGuiButtonFlags flags)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);

    ImVec2 pos = window->DC.CursorPos;
    if ((flags & ImGuiButtonFlags_AlignTextBaseLine) && style.FramePadding.y < window->DC.CurrLineTextBaseOffset)
        pos.y += window->DC.CurrLineTextBaseOffset - style.FramePadding.y;
    ImVec2 size = ImGui::CalcItemSize(size_arg, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);

    const ImRect bb(pos, pos + size);
    ImGui::ItemSize(size, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id))
        return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, flags);

    static ImGuiID current_selected_id = 0;
    static ImVec2 target_rect_pos = ImVec2(0, 0);
    static ImVec2 current_rect_pos = ImVec2(0, 0);
    static ImVec2 target_rect_size = ImVec2(0, 0);
    static ImVec2 current_rect_size = ImVec2(0, 0);
    static float anim_alpha = 0.4f;
    static bool first_frame = true;
    static bool background_drawn = false;

    bool selected = (current_selected_id == id);

    // 默认选中主页
    static bool home_initialized = false;
    static ImGuiID home_id = 0;

    if (id == window->GetID(ICON_FA_HOUSE""))
    {
        home_id = id;
        if (!home_initialized)
        {
            current_selected_id = id;
            home_initialized = true;
        }
    }

    if (first_frame)
    {
        target_rect_pos = pos;
        current_rect_pos = target_rect_pos;
        target_rect_size = size;
        current_rect_size = target_rect_size;
        first_frame = false;
        anim_alpha = 1.0f;
    }

    if (selected)
    {
        target_rect_pos = pos;
        target_rect_size = size;
    }

    float move_t = 0.08f;
    current_rect_pos.x = current_rect_pos.x + (target_rect_pos.x - current_rect_pos.x) * move_t;
    current_rect_pos.y = current_rect_pos.y + (target_rect_pos.y - current_rect_pos.y) * move_t;
    current_rect_size.x = current_rect_size.x + (target_rect_size.x - current_rect_size.x) * move_t;
    current_rect_size.y = current_rect_size.y + (target_rect_size.y - current_rect_size.y) * move_t;

    float target_alpha = selected ? 1.0f : 1.0f;
    if (anim_alpha < target_alpha)
        anim_alpha = ImMin(anim_alpha + g.IO.DeltaTime * 4.0f, target_alpha);
    else if (anim_alpha > target_alpha)
        anim_alpha = ImMax(anim_alpha - g.IO.DeltaTime * 4.0f, target_alpha);

    // 绘制 Mac 蓝色毛玻璃背景矩形
    if (!background_drawn)
    {
        float radius = current_rect_size.y * 0.5f;
        ImColor bg_color = ImColor(0.0f, 122/255.0f, 1.0f, selected ? 0.75f : anim_alpha * 0.5f);
        window->DrawList->AddRectFilled(current_rect_pos, current_rect_pos + current_rect_size, bg_color, radius, ImDrawFlags_RoundCornersRight);
        background_drawn = true;
    }

    // 绘制文本（包含图标）- 水平居中
    ImVec4 text_color = selected ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(89/255.0f, 89/255.0f, 89/255.0f, 1.0f);

    // 水平居中计算
    float text_x = pos.x + (size.x - label_size.x) * 0.5f;
    float text_y = pos.y + (size.y - label_size.y) * 0.5f;
    ImVec2 text_pos = ImVec2(text_x, text_y);

    ImGui::PushStyleColor(ImGuiCol_Text, text_color);
    ImGui::RenderText(text_pos, label);
    ImGui::PopStyleColor();

    // 重置背景绘制标志
    if (id == home_id || id == window->GetID(ICON_FA_GEAR""))
        background_drawn = false;

    if (pressed)
    {
        current_selected_id = id;
        target_rect_pos = pos;
        target_rect_size = size;
        anim_alpha = 1.0f;
    }

    return pressed;
}

// ============================================================================
// HorizontalToggleBar（原版 imgui_widgets.cpp 第3013-3208行）
// ============================================================================
bool HorizontalToggleBar(const char* label, int* current_item, const char* const items[], int items_count)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

    ImGui::PushID(label);
    const ImGuiID id = window->GetID(label);

    // 尺寸参数
    const float bar_height = 60.0f;
    const float toggle_height = 40.0f;  // 高度距顶部10，底部10 = 60 - 10 - 10 = 40
    const float rounding = toggle_height * 0.5f;  // 纯圆角（胶囊形）
    const float left_margin = 10.0f;
    const float right_margin = 10.0f;
    const float toggle_area_width = 300.0f;

    // 文本尺寸
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);

    // 宽度-1自动填满父容器
    ImVec2 size_arg(-1.0f, bar_height);
    ImVec2 actual_size = ImGui::CalcItemSize(size_arg, label_size.x + toggle_area_width + left_margin + right_margin, bar_height);

    const float total_width = actual_size.x;
    const float total_height = actual_size.y;

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect total_bb(pos, pos + ImVec2(total_width, total_height));

    ImGui::ItemSize(total_bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(total_bb, id))
    {
        ImGui::PopID();
        return false;
    }

    // 文本区域（左侧，距离左边框10）
    ImRect text_bb(
        ImVec2(pos.x + left_margin, pos.y),
        ImVec2(pos.x + left_margin + label_size.x, pos.y + total_height)
    );

    // 切换条区域（右侧，距离右边框10，距顶部和底部各10）
    ImRect toggle_bb(
        ImVec2(pos.x + total_width - right_margin - toggle_area_width,
               pos.y + 10.0f),  // 距顶部10
        ImVec2(pos.x + total_width - right_margin,
               pos.y + total_height - 10.0f)  // 距底部10
    );

    // 轨道总有效宽度，等分每个选项
    const float toggle_total_width = toggle_bb.Max.x - toggle_bb.Min.x;
    const float item_width = toggle_total_width / items_count;

    // 交互检测
    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(total_bb, id, &hovered, &held);

    // 处理点击
    bool value_changed = false;
    if (pressed && hovered)
    {
        ImVec2 local_mouse_pos = ImGui::GetMousePos();
        float toggle_left = toggle_bb.Min.x;

        for (int i = 0; i < items_count; i++)
        {
            ImRect item_bb(
                ImVec2(toggle_left + item_width * i, toggle_bb.Min.y),
                ImVec2(toggle_left + item_width * (i + 1), toggle_bb.Max.y)
            );

            if (item_bb.Contains(local_mouse_pos))
            {
                if (*current_item != i)
                {
                    *current_item = i;
                    value_changed = true;
                    ImGui::MarkItemEdited(id);
                }
                break;
            }
        }
    }

    // 持久化激活态
    ImGuiStorage* storage = window->DC.StateStorage;
    bool* is_active = storage->GetBoolRef(id, false);
    if (pressed) *is_active = !(*is_active);

    // 动画状态
    float* animation_pos = storage->GetFloatRef(id + items_count, 0.0f);
    const float target_pos = item_width * (*current_item);
    const float animation_speed = 0.06f;

    if (*animation_pos != target_pos)
    {
        float delta = target_pos - *animation_pos;
        *animation_pos += delta * animation_speed;
        if (fabs(*animation_pos - target_pos) < 0.5f)
            *animation_pos = target_pos;
    }

    // 动画位移与缩放
    float* anim_y = storage->GetFloatRef(id + 0x1000, 0.0f);
    float* anim_scale = storage->GetFloatRef(id + 0x2000, 1.0f);
    const float dt = g.IO.DeltaTime;
    const float speed = 8.0f;

    float target_y = (hovered && !held) ? -2.0f : 0.0f;
    *anim_y = ImLerp(*anim_y, target_y, dt * speed);

    float target_scale = (held && hovered) ? 0.98f : 1.0f;
    *anim_scale = ImLerp(*anim_scale, target_scale, dt * speed);

    ImVec2 center = total_bb.GetCenter();
    ImVec2 animated_size = total_bb.GetSize() * *anim_scale;
    ImRect animated_bb(
        ImVec2(center.x - animated_size.x * 0.5f, center.y - animated_size.y * 0.5f + *anim_y),
        ImVec2(center.x + animated_size.x * 0.5f, center.y + animated_size.y * 0.5f + *anim_y)
    );

    // 重新计算动画后的区域
    ImRect animated_text_bb(
        ImVec2(animated_bb.Min.x + left_margin, animated_bb.Min.y),
        ImVec2(animated_bb.Min.x + left_margin + label_size.x, animated_bb.Max.y)
    );

    ImRect animated_toggle_bb(
        ImVec2(animated_bb.Max.x - right_margin - toggle_area_width,
               animated_bb.Min.y + 10.0f),  // 距顶部10
        ImVec2(animated_bb.Max.x - right_margin,
               animated_bb.Max.y - 10.0f)  // 距底部10
    );
    const float animated_toggle_width = animated_toggle_bb.GetWidth();
    const float animated_item_width = animated_toggle_width / items_count;

    // 配色规则
    ImU32 col_text;
    if (held && hovered) {
        col_text = IM_COL32(255, 255, 255, 255);
    } else if (*is_active) {
        col_text = IM_COL32(255, 255, 255, 255);
    } else if (hovered) {
        col_text = IM_COL32(22, 119, 255, 255);
    } else {
        col_text = IM_COL32(102, 102, 102, 255);
    }

    // 切换条颜色
    ImU32 col_toggle_border = IM_COL32(200, 200, 200, 255);
    ImU32 col_toggle_active = IM_COL32(22, 119, 255, 255);

    // 绘制标签文本（左侧，垂直居中）
    ImVec2 animated_text_pos(
        animated_text_bb.Min.x,
        animated_text_bb.Min.y + (animated_text_bb.GetHeight() - label_size.y) * 0.5f
    );
    window->DrawList->AddText(animated_text_pos, col_text, label);

    // 绘制切换条边框（只有边框，无背景，纯圆角）
    window->DrawList->AddRect(animated_toggle_bb.Min, animated_toggle_bb.Max, col_toggle_border, rounding, 0, 1.5f);

    // 绘制活跃滑块（纯圆角）
    const ImRect slider_bb(
        ImVec2(animated_toggle_bb.Min.x + *animation_pos, animated_toggle_bb.Min.y),
        ImVec2(animated_toggle_bb.Min.x + *animation_pos + animated_item_width, animated_toggle_bb.Max.y)
    );
    window->DrawList->AddRectFilled(slider_bb.Min, slider_bb.Max, col_toggle_active, rounding);

    // 绘制所有选项文本
    for (int i = 0; i < items_count; i++)
    {
        const char* item_text = items[i];
        ImVec2 text_size = ImGui::CalcTextSize(item_text, NULL, true);

        float item_center_x = animated_toggle_bb.Min.x + animated_item_width * i + animated_item_width * 0.5f;
        ImVec2 item_text_pos(
            item_center_x - text_size.x * 0.5f,
            animated_toggle_bb.Min.y + (animated_toggle_bb.GetHeight() - text_size.y) * 0.5f
        );

        bool is_over_slider = (item_center_x >= slider_bb.Min.x && item_center_x <= slider_bb.Max.x);
        ImU32 item_color = is_over_slider ? IM_COL32(255, 255, 255, 255) : IM_COL32(102, 102, 102, 255);

        window->DrawList->AddText(item_text_pos, item_color, item_text);
    }

    ImGui::RenderNavHighlight(total_bb, id);
    ImGui::PopID();
    return value_changed;
}

// ============================================================================
// render_dynamic_island - 灵动岛（窗口上方胶囊，点击切换窗口显示/隐藏）
// ============================================================================
// 灵动岛状态（文件内静态，供 render 和 bounds 查询共用）
static bool g_island_expanded = false;       // 展开状态（窗口隐藏时展开）
static float g_island_expand_t = 0.0f;       // 展开/收起动画进度 0~1
static float g_island_cw = 240.0f;           // 当前宽度
static float g_island_ch = 52.0f;            // 当前高度
static float g_island_x = 0.0f;              // 当前左上角 x
static float g_island_y = 0.0f;              // 当前左上角 y

bool get_dynamic_island_bounds(float outBounds[4]) {
    // 灵动岛始终可点击（即使窗口隐藏，灵动岛也要能点）
    outBounds[0] = g_island_x;
    outBounds[1] = g_island_y;
    outBounds[2] = g_island_x + g_island_cw;
    outBounds[3] = g_island_y + g_island_ch;
    return true;
}

void render_dynamic_island() {
    // 局部动画状态（仅渲染用）
    static float hover_glow = 0.0f;        // 悬停光晕强度
    static float press_scale = 1.0f;       // 按压缩放
    static float dot_pulse = 0.0f;         // 收起态圆点呼吸

    const float dt = ImGui::GetIO().DeltaTime;

    // 展开/收起动画（丝滑 lerp）
    float target_t = g_island_expanded ? 1.0f : 0.0f;
    g_island_expand_t += (target_t - g_island_expand_t) * ImMin(dt * 10.0f, 1.0f);

    // 尺寸参数（调大）
    const float collapsed_w = 240.0f, collapsed_h = 52.0f;
    const float expanded_w = 360.0f, expanded_h = 84.0f;
    g_island_cw = collapsed_w + (expanded_w - collapsed_w) * g_island_expand_t;
    g_island_ch = collapsed_h + (expanded_h - collapsed_h) * g_island_expand_t;
    const float rounding = g_island_ch * 0.5f;

    // 定位：屏幕顶部居中，紧贴窗口上方（间距加大到 24px，更往上）
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    g_island_x = (viewport->Pos.x + viewport->Size.x * 0.5f) - (g_island_cw * 0.5f);
    g_island_y = (viewport->Pos.y + viewport->Size.y * 0.5f) - 400.0f - g_island_ch - 24.0f;

    // 点击区域
    ImVec2 island_pos(g_island_x, g_island_y);
    ImVec2 island_size(g_island_cw, g_island_ch);
    ImRect island_bb(island_pos, island_pos + island_size);

    // === 用独立透明窗口包裹，避免 ImGui 自动创建 Debug 窗口 ===
    ImGui::SetNextWindowPos(island_pos);
    ImGui::SetNextWindowSize(island_size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGuiWindowFlags island_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBackground;
    bool open = ImGui::Begin("##DynamicIslandWin", nullptr, island_flags);

    bool hovered = false, pressed = false, held = false;
    if (open) {
        ImGui::SetCursorScreenPos(island_pos);
        ImGui::InvisibleButton("##DynamicIsland", island_size);
        hovered = ImGui::IsItemHovered();
        pressed = ImGui::IsItemClicked();
        held = ImGui::IsItemActive();

        // 点击切换
        if (pressed) {
            g_island_expanded = !g_island_expanded;
            MainAuraOne = !g_island_expanded;  // 展开时隐藏窗口，收起时显示窗口
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();

    // 悬停光晕动画
    float target_glow = hovered ? 1.0f : 0.0f;
    hover_glow += (target_glow - hover_glow) * ImMin(dt * 12.0f, 1.0f);

    // 按压缩放动画
    float target_scale = (held && hovered) ? 0.93f : 1.0f;
    press_scale += (target_scale - press_scale) * ImMin(dt * 18.0f, 1.0f);

    // 圆点呼吸
    dot_pulse += dt * 3.0f;

    // === 绘制（用 ForegroundDrawList，确保在最上层）===
    ImDrawList* draw_list = ImGui::GetForegroundDrawList();

    // 计算缩放后的矩形（以中心缩放）
    ImVec2 center = island_bb.GetCenter();
    ImVec2 scaled_half = ImVec2(g_island_cw * press_scale * 0.5f, g_island_ch * press_scale * 0.5f);
    ImRect draw_bb(center - scaled_half, center + scaled_half);

    // 悬停蓝色光晕
    if (hover_glow > 0.01f) {
        for (int i = 3; i >= 1; i--) {
            float glow_r = i * 7.0f * hover_glow;
            int alpha = (int)(14.0f * hover_glow * (4 - i));
            draw_list->AddRect(
                ImVec2(draw_bb.Min.x - glow_r, draw_bb.Min.y - glow_r),
                ImVec2(draw_bb.Max.x + glow_r, draw_bb.Max.y + glow_r),
                IM_COL32(0, 122, 255, alpha),
                rounding + glow_r, 0, 1.5f
            );
        }
    }

    // 主体黑色背景
    draw_list->AddRectFilled(draw_bb.Min, draw_bb.Max, IM_COL32(0, 0, 0, 245), rounding);

    // 边框微光
    ImU32 border_col = IM_COL32(60, 60, 60, 80);
    if (hover_glow > 0.01f) {
        int a = (int)(80 + 120 * hover_glow);
        border_col = IM_COL32(0, 122, 255, a);
    }
    draw_list->AddRect(draw_bb.Min, draw_bb.Max, border_col, rounding, 0, 1.0f);

    // 内容绘制
    if (g_island_expand_t < 0.5f) {
        // === 收起态：两个小圆点 ===
        float dot_alpha = 1.0f - g_island_expand_t * 2.0f;
        float dot_r = 6.0f;
        float pulse = 0.8f + 0.2f * sinf(dot_pulse);
        int dot_a = (int)(255 * dot_alpha * pulse);
        float gap = 20.0f;
        ImVec2 dot_center = center;
        draw_list->AddCircleFilled(
            ImVec2(dot_center.x - gap * 0.5f, dot_center.y),
            dot_r, IM_COL32(255, 255, 255, dot_a), 16
        );
        draw_list->AddCircleFilled(
            ImVec2(dot_center.x + gap * 0.5f, dot_center.y),
            dot_r, IM_COL32(255, 255, 255, dot_a), 16
        );
    }

    if (g_island_expand_t > 0.3f) {
        // === 展开态：眼睛图标 + 文字 ===
        float content_alpha = ImMin((g_island_expand_t - 0.3f) / 0.4f, 1.0f);
        int text_a = (int)(255 * content_alpha);

        // 眼睛图标
        const char* eye_icon = g_island_expanded ? ICON_FA_EYE_SLASH"" : ICON_FA_EYE"";
        ImVec2 icon_size = ImGui::CalcTextSize(eye_icon);
        float icon_x = center.x - 80.0f;
        float icon_y = center.y - icon_size.y * 0.5f;
        draw_list->AddText(ImVec2(icon_x, icon_y), IM_COL32(0, 122, 255, text_a), eye_icon);

        // 文字
        const char* label = g_island_expanded ? "\xe7\x82\xb9\xe5\x87\xbb\xe5\xb1\x95\xe5\xbc\x80" : "\xe7\x82\xb9\xe5\x87\xbb\xe9\x9a\x90\xe8\x97\x8f"; // 点击展开 / 点击隐藏
        ImVec2 label_size = ImGui::CalcTextSize(label);
        float label_x = icon_x + icon_size.x + 10.0f;
        float label_y = center.y - label_size.y * 0.5f;
        draw_list->AddText(ImVec2(label_x, label_y), IM_COL32(255, 255, 255, text_a), label);
    }
}

// ============================================================================
// render_window（原版 MainUI.h 第1-270行，严格逐行移植）
// ============================================================================
void render_window() {
    // === 菜单展开动画（MainUI.h 第1-19行）===
    if (MainAuraOne) {
        if (!last_menu_state) {
            last_menu_state = true;
        }
        float delta = ImGui::GetIO().DeltaTime;
        menu_expand += (1.0f - menu_expand) * delta * 12.0f;
        menu_expand = fminf(1.0f, menu_expand);
    } else {
        if (last_menu_state) {
            last_menu_state = false;
        }
        float delta = ImGui::GetIO().DeltaTime;
        menu_expand += (0.0f - menu_expand) * delta * 15.0f;
        menu_expand = fmaxf(0.0f, menu_expand);
    }
    if (menu_expand < 0.01f && !MainAuraOne) {
        UpdateBlurWindow({}, {}, 0, false, 0);
        return;
    }
    float width_factor = menu_expand;
    float min_width = 2.0f;
    float current_width = min_width + (1200.0f - min_width) * width_factor;

    // === 标签切换动画（MainUI.h 第23-39行）===
    static int Tab_Main = 1;
    static int prev_Tab_Main = 1;
    static float tab_alpha = 1.0f;
    static bool tab_is_animating = false;
    static float 模糊强度 = 0.8;
    if (prev_Tab_Main != Tab_Main) {
        tab_is_animating = true;
        tab_alpha = 0.0f;
        prev_Tab_Main = Tab_Main;
    }
    if (tab_is_animating) {
        tab_alpha += ImGui::GetIO().DeltaTime * 2.0f;
        if (tab_alpha >= 1.0f) {
            tab_alpha = 1.0f;
            tab_is_animating = false;
        }
    }

    // === 窗口样式（MainUI.h 第40-53行）===
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 40.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGuiWindowFlags mainWindowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoMove;
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 window_pos = ImVec2((viewport->Pos.x + viewport->Size.x * 0.5f) - (current_width * 0.5f),(viewport->Pos.y + viewport->Size.y * 0.5f) - 400.0f);
    ImVec2 window_size = ImVec2(current_width, 800.0f);
    ImGui::SetNextWindowPos(window_pos);
    ImGui::SetNextWindowSize(ImVec2(current_width, 800.0f));
    UpdateBlurWindow(window_pos, window_size, 40.0f, true, 255);
    float content_alpha = fminf(1.0f, (width_factor - 0.2f) / 0.6f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, content_alpha);
    if (ImGui::Begin("MainAuraNexusUI", nullptr, mainWindowFlags)) {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        // === 顶部卡片 BidirectionalCard1（MainUI.h 第56-78行）===
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("MainAuraNexusUI_BidirectionalCard1", ImVec2(-1, 100), false, ImGuiWindowFlags_NoScrollbar);
        ImVec2 child_pos = ImGui::GetWindowPos();
        ImVec2 child_size = ImGui::GetWindowSize();
        draw_list->AddRectFilled(child_pos, ImVec2(child_pos.x + child_size.x, child_pos.y + child_size.y), ImGui::ColorConvertFloat4ToU32(ImVec4(255/255.f, 255/255.f, 255/255.f, 0.5f)), 40.0f, ImDrawFlags_RoundCornersTop);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.00f, 0.00f, 0.00f, 0.00f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::SetCursorPosX(10.0f);ImGui::SetCursorPosY(10.0f);
        float CardWidth1 = ImGui::GetContentRegionAvail().x - 10.0f;
        float CardHeight1 = ImGui::GetContentRegionAvail().y - 10.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));
        if (ImGui::BeginChild("MainAuraNexusUI_Card1", ImVec2(CardWidth1, CardHeight1), false, ImGuiWindowFlags_NoScrollbar)) {
            if (ButtonWithIcon("\xe7\xbd\x91\xe6\x98\x93\xe4\xba\x91", "\xe8\xa3\xb8\xe5\xa5\x94\xe5\x8a\x9f\xe8\x83\xbd", LOGO, ImVec2(300, -1))) { }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        // === 左侧标签栏 BidirectionalCard2（MainUI.h 第79-103行）===
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("MainAuraNexusUI_BidirectionalCard2", ImVec2(200, -1), false, ImGuiWindowFlags_NoScrollbar);
        ImVec2 child_pos1 = ImGui::GetWindowPos();
        ImVec2 child_size1 = ImGui::GetWindowSize();
        draw_list->AddRectFilled(child_pos1, ImVec2(child_pos1.x + child_size1.x, child_pos1.y + child_size1.y), ImGui::ColorConvertFloat4ToU32(ImVec4(255/255.f, 255/255.f, 255/255.f, 0.5f)), 40.0f, ImDrawFlags_RoundCornersBottomLeft);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.00f, 0.00f, 0.00f, 0.00f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::SetCursorPosX(0.0f);ImGui::SetCursorPosY(15.0f);
        float CardWidth2 = ImGui::GetContentRegionAvail().x;
        float CardHeight2 = ImGui::GetContentRegionAvail().y - 10.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));
        if (ImGui::BeginChild("MainAuraNexusUI_Card2", ImVec2(CardWidth2, CardHeight2), false, ImGuiWindowFlags_NoScrollbar)) {
            if (ButtonTab(ICON_FA_HOUSE"", ImVec2(-1, 80))) { Tab_Main = 1; }
            if (ButtonTab(ICON_FA_CROSSHAIRS"", ImVec2(-1, 80))) { Tab_Main = 2; }
            if (ButtonTab(ICON_FA_GEAR"", ImVec2(-1, 80))) { Tab_Main = 3; }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        ImGui::SameLine();

        // === 右侧内容区 BidirectionalCard3（MainUI.h 第105-265行）===
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("MainAuraNexusUI_BidirectionalCard3", ImVec2(-1, -1), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImVec2 o = origin;
        ImVec2 a = ImVec2(origin.x + 0.5, origin.y + 40);
        ImVec2 b = ImVec2(origin.x + 40, origin.y);
        ImVec2 center = ImVec2(origin.x + 40, origin.y + 40);
        float radius = 40.0f;
        draw_list->PathClear();
        draw_list->PathLineTo(o);
        draw_list->PathLineTo(a);
        draw_list->PathArcTo(center, radius, IM_PI, IM_PI * 1.5f, 16);
        draw_list->PathFillConvex(IM_COL32(255, 255, 255, 128));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.00f, 0.00f, 0.00f, 0.00f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::SetCursorPosX(10.0f);ImGui::SetCursorPosY(15.0f);
        float CardWidth3 = ImGui::GetContentRegionAvail().x - 10.0f;
        float CardHeight3 = ImGui::GetContentRegionAvail().y - 10.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, tab_alpha);
        if (ImGui::BeginChild("MainAuraNexusUI_Card3", ImVec2(CardWidth3, CardHeight3), false, ImGuiWindowFlags_NoScrollbar)) {

            // ===== 标签页 1：主页（MainUI.h 第129-163行）=====
            if (Tab_Main == 1) {
                ImGui::Columns(2, nullptr, false);
                if (BeginGlassCard("HomeCard1", ImVec2(-1, -1), true, ImGuiWindowFlags_NoScrollbar)) {
                    DrawAuraSectionTitle("\xe6\xb8\xb8\xe6\x88\x8f\xe5\x88\x9d\xe5\xa7\x8b\xe5\x8c\x96"); // 游戏初始化
                    ImGui::Spacing();
                    if (ImGui::Button("\xe5\x90\xaf\xe5\x8a\xa8\xe6\xb8\xb8\xe6\x88\x8f\xe5\xb9\xb6\xe6\xb3\xa8\xe5\x85\xa5", ImVec2(-1, 70))) { // 启动游戏并注入
                        std::thread([](){ lb_init_game(); }).detach();
                    }
                    ImGui::Separator();
                    if (ImGui::Button("\xe6\x97\xa0\xe7\x97\x95\xe9\x80\x80\xe5\x87\xba\xe7\xa8\x8b\xe5\xba\x8f", ImVec2(-1, 50))) { // 无痕退出程序
                        std::thread([](){ lb_clean_exit(); }).detach();
                    }
                    ImGui::Separator();
                    ImGui::TextColored(lb_gjc_injected ? ImVec4(0.1f,0.7f,0.1f,1) : ImVec4(0.8f,0.3f,0,1),
                        "\xe8\xbf\x87\xe6\xa3\x80\xe6\xb5\x8b: %s", lb_gjc_status.c_str()); // 过检测
                    ImGui::Separator();
                    ImGui::Text("\xe6\xb8\xb8\xe6\x88\x8f\xe8\xbf\x9b\xe7\xa8\x8b: %s", lb_game_running ? "\xe6\xad\xa3\xe5\xb8\xb8" : "\xe6\x9c\xaa\xe5\xb0\xb1\xe7\xbb\xaa"); // 游戏进程: 正常/未就绪
                    ImGui::Text("\xe5\x9f\xba\xe5\x9d\x80\xe7\x8a\xb6\xe6\x80\x81: %s", lb_base_libUE4 ? "\xe5\xb7\xb2\xe8\x8e\xb7\xe5\x8f\x96" : "\xe6\x9c\xaa\xe8\x8e\xb7\xe5\x8f\x96"); // 基址状态: 已获取/未获取
                }
                EndGlassCard();
                ImGui::NextColumn();
                if (BeginGlassCard("HomeCard2", ImVec2(-1, -1), true, ImGuiWindowFlags_NoScrollbar)) {
                    DrawAuraSectionTitle("\xe7\xb3\xbb\xe7\xbb\x9f\xe4\xbf\xa1\xe6\x81\xaf"); // 系统信息
                    ImGui::Spacing();
                    ImGui::Text("FPS: %d", GetRealFPS());
                    ImGui::Text("CPU: %d%%", GetRealCPU());
                    ImGui::Text("GPU: %d%%", GetRealGPU());
                    ImGui::Text("\xe5\x86\x85\xe5\xad\x98: %d%%", GetRealRAMPercent()); // 内存
                    ImGui::Separator();
                    const char* Antirecordingscreen[] = {"\xe5\x8f\xaf\xe5\xbd\x95\xe5\xb1\x8f", "\xe9\x98\xb2\xe5\xbd\x95\xe5\xb1\x8f"}; // 可录屏/防录屏
                    if (HorizontalToggleBar("\xe9\x98\xb2\xe5\xbd\x95\xe5\xb1\x8f\xe8\xae\xbe\xe7\xbd\xae", &Prevent, Antirecordingscreen, 2)) { } // 防录屏设置
                }
                EndGlassCard();
                ImGui::Columns(1);
            }
            // ===== 标签页 2：裸奔功能（MainUI.h 第164-228行）=====
            else if (Tab_Main == 2) {
                ImGui::Columns(2, nullptr, false);
                if (BeginGlassCard("LB_Card1", ImVec2(-1, -1), true, ImGuiWindowFlags_NoScrollbar)) {
                    DrawAuraSectionTitle("\xe5\x8a\x9f\xe8\x83\xbd\xe5\xbc\x80\xe5\x85\xb3"); // 功能开关
                    ImGui::Spacing();
                    bool features_enabled = lb_game_ready;
                    ImGui::BeginDisabled(!features_enabled);
                    bool luna = lb_feature_luna;
                    if (ImGui::Checkbox("\xe9\x9c\xb2\xe5\xa8\x9c\xe5\x86\x85\xe9\x80\x8f", &luna)) lb_feature_luna = luna; // 露娜内透
                    ImGui::Separator();
                    bool head = lb_feature_head;
                    if (ImGui::Checkbox("\xe5\xa4\xb4\xe9\x83\xa8\xe8\x8c\x83\xe5\x9b\xb4", &head)) lb_feature_head = head; // 头部范围
                    ImGui::SameLine(); ImGui::SetNextItemWidth(120.0f);
                    float hv = lb_head_val; if (ImGui::SliderFloat("##head", &hv, 50, 150)) lb_head_val = hv;
                    ImGui::Separator();
                    bool body = lb_feature_body;
                    if (ImGui::Checkbox("\xe8\xba\xab\xe4\xbd\x93\xe8\x8c\x83\xe5\x9b\xb4", &body)) lb_feature_body = body; // 身体范围
                    ImGui::SameLine(); ImGui::SetNextItemWidth(120.0f);
                    float bv = lb_body_val; if (ImGui::SliderFloat("##body", &bv, 50, 150)) lb_body_val = bv;
                    ImGui::Separator();
                    static bool abd_enabled = false;
                    static float abd_val = 99.0f;
                    if (ImGui::Checkbox("\xe8\x85\xb9\xe9\x83\xa8\xe8\x8c\x83\xe5\x9b\xb4", &abd_enabled)) { // 腹部范围
                        if (abd_enabled) { if (!lb_freeze_running) { lb_abdominal_target=abd_val; lb_freeze_running=true; lb_freeze_thread=std::thread(lb_freeze_thread_func); lb_freeze_thread.detach(); } }
                        else lb_freeze_running = false;
                    }
                    ImGui::SameLine(); ImGui::SetNextItemWidth(120.0f);
                    if (ImGui::SliderFloat("##abd", &abd_val, 50, 150)) lb_abdominal_target = abd_val;
                    ImGui::Separator();
                    bool bullet = lb_feature_bullet;
                    if (ImGui::Checkbox("\xe5\xad\x90\xe5\xbc\xb9\xe6\x8d\xae\xe7\x82\xb9", &bullet)) { // 子弹据点
                        lb_feature_bullet = bullet;
                        if (bullet && lb_bullet_addr) { lb_writeDword(lb_bullet_addr, lb_bullet_val); lb_bullet_frozen=true; } else lb_bullet_frozen=false;
                    }
                    ImGui::SameLine();
                    ImGui::TextColored(lb_bullet_frozen ? ImVec4(0.1f,0.7f,0.1f,1) : ImVec4(0.8f,0.3f,0,1), lb_bullet_frozen ? "\xe5\xb7\xb2\xe5\xbc\x80\xe5\x90\xaf" : "\xe6\x9c\xaa\xe5\xbc\x80\xe5\x90\xaf"); // 已开启/未开启
                    ImGui::Separator();
                    bool norecoil = lb_feature_norecoil;
                    if (ImGui::Checkbox("\xe5\x85\xa8\xe5\xb1\x80\xe6\x97\xa0\xe5\x90\x8e", &norecoil)) { // 全局无后
                        lb_feature_norecoil = norecoil;
                        if (norecoil && lb_norecoil_addr) { lb_writeDword(lb_norecoil_addr, lb_norecoil_val); lb_norecoil_frozen=true; } else lb_norecoil_frozen=false;
                    }
                    ImGui::SameLine();
                    ImGui::TextColored(lb_norecoil_frozen ? ImVec4(0.1f,0.7f,0.1f,1) : ImVec4(0.8f,0.3f,0,1), lb_norecoil_frozen ? "\xe5\xb7\xb2\xe5\xbc\x80\xe5\x90\xaf" : "\xe6\x9c\xaa\xe5\xbc\x80\xe5\x90\xaf");
                    ImGui::Separator();
                    bool tp = lb_feature_thirdperson;
                    if (ImGui::Checkbox("\xe7\xac\xac\xe4\xb8\x89\xe4\xba\xba\xe7\xa7\xb0", &tp)) { // 第三人称
                        lb_feature_thirdperson = tp;
                        if (tp && lb_thirdperson_addr) { lb_writeDword(lb_thirdperson_addr, lb_thirdperson_val); lb_thirdperson_frozen=true; if(!lb_thirdperson_thread.joinable()){lb_thirdperson_thread=std::thread(lb_thirdperson_thread_func);lb_thirdperson_thread.detach();} } else lb_thirdperson_frozen=false;
                    }
                    ImGui::SameLine();
                    ImGui::TextColored(lb_thirdperson_frozen ? ImVec4(0.1f,0.7f,0.1f,1) : ImVec4(0.8f,0.3f,0,1), lb_thirdperson_frozen ? "\xe5\xb7\xb2\xe5\xbc\x80\xe5\x90\xaf" : "\xe6\x9c\xaa\xe5\xbc\x80\xe5\x90\xaf");
                    ImGui::Separator();
                    bool nt = lb_feature_neitou;
                    if (ImGui::Checkbox("\xe5\x85\xa8\xe5\xb1\x80\xe5\x86\x85\xe9\x80\x8f", &nt)) { // 全局内透
                        lb_feature_neitou = nt;
                        if (nt && !lb_neitou_running) { lb_neitou_world_ptr=lb_base_libUE4?lb_base_libUE4:0; lb_neitou_thread=std::thread(lb_neitou_thread_func); lb_neitou_thread.detach(); }
                    }
                    ImGui::SameLine();
                    ImGui::TextColored(lb_neitou_running ? ImVec4(0.1f,0.7f,0.1f,1) : ImVec4(0.8f,0.3f,0,1), "%s", lb_neitou_status.c_str());
                    ImGui::EndDisabled();
                }
                EndGlassCard();
                ImGui::Columns(1);
            }
            // ===== 标签页 3：设置（MainUI.h 第229-257行）=====
            else if (Tab_Main == 3) {
                ImGui::Columns(2, nullptr, false);
                if (BeginGlassCard("SetCard1", ImVec2(-1, -1), true, ImGuiWindowFlags_NoScrollbar)) {
                    DrawAuraSectionTitle("\xe6\x98\xbe\xe7\xa4\xba\xe8\xae\xbe\xe7\xbd\xae"); // 显示设置
                    ImGui::Spacing();
                    ImGui::SliderFloat("\xe5\xb8\xa7\xe7\x8e\x87", &FPSControlSize, 0.0f, 165.0f, "%.0f"); // 帧率
                    ImGui::Separator();
                    ImGui::SliderFloat("\xe6\xa8\xa1\xe7\xb3\x8a\xe5\xbc\xba\xe5\xba\xa6", &模糊强度, 0, 1, "%.1f"); // 模糊强度
                    ImGui::Separator();
                    const char* background_modes[] = {"\xe6\x9c\x89\xe5\x90\x8e\xe5\x8f\xb0", "\xe6\x97\xa0\xe5\x90\x8e\xe5\x8f\xb0"}; // 有后台/无后台
                    if (HorizontalToggleBar("\xe6\x97\xa0\xe5\x90\x8e\xe5\x8f\xb0\xe8\xae\xbe\xe7\xbd\xae", &background_mode, background_modes, 2)) { } // 无后台设置
                }
                EndGlassCard();
                ImGui::NextColumn();
                if (BeginGlassCard("SetCard2", ImVec2(-1, -1), true, ImGuiWindowFlags_NoScrollbar)) {
                    DrawAuraSectionTitle("\xe7\xbb\x98\xe5\x9b\xbe\xe8\xae\xbe\xe7\xbd\xae"); // 绘图设置
                    ImGui::Spacing();
                    ImGui::SliderFloat("\xe4\xba\xba\xe7\x89\xa9\xe7\xbb\x98\xe5\x9b\xbe\xe5\xa4\xa7\xe5\xb0\x8f", &ImGuiDrawESP, 0.3, 1.5, "%.1f"); // 人物绘图大小
                    ImGui::Separator();
                    ImGui::SliderFloat("\xe4\xb8\x96\xe7\x95\x8c\xe7\xbb\x98\xe5\x9b\xbe\xe5\xa4\xa7\xe5\xb0\x8f", &ImGuiDrawESP2, 0.3, 1.5, "%.1f"); // 世界绘图大小
                    ImGui::Separator();
                    if (ImGui::Button("\xe4\xbf\x9d\xe5\xad\x98\xe9\x85\x8d\xe7\xbd\xae", ImVec2(-1, 50))) { } // 保存配置
                    ImGui::Separator();
                    if (ImGui::Button("\xe5\x8a\xa0\xe8\xbd\xbd\xe9\x85\x8d\xe7\xbd\xae", ImVec2(-1, 50))) { } // 加载配置
                }
                EndGlassCard();
                ImGui::Columns(1);
            }
            ImGui::PopStyleVar();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }
    ImGui::End();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

} // namespace aura_ui
