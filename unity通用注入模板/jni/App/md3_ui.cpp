//
// MD3 UI 渲染模块实现
// 完整 Material Design 3 风格组件 + 现代化动画系统
// 遵循 MD3 motion 规范：emphasized easing、100-300ms 时长、state layer、ripple
//

// 启用 ImGui 数学运算符重载（必须在所有 imgui.h include 之前定义）
#define IMGUI_DEFINE_MATH_OPERATORS

#include "md3_ui.hpp"
#include "imgui_internal.h"

#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <unordered_map>

namespace md3_ui {

// ============================================================================
// 全局配色
// ============================================================================
static md3::palette_bundle_t* g_palette = nullptr;
static md3::scheme* g_scheme = nullptr;

md3::scheme const& get_scheme() { return *g_scheme; }

// ============================================================================
// MD3 缓动函数（参考 m3 spec: cubic-bezier(0.2, 0, 0, 1) 等）
// ============================================================================
// Emphasized（最常用，进入+退出）：cubic-bezier(0.2, 0, 0, 1)
static float ease_emphasized(float t) noexcept {
    // 用 Bezier 数值近似（避免依赖外部库）
    // 简化：4 段近似，足够顺滑
    return 1.0f - std::pow(1.0f - t, 4.0f) * (1.0f - t * 0.2f);
}
// Emphasized decelerate（进入元素）：cubic-bezier(0.05, 0.7, 0.1, 1)
static float ease_decel(float t) noexcept {
    return 1.0f - std::pow(1.0f - t, 3.5f);
}
// Emphasized accelerate（退出元素）：cubic-bezier(0.3, 0, 0.8, 0.15)
static float ease_accel(float t) noexcept {
    return std::pow(t, 3.0f);
}
// Standard：cubic-bezier(0.2, 0, 0, 1) 的轻量版
static float ease_standard(float t) noexcept {
    return 1.0f - std::pow(1.0f - t, 3.0f);
}

// 帧率无关的指数趋近（speed 越大越快，约等于 1/speed 秒到达 63%）
static float approach(float current, float target, float speed) noexcept {
    float const dt = ImGui::GetIO().DeltaTime;
    float const factor = 1.0f - std::exp(-speed * dt);
    return current + (target - current) * factor;
}

// 时长驱动的动画（duration 秒内从 0→1，使用 emphasized 缓动）
static float timed_progress(float& timer, float duration, bool& running) noexcept {
    if (!running) return 1.0f;
    timer += ImGui::GetIO().DeltaTime;
    if (timer >= duration) {
        timer = duration;
        running = false;
    }
    float const t = timer / duration;
    return ease_emphasized(t);
}

// ============================================================================
// MD3 Ripple 系统（参考 m3 规范：从点击位置扩散，淡出）
// 多 drop 支持，每个 drop 有独立的 grow + fade 生命周期
// ============================================================================
struct ripple_drop_t {
    ImVec2 center{};
    float  start_time  = -1.0f;
    float  release_time = -1.0f;
    bool   alive = false;
};

struct ripple_t {
    static constexpr int   k_max_drops        = 6;
    static constexpr float k_grow_duration    = 0.42f; // 扩散时长
    static constexpr float k_fade_duration    = 0.40f; // 淡出时长
    static constexpr float k_alpha_in_dur     = 0.12f; // 透明度上升
    static constexpr float k_max_alpha        = 0.18f; // 最大不透明度（state layer press）
    static constexpr float k_min_press_dur    = 0.20f; // 最短按压时长
    static constexpr float k_hover_max_alpha  = 0.08f; // hover state layer 最大
    static constexpr float k_hover_fade_speed = 14.0f;

    ripple_drop_t drops[k_max_drops]{};
    int           active_press_idx = -1;
    bool          hover_suppressed = false;
    float         hover_alpha = 0.0f;
    bool          hover_on = false;

    void push(ImVec2 pos, float now) {
        for (int i = 0; i < k_max_drops; ++i) {
            if (!drops[i].alive) {
                drops[i].center = pos;
                drops[i].start_time = now;
                drops[i].release_time = -1.0f;
                drops[i].alive = true;
                active_press_idx = i;
                hover_suppressed = false;
                hover_on = true;
                return;
            }
        }
    }
    void release(float now, bool keep_hover = false) {
        if (active_press_idx >= 0 && active_press_idx < k_max_drops) {
            auto& d = drops[active_press_idx];
            if (d.alive && d.release_time < 0.0f) d.release_time = now;
        }
        active_press_idx = -1;
        hover_suppressed = true;
        if (!keep_hover) hover_on = false;
    }
    void set_hover(bool on) noexcept {
        if (!on) { hover_suppressed = false; hover_on = false; }
        else if (!hover_suppressed) hover_on = true;
    }
    void tick_hover(float dt) noexcept {
        float const factor = 1.0f - std::exp(-k_hover_fade_speed * dt);
        float const target = hover_on ? k_hover_max_alpha : 0.0f;
        hover_alpha = hover_alpha + (target - hover_alpha) * std::clamp(factor, 0.0f, 1.0f);
    }
    float get_hover_alpha() const noexcept { return hover_alpha; }

    // 渲染所有 drop（圆形扩散 + 透明度淡出），使用 PathFillConvex
    void render_drops(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, ImU32 base_col, float now) {
        float const w = b.x - a.x;
        float const h = b.y - a.y;
        float const max_radius = std::sqrt(w * w + h * h);
        int const segments = 36;

        for (auto& d : drops) {
            if (!d.alive) continue;
            float const grow_t = std::clamp((now - d.start_time) / k_grow_duration, 0.0f, 1.0f);
            // emphasized decelerate 缓动
            float const eased = ease_decel(grow_t);
            float const radius = eased * max_radius;

            float const min_visible = d.start_time + k_min_press_dur;
            float const fade_start = (d.release_time >= 0.0f) ? std::max(d.release_time, min_visible) : -1.0f;

            float const alpha_in = std::clamp((now - d.start_time) / k_alpha_in_dur, 0.0f, 1.0f);
            float alpha = k_max_alpha * alpha_in;

            if (fade_start >= 0.0f && now >= fade_start) {
                float const fade_t = std::clamp((now - fade_start) / k_fade_duration, 0.0f, 1.0f);
                // emphasized accelerate 缓动（淡出）
                float const fade_eased = ease_accel(fade_t);
                alpha = k_max_alpha * (1.0f - fade_eased);
                if (fade_t >= 1.0f) { d.alive = false; continue; }
            }
            if (alpha < 0.001f || radius < 1.0f) continue;

            ImU32 const col = (base_col & 0x00FFFFFFu) | (ImU32(alpha * 255.0f) << 24);

            // 用 PathFillConvex 画圆，ImGui 会自动按 DrawList 的 clip rect 裁剪
            // 但圆会超出 rect 边界，所以用 PathArcTo + 限制到 rect 内
            // 简化方案：直接画圆，依赖窗口 clip
            dl->PathClear();
            for (int i = 0; i <= segments; ++i) {
                float const ang = (float)i / segments * 2.0f * 3.14159265f;
                ImVec2 const p(d.center.x + std::cos(ang) * radius,
                               d.center.y + std::sin(ang) * radius);
                if (i == 0) dl->PathLineTo(p);
                else dl->PathLineTo(p);
            }
            // 用 AddCircleFilled 更简单且自动抗锯齿
            dl->AddCircleFilled(d.center, radius, col, segments);
        }
    }
    void render(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, ImU32 base_col, float now) {
        tick_hover(ImGui::GetIO().DeltaTime);
        if (hover_alpha > 0.001f) {
            ImU32 const hover_col = (base_col & 0x00FFFFFFu) | (ImU32(hover_alpha * 255.0f) << 24);
            dl->AddRectFilled(a, b, hover_col, rounding);
        }
        render_drops(dl, a, b, rounding, base_col, now);
    }
};

// ============================================================================
// 按元素 ID 管理动画状态（避免全局静态数组下标冲突）
// ============================================================================
struct elem_state_t {
    ripple_t ripple;
    float    hover_t = 0.0f;     // hover 进度（0-1）
    float    press_t = 0.0f;     // press 进度（0-1）
    float    select_t = 0.0f;    // 选中进度（0-1）
    float    scale_t = 0.0f;     // 缩放动画（用于 FAB / 按钮 press 反馈）
    bool     pressing = false;
    bool     cancelled = false;
    ImVec2   press_pos{};
    float    press_time = 0.0f;
};

static std::unordered_map<ImGuiID, elem_state_t> g_states;

static elem_state_t& get_state(ImGuiID id) {
    return g_states[id];
}

// 通用按压行为处理（带 drag 取消、ripple 触发）
// 返回 true 表示这一次释放算作有效点击
static bool handle_press(elem_state_t& s, ImRect const& bb, ImGuiID id) {
    ImGuiContext& g = *GImGui;
    ImVec2 const mp = g.IO.MousePos;
    bool const m_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    bool const m_released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    bool const window_hovered = (g.HoveredWindow == ImGui::GetCurrentWindow());
    bool const hovered = window_hovered && bb.Contains(mp);
    float const now = (float)ImGui::GetTime();

    if (!s.pressing) s.ripple.set_hover(hovered);

    if (m_clicked && hovered && !s.pressing) {
        s.pressing = true;
        s.cancelled = false;
        s.press_pos = mp;
        s.press_time = now;
    }

    bool clicked = false;
    if (s.pressing) {
        if (!s.cancelled) {
            float const dx = mp.x - s.press_pos.x;
            float const dy = mp.y - s.press_pos.y;
            if (dx * dx + dy * dy > 36.0f) { // 6px 拖拽阈值
                s.cancelled = true;
            }
        }
        if (m_released) {
            if (!s.cancelled) {
                s.ripple.push(s.press_pos, now);
                s.ripple.release(now, true);
                clicked = true;
            } else {
                s.ripple.release(now, false);
            }
            s.pressing = false;
        }
    }
    return clicked;
}

// ============================================================================
// 通用绘制工具
// ============================================================================
static ImU32 col_with_alpha(ImU32 base, float alpha) noexcept {
    return (base & 0x00FFFFFFu) | (ImU32(std::clamp(alpha, 0.0f, 1.0f) * 255.0f) << 24);
}

// 圆角矩形阴影（多层渐变模拟 elevation）
static void draw_shadow(ImDrawList* dl, ImVec2 p_min, ImVec2 p_max, float rounding,
                        float elevation, ImU32 rgb = IM_COL32(0, 0, 0, 0)) {
    if (elevation <= 0.0f) return;
    // 阴影颜色默认黑色
    if ((rgb & 0x00FFFFFFu) == 0) rgb = IM_COL32(0, 0, 0, 0);
    int const steps = 6;
    float const base_a = 0.18f * std::min(elevation / 8.0f, 1.0f);
    for (int i = steps; i >= 1; --i) {
        float const t = (float)(i - 1) / (steps - 1);
        float const expand = elevation * t * 0.8f;
        float const alpha = base_a * (1.0f - t) * (1.0f - t);
        ImU32 const c = col_with_alpha(IM_COL32(0, 0, 0, 255), alpha);
        dl->AddRectFilled(ImVec2(p_min.x - expand, p_min.y - expand),
                          ImVec2(p_max.x + expand, p_max.y + expand),
                          c, rounding + expand);
    }
}

// 用 ImVec4 做线性插值
static ImVec4 lerp_vec4(ImVec4 a, ImVec4 b, float t) noexcept {
    return ImVec4(a.x + (b.x - a.x) * t,
                  a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t,
                  a.w + (b.w - a.w) * t);
}
static ImU32 lerp_col32(ImU32 a, ImU32 b, float t) noexcept {
    ImVec4 const va = ImGui::ColorConvertU32ToFloat4(a);
    ImVec4 const vb = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(lerp_vec4(va, vb, t));
}

// ============================================================================
// 初始化
// ============================================================================
void init() {
    // 用 Material Blue (#4079FF) 作为种子色生成 MD3 配色
    md3::argb_t seed{ 0xff, 0x40, 0x79, 0xff };
    g_palette = new md3::palette_bundle_t(md3::palette_bundle_t::from_source(seed));
    g_scheme = new md3::scheme(g_palette->dark);

    // 应用 MD3 样式到 ImGui
    md3::imgui_style::apply(*g_scheme, &ImGui::GetStyle());

    // 额外样式调整（MD3 规范圆角）
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScrollbarRounding = 8;
    style.ScrollbarSize = 8;
    style.CircleTessellationMaxError = 0.02f;
    style.WindowRounding = 28;
    style.ChildRounding = 16;
    style.FrameRounding = 12;
    style.GrabRounding = 12;
    style.PopupRounding = 14;
    style.WindowBorderSize = 0;
    style.FrameBorderSize = 0;
    style.WindowPadding = ImVec2(20, 20);
    style.FramePadding = ImVec2(16, 10);
    style.ItemSpacing = ImVec2(8, 8);
}

// ============================================================================
// MD3 Button（Filled / Tonal / Outlined / Text）
// ============================================================================
enum class button_variant_e {
    filled,      // 实心（primary 背景）
    tonal,       // 次要色调（secondary_container）
    outlined,    // 描边
    text,        // 纯文字
    elevated,    // 带 elevation 的 filled tonal
};

static bool md3_button(const char* label, ImVec2 size, button_variant_e variant, int idx) {
    using namespace md3;
    auto const& sch = *g_scheme;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    // 用 label + idx 生成稳定 ID
    ImGuiID const id = window->GetID(label);
    ImVec2 const pos = window->DC.CursorPos;
    ImRect const bb(pos, pos + size);
    ImGui::ItemSize(size, ImGui::GetStyle().FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return false;

    elem_state_t& s = get_state(id);
    bool const clicked = handle_press(s, bb, id);

    // hover / press 进度（用于 state layer）
    bool const hovered = ImGui::IsItemHovered();
    s.hover_t = approach(s.hover_t, hovered ? 1.0f : 0.0f, 18.0f);
    s.press_t = approach(s.press_t, s.pressing ? 1.0f : 0.0f, 30.0f);

    auto* dl = window->DrawList;
    float const round = 20.0f; // MD3 full button 圆角

    // 选择颜色角色
    argb_t bg_c, content_c, state_c;
    bool   has_bg = true;
    bool   has_border = false;
    bool   has_shadow = false;

    switch (variant) {
        case button_variant_e::filled:
            bg_c = sch.get(scheme_role_e::primary);
            content_c = sch.get(scheme_role_e::on_primary);
            state_c = sch.get(scheme_role_e::on_primary);
            break;
        case button_variant_e::tonal:
            bg_c = sch.get(scheme_role_e::secondary_container);
            content_c = sch.get(scheme_role_e::on_secondary_container);
            state_c = sch.get(scheme_role_e::on_secondary_container);
            break;
        case button_variant_e::outlined:
            bg_c = sch.get(scheme_role_e::surface);
            content_c = sch.get(scheme_role_e::primary);
            state_c = sch.get(scheme_role_e::primary);
            has_border = true;
            break;
        case button_variant_e::text:
            bg_c = sch.get(scheme_role_e::surface);
            content_c = sch.get(scheme_role_e::primary);
            state_c = sch.get(scheme_role_e::primary);
            has_bg = false;
            break;
        case button_variant_e::elevated:
            bg_c = sch.get(scheme_role_e::surface_container_low);
            content_c = sch.get(scheme_role_e::primary);
            state_c = sch.get(scheme_role_e::primary);
            has_shadow = true;
            break;
    }

    // elevation 阴影
    if (has_shadow) {
        draw_shadow(dl, bb.Min, bb.Max, round, 3.0f + s.hover_t * 2.0f);
    }

    // 背景
    if (has_bg) {
        ImU32 const bg_col = IM_COL32(bg_c.r, bg_c.g, bg_c.b, bg_c.a);
        dl->AddRectFilled(bb.Min, bb.Max, bg_col, round);
    }

    // 描边
    if (has_border) {
        argb_t const outline_c = sch.get(scheme_role_e::outline_variant);
        ImU32 const border_col = IM_COL32(outline_c.r, outline_c.g, outline_c.b, outline_c.a);
        dl->AddRect(bb.Min, bb.Max, border_col, round, 0, 1.0f);
    }

    // state layer（hover + press 叠加）
    ImU32 const state_base = IM_COL32(state_c.r, state_c.g, state_c.b, 255);
    float const hover_alpha = 0.08f * s.hover_t;
    float const press_alpha = 0.12f * s.press_t;
    if (hover_alpha > 0.001f) {
        dl->AddRectFilled(bb.Min, bb.Max, col_with_alpha(state_base, hover_alpha), round);
    }
    if (press_alpha > 0.001f) {
        dl->AddRectFilled(bb.Min, bb.Max, col_with_alpha(state_base, press_alpha), round);
    }

    // ripple
    float const now = (float)ImGui::GetTime();
    s.ripple.render(dl, bb.Min, bb.Max, round, state_base, now);

    // 文字（带轻微 scale 反馈，press 时缩小 2%）
    float const scale = 1.0f - 0.02f * s.press_t;
    ImVec2 const text_size = ImGui::CalcTextSize(label);
    ImVec2 const center = bb.GetCenter();
    ImVec2 const text_pos(center.x - text_size.x * 0.5f * scale,
                          center.y - text_size.y * 0.5f * scale);
    ImU32 const text_col = IM_COL32(content_c.r, content_c.g, content_c.b, content_c.a);
    if (scale < 0.999f) {
        // 简化：直接画文字（scale 仅影响位置居中，文字大小不变以避免模糊）
        dl->AddText(text_pos, text_col, label);
    } else {
        dl->AddText(ImVec2(center.x - text_size.x * 0.5f, center.y - text_size.y * 0.5f), text_col, label);
    }

    return clicked;
}

// ============================================================================
// MD3 Switch（带 icon、state layer、spring 感动画）
// ============================================================================
static void md3_switch(bool* v, const char* id_str) {
    using namespace md3;
    auto const& sch = *g_scheme;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;

    ImVec2 const track_size(52, 32);
    ImVec2 const pos = window->DC.CursorPos;
    ImRect const bb(pos, pos + track_size);
    ImGuiID const id = window->GetID(id_str);
    ImGui::ItemSize(track_size, ImGui::GetStyle().FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return;

    elem_state_t& s = get_state(id);
    bool const clicked = handle_press(s, bb, id);
    if (clicked) *v = !*v;

    bool const hovered = ImGui::IsItemHovered();
    s.hover_t = approach(s.hover_t, hovered ? 1.0f : 0.0f, 18.0f);

    // 选中进度（spring 感：用 emphasized 缓动 + 快速度）
    float const target = *v ? 1.0f : 0.0f;
    s.select_t = approach(s.select_t, target, 18.0f);

    auto* dl = window->DrawList;
    float const t = s.select_t;

    // 颜色角色
    argb_t const outline_c = sch.get(scheme_role_e::outline_variant);
    argb_t const primary_c = sch.get(scheme_role_e::primary);
    argb_t const on_primary_c = sch.get(scheme_role_e::on_primary);
    argb_t const surface_c = sch.get(scheme_role_e::surface);
    argb_t const on_surface_c = sch.get(scheme_role_e::on_surface);

    // 轨道高度：off=16, on=24（MD3 规范，开关时高度变化）
    float const track_h = 16.0f + 8.0f * t;
    ImVec2 const track_min(pos.x, pos.y + (track_size.y - track_h) * 0.5f);
    ImVec2 const track_max(pos.x + track_size.x, pos.y + (track_size.y + track_h) * 0.5f);
    float const track_round = track_h * 0.5f;

    // 轨道颜色：off=outline_variant, on=primary
    ImU32 const track_off = IM_COL32(outline_c.r, outline_c.g, outline_c.b, outline_c.a);
    ImU32 const track_on = IM_COL32(primary_c.r, primary_c.g, primary_c.b, primary_c.a);
    ImU32 const track_col = lerp_col32(track_off, track_on, t);
    dl->AddRectFilled(track_min, track_max, track_col, track_round, ImDrawFlags_RoundCornersAll);

    // state layer（在轨道上）
    ImU32 const state_base = IM_COL32(on_surface_c.r, on_surface_c.g, on_surface_c.b, 255);
    ImU32 const state_on = IM_COL32(on_primary_c.r, on_primary_c.g, on_primary_c.b, 255);
    ImU32 const state_col = lerp_col32(state_base, state_on, t);
    float const state_alpha = 0.08f * s.hover_t + 0.12f * s.press_t;
    if (state_alpha > 0.001f) {
        dl->AddRectFilled(track_min, track_max, col_with_alpha(state_col, state_alpha),
                          track_round, ImDrawFlags_RoundCornersAll);
    }

    // ripple（限制在轨道内）
    float const now = (float)ImGui::GetTime();
    s.ripple.render(dl, track_min, track_max, track_round, state_col, now);

    // thumb（滑块）：off=半径8带边框, on=半径12实心
    float const thumb_r = 8.0f + 4.0f * t;
    float const thumb_x = pos.x + 16.0f + (track_size.x - 32.0f) * t;
    float const thumb_y = pos.y + track_size.y * 0.5f;

    if (t < 0.5f) {
        // off 态：白色实心 + outline 描边
        ImU32 const thumb_col = IM_COL32(surface_c.r, surface_c.g, surface_c.b, surface_c.a);
        dl->AddCircleFilled(ImVec2(thumb_x, thumb_y), thumb_r, thumb_col, 24);
        dl->AddCircle(ImVec2(thumb_x, thumb_y), thumb_r, track_off, 24, 2.0f);
    } else {
        // on 态：on_primary 实心
        ImU32 const thumb_col = IM_COL32(on_primary_c.r, on_primary_c.g, on_primary_c.b, on_primary_c.a);
        dl->AddCircleFilled(ImVec2(thumb_x, thumb_y), thumb_r, thumb_col, 24);
    }
}

// ============================================================================
// MD3 Filter Chip（带 checkmark 动画、state layer）
// ============================================================================
static bool md3_chip(const char* label, bool selected, int idx) {
    using namespace md3;
    auto const& sch = *g_scheme;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImVec2 const text_size = ImGui::CalcTextSize(label);
    // 选中时左侧留出 checkmark 空间
    float const check_w = 18.0f;
    ImVec2 const size(text_size.x + 24.0f + check_w * 0.0f, 36.0f);
    ImVec2 const pos = window->DC.CursorPos;
    ImRect const bb(pos, pos + size);

    ImGuiID const id = window->GetID(label);
    ImGui::ItemSize(size, ImGui::GetStyle().FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return false;

    elem_state_t& s = get_state(id);
    bool const clicked = handle_press(s, bb, id);

    bool const hovered = ImGui::IsItemHovered();
    s.hover_t = approach(s.hover_t, hovered ? 1.0f : 0.0f, 18.0f);
    s.press_t = approach(s.press_t, s.pressing ? 1.0f : 0.0f, 30.0f);

    float const target = selected ? 1.0f : 0.0f;
    s.select_t = approach(s.select_t, target, 16.0f);
    float const t = s.select_t;

    auto* dl = window->DrawList;
    float const round = 8.0f;

    argb_t const sec_container_c = sch.get(scheme_role_e::secondary_container);
    argb_t const on_sec_container_c = sch.get(scheme_role_e::on_secondary_container);
    argb_t const surface_c = sch.get(scheme_role_e::surface);
    argb_t const on_surface_variant_c = sch.get(scheme_role_e::on_surface_variant);
    argb_t const outline_c = sch.get(scheme_role_e::outline_variant);

    // 背景：off=surface, on=secondary_container
    ImU32 const bg_off = IM_COL32(surface_c.r, surface_c.g, surface_c.b, surface_c.a);
    ImU32 const bg_on = IM_COL32(sec_container_c.r, sec_container_c.g, sec_container_c.b, sec_container_c.a);
    ImU32 const bg_col = lerp_col32(bg_off, bg_on, t);
    dl->AddRectFilled(bb.Min, bb.Max, bg_col, round);

    // 描边（off 态有，on 态无）
    if (t < 0.95f) {
        ImU32 const border_col = IM_COL32(outline_c.r, outline_c.g, outline_c.b,
                                          (int)(outline_c.a * (1.0f - t)));
        dl->AddRect(bb.Min, bb.Max, border_col, round, 0, 1.0f);
    }

    // state layer
    ImU32 const state_base = IM_COL32(on_sec_container_c.r, on_sec_container_c.g, on_sec_container_c.b, 255);
    float const state_alpha = 0.08f * s.hover_t + 0.12f * s.press_t;
    if (state_alpha > 0.001f) {
        dl->AddRectFilled(bb.Min, bb.Max, col_with_alpha(state_base, state_alpha), round);
    }

    // ripple
    float const now = (float)ImGui::GetTime();
    s.ripple.render(dl, bb.Min, bb.Max, round, state_base, now);

    // checkmark（选中时显示，带 scale 动画）
    if (t > 0.05f) {
        ImVec2 const check_center(pos.x + 12, pos.y + size.y * 0.5f);
        float const check_scale = t;
        float const r = 5.0f * check_scale;
        // 画一个简单的对勾（两条线）
        ImU32 const check_col = IM_COL32(on_sec_container_c.r, on_sec_container_c.g,
                                         on_sec_container_c.b, (int)(on_sec_container_c.a * t));
        ImVec2 const p1(check_center.x - r, check_center.y);
        ImVec2 const p2(check_center.x - r * 0.3f, check_center.y + r * 0.7f);
        ImVec2 const p3(check_center.x + r, check_center.y - r * 0.7f);
        dl->AddLine(p1, p2, check_col, 1.8f);
        dl->AddLine(p2, p3, check_col, 1.8f);
    }

    // 文字（颜色：off=on_surface_variant, on=on_secondary_container）
    ImU32 const text_off = IM_COL32(on_surface_variant_c.r, on_surface_variant_c.g,
                                    on_surface_variant_c.b, on_surface_variant_c.a);
    ImU32 const text_on = IM_COL32(on_sec_container_c.r, on_sec_container_c.g,
                                   on_sec_container_c.b, on_sec_container_c.a);
    ImU32 const text_col = lerp_col32(text_off, text_on, t);
    // 文字位置：选中时右移给 checkmark 让位
    float const text_x = pos.x + 12.0f + 18.0f * t;
    float const text_y = pos.y + (size.y - text_size.y) * 0.5f;
    dl->AddText(ImVec2(text_x, text_y), text_col, label);

    return clicked;
}

// ============================================================================
// MD3 Card（带 elevation shadow、hover state layer、shape morphing）
// ============================================================================
static void md3_card(const char* id_str, float height, const char* title, const char* subtitle) {
    using namespace md3;
    auto const& sch = *g_scheme;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;

    ImVec2 const size = ImGui::GetContentRegionAvail();
    ImVec2 const pos = window->DC.CursorPos;
    ImRect const bb(pos, pos + ImVec2(size.x, height));
    ImGuiID const id = window->GetID(id_str);
    ImGui::ItemSize(ImVec2(size.x, height), 0);
    if (!ImGui::ItemAdd(bb, id)) return;

    elem_state_t& s = get_state(id);
    bool const hovered = ImGui::IsItemHovered();
    s.hover_t = approach(s.hover_t, hovered ? 1.0f : 0.0f, 12.0f);

    auto* dl = window->DrawList;
    // 圆角 morphing：hover 时从 12 → 16
    float const round = 12.0f + 4.0f * s.hover_t;

    argb_t const container_c = sch.get(scheme_role_e::surface_container);
    argb_t const container_high_c = sch.get(scheme_role_e::surface_container_high);
    argb_t const on_surface_c = sch.get(scheme_role_e::on_surface);
    argb_t const on_surface_variant_c = sch.get(scheme_role_e::on_surface_variant);
    argb_t const primary_c = sch.get(scheme_role_e::primary);
    argb_t const outline_variant_c = sch.get(scheme_role_e::outline_variant);

    // elevation 阴影（hover 时升高）
    float const elevation = 1.0f + 4.0f * s.hover_t;
    draw_shadow(dl, bb.Min, bb.Max, round, elevation);

    // 背景：hover 时从 container → container_high
    ImU32 const bg_low = IM_COL32(container_c.r, container_c.g, container_c.b, container_c.a);
    ImU32 const bg_high = IM_COL32(container_high_c.r, container_high_c.g, container_high_c.b, container_high_c.a);
    ImU32 const bg_col = lerp_col32(bg_low, bg_high, s.hover_t);
    dl->AddRectFilled(bb.Min, bb.Max, bg_col, round);

    // 细描边
    ImU32 const border_col = IM_COL32(outline_variant_c.r, outline_variant_c.g,
                                      outline_variant_c.b, (int)(outline_variant_c.a * 0.6f));
    dl->AddRect(bb.Min, bb.Max, border_col, round, 0, 1.0f);

    // state layer（hover）
    if (s.hover_t > 0.001f) {
        ImU32 const state_base = IM_COL32(on_surface_c.r, on_surface_c.g, on_surface_c.b, 255);
        dl->AddRectFilled(bb.Min, bb.Max, col_with_alpha(state_base, 0.08f * s.hover_t), round);
    }

    // 左侧装饰条（primary 色，hover 时高度增加）
    float const bar_h = (height - 24.0f) * (0.5f + 0.5f * s.hover_t);
    float const bar_y = pos.y + (height - bar_h) * 0.5f;
    ImU32 const bar_col = IM_COL32(primary_c.r, primary_c.g, primary_c.b, primary_c.a);
    dl->AddRectFilled(ImVec2(pos.x, bar_y), ImVec2(pos.x + 4, bar_y + bar_h), bar_col, 2.0f);

    // 标题
    ImU32 const title_col = IM_COL32(on_surface_c.r, on_surface_c.g, on_surface_c.b, on_surface_c.a);
    dl->AddText(ImVec2(pos.x + 16, pos.y + 14), title_col, title);

    // 副标题
    if (subtitle) {
        ImU32 const sub_col = IM_COL32(on_surface_variant_c.r, on_surface_variant_c.g,
                                       on_surface_variant_c.b, on_surface_variant_c.a);
        dl->AddText(ImVec2(pos.x + 16, pos.y + 36), sub_col, subtitle);
    }
}

// ============================================================================
// MD3 FAB（Floating Action Button）- 圆形，带 elevation + ripple
// ============================================================================
static bool md3_fab(const char* icon, ImVec2 pos, float size) {
    using namespace md3;
    auto const& sch = *g_scheme;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImRect const bb(pos, ImVec2(pos.x + size, pos.y + size));
    ImGuiID const id = window->GetID(icon);
    ImGui::ItemSize(ImVec2(size, size), 0);
    if (!ImGui::ItemAdd(bb, id)) return false;

    elem_state_t& s = get_state(id);
    bool const clicked = handle_press(s, bb, id);

    bool const hovered = ImGui::IsItemHovered();
    s.hover_t = approach(s.hover_t, hovered ? 1.0f : 0.0f, 18.0f);
    s.press_t = approach(s.press_t, s.pressing ? 1.0f : 0.0f, 30.0f);

    auto* dl = window->DrawList;
    float const round = size * 0.5f;

    argb_t const primary_c = sch.get(scheme_role_e::primary);
    argb_t const on_primary_c = sch.get(scheme_role_e::on_primary);

    // elevation 阴影（hover 时升高）
    float const elevation = 6.0f + 3.0f * s.hover_t;
    draw_shadow(dl, bb.Min, bb.Max, round, elevation);

    // 背景
    ImU32 const bg_col = IM_COL32(primary_c.r, primary_c.g, primary_c.b, primary_c.a);
    dl->AddRectFilled(bb.Min, bb.Max, bg_col, round);

    // state layer
    ImU32 const state_base = IM_COL32(on_primary_c.r, on_primary_c.g, on_primary_c.b, 255);
    float const state_alpha = 0.08f * s.hover_t + 0.12f * s.press_t;
    if (state_alpha > 0.001f) {
        dl->AddRectFilled(bb.Min, bb.Max, col_with_alpha(state_base, state_alpha), round);
    }

    // ripple
    float const now = (float)ImGui::GetTime();
    s.ripple.render(dl, bb.Min, bb.Max, round, state_base, now);

    // 图标（用文字代替，press 时轻微缩小）
    float const scale = 1.0f - 0.05f * s.press_t;
    ImVec2 const icon_size = ImGui::CalcTextSize(icon);
    ImVec2 const center = bb.GetCenter();
    ImU32 const icon_col = IM_COL32(on_primary_c.r, on_primary_c.g, on_primary_c.b, on_primary_c.a);
    dl->AddText(ImVec2(center.x - icon_size.x * 0.5f * scale,
                       center.y - icon_size.y * 0.5f * scale), icon_col, icon);

    return clicked;
}

// ============================================================================
// MD3 Segmented Button（带选中态动画、ripple）
// ============================================================================
static void md3_segmented(int* selected, const char* const* labels, int count) {
    using namespace md3;
    auto const& sch = *g_scheme;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;

    ImVec2 const pos = window->DC.CursorPos;
    float const avail = ImGui::GetContentRegionAvail().x;
    float const height = 40.0f;
    float const gap = 3.0f;
    float const seg_w = (avail - gap * (count - 1)) / count;

    for (int i = 0; i < count; ++i) {
        float const x0 = pos.x + (seg_w + gap) * i;
        float const x1 = (i == count - 1) ? pos.x + avail : x0 + seg_w;
        ImRect const bb(ImVec2(x0, pos.y), ImVec2(x1, pos.y + height));

        char id_buf[32];
        snprintf(id_buf, sizeof(id_buf), "##seg_%d", i);
        ImGuiID const id = window->GetID(id_buf);
        ImGui::ItemSize(ImVec2(seg_w, height), 0);
        if (!ImGui::ItemAdd(bb, id)) continue;

        elem_state_t& s = get_state(id);
        bool const clicked = handle_press(s, bb, id);
        if (clicked) *selected = i;

        bool const hovered = ImGui::IsItemHovered();
        s.hover_t = approach(s.hover_t, hovered ? 1.0f : 0.0f, 18.0f);
        bool const is_sel = (*selected == i);
        s.select_t = approach(s.select_t, is_sel ? 1.0f : 0.0f, 18.0f);

        auto* dl = window->DrawList;
        float const round = 20.0f;
        bool const is_first = (i == 0);
        bool const is_last = (i == count - 1);

        argb_t const sec_container_c = sch.get(scheme_role_e::secondary_container);
        argb_t const on_sec_container_c = sch.get(scheme_role_e::on_secondary_container);
        argb_t const surface_c = sch.get(scheme_role_e::surface);
        argb_t const on_surface_c = sch.get(scheme_role_e::on_surface);
        argb_t const outline_c = sch.get(scheme_role_e::outline);

        // 背景
        ImU32 const bg_off = IM_COL32(surface_c.r, surface_c.g, surface_c.b, surface_c.a);
        ImU32 const bg_on = IM_COL32(sec_container_c.r, sec_container_c.g, sec_container_c.b, sec_container_c.a);
        ImU32 const bg_col = lerp_col32(bg_off, bg_on, s.select_t);
        dl->AddRectFilled(bb.Min, bb.Max, bg_col, round);

        // 描边
        ImU32 const border_col = IM_COL32(outline_c.r, outline_c.g, outline_c.b, outline_c.a);
        dl->AddRect(bb.Min, bb.Max, border_col, round, 0, 1.0f);

        // state layer
        ImU32 const state_base = IM_COL32(on_surface_c.r, on_surface_c.g, on_surface_c.b, 255);
        float const state_alpha = 0.08f * s.hover_t + 0.12f * s.press_t;
        if (state_alpha > 0.001f) {
            dl->AddRectFilled(bb.Min, bb.Max, col_with_alpha(state_base, state_alpha), round);
        }

        // ripple
        float const now = (float)ImGui::GetTime();
        s.ripple.render(dl, bb.Min, bb.Max, round, state_base, now);

        // checkmark（选中时显示）
        ImVec2 const text_size = ImGui::CalcTextSize(labels[i]);
        float const check_advance = 18.0f * s.select_t;
        if (s.select_t > 0.05f) {
            ImVec2 const check_center(bb.Min.x + 12, bb.GetCenter().y);
            float const r = 4.0f * s.select_t;
            ImU32 const check_col = IM_COL32(on_sec_container_c.r, on_sec_container_c.g,
                                             on_sec_container_c.b, (int)(on_sec_container_c.a * s.select_t));
            dl->AddLine(ImVec2(check_center.x - r, check_center.y),
                        ImVec2(check_center.x - r * 0.3f, check_center.y + r * 0.7f), check_col, 1.8f);
            dl->AddLine(ImVec2(check_center.x - r * 0.3f, check_center.y + r * 0.7f),
                        ImVec2(check_center.x + r, check_center.y - r * 0.7f), check_col, 1.8f);
        }

        // 文字
        ImU32 const text_off = IM_COL32(on_surface_c.r, on_surface_c.g, on_surface_c.b, on_surface_c.a);
        ImU32 const text_on = IM_COL32(on_sec_container_c.r, on_sec_container_c.g,
                                       on_sec_container_c.b, on_sec_container_c.a);
        ImU32 const text_col = lerp_col32(text_off, text_on, s.select_t);
        float const text_x = bb.Min.x + 12.0f + check_advance;
        float const text_y = bb.GetCenter().y - text_size.y * 0.5f;
        dl->AddText(ImVec2(text_x, text_y), text_col, labels[i]);
    }
    ImGui::Dummy(ImVec2(0, 0));
}

// ============================================================================
// MD3 Slider（带 active track + stop indicator）
// ============================================================================
static bool md3_slider(float* v, float v_min, float v_max, const char* id_str) {
    using namespace md3;
    auto const& sch = *g_scheme;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImVec2 const pos = window->DC.CursorPos;
    ImVec2 const size(ImGui::GetContentRegionAvail().x, 20.0f);
    ImRect const bb(pos, pos + size);
    ImGuiID const id = window->GetID(id_str);
    ImGui::ItemSize(size, 0);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool const hovered = ImGui::IsItemHovered();
    bool const active = ImGui::GetActiveID() == id;

    // 拖拽逻辑
    bool changed = false;
    if (active) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            ImGui::SetActiveID(id, window);
            ImGui::SetFocusID(id, window);
        }
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImGui::SetActiveID(id, window);
        ImGui::SetFocusID(id, window);
    }
    if (ImGui::GetActiveID() == id) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            float const t = std::clamp((ImGui::GetIO().MousePos.x - pos.x) / size.x, 0.0f, 1.0f);
            float const new_v = v_min + t * (v_max - v_min);
            if (std::abs(new_v - *v) > 0.001f) {
                *v = new_v;
                changed = true;
            }
        } else {
            ImGui::ClearActiveID();
        }
    }

    elem_state_t& s = get_state(id);
    s.hover_t = approach(s.hover_t, (hovered || active) ? 1.0f : 0.0f, 18.0f);

    auto* dl = window->DrawList;
    float const cy = pos.y + size.y * 0.5f;
    float const track_h = 4.0f + 2.0f * s.hover_t;
    float const progress = std::clamp((*v - v_min) / (v_max - v_min), 0.0f, 1.0f);
    float const thumb_x = pos.x + progress * size.x;

    argb_t const sec_container_c = sch.get(scheme_role_e::secondary_container);
    argb_t const primary_c = sch.get(scheme_role_e::primary);
    argb_t const on_primary_c = sch.get(scheme_role_e::on_primary);

    // inactive track
    ImU32 const inactive_col = IM_COL32(sec_container_c.r, sec_container_c.g, sec_container_c.b, sec_container_c.a);
    dl->AddRectFilled(ImVec2(pos.x, cy - track_h * 0.5f),
                      ImVec2(pos.x + size.x, cy + track_h * 0.5f),
                      inactive_col, track_h * 0.5f);

    // active track
    ImU32 const active_col = IM_COL32(primary_c.r, primary_c.g, primary_c.b, primary_c.a);
    dl->AddRectFilled(ImVec2(pos.x, cy - track_h * 0.5f),
                      ImVec2(thumb_x, cy + track_h * 0.5f),
                      active_col, track_h * 0.5f);

    // thumb（hover/active 时变大）
    float const thumb_r = 10.0f + 4.0f * s.hover_t;
    ImU32 const thumb_col = IM_COL32(primary_c.r, primary_c.g, primary_c.b, primary_c.a);
    dl->AddCircleFilled(ImVec2(thumb_x, cy), thumb_r, thumb_col, 24);

    // active 时的 state layer
    if (active) {
        ImU32 const state_col = IM_COL32(on_primary_c.r, on_primary_c.g, on_primary_c.b, 255);
        dl->AddCircleFilled(ImVec2(thumb_x, cy), thumb_r + 6.0f, col_with_alpha(state_col, 0.12f), 24);
    }

    return changed;
}

// ============================================================================
// MD3 Circular Progress Indicator（indeterminate，旋转 + 缩放）
// ============================================================================
static void md3_circular_progress(ImVec2 center, float radius, float dt) {
    using namespace md3;
    auto const& sch = *g_scheme;
    auto* dl = ImGui::GetWindowDrawList();

    static float angle = 0.0f;
    static float size_t = 0.0f;
    static bool grow = true;
    angle += dt * 2.5f;
    if (angle > 1.0f) angle -= 1.0f;

    // 弧长变化（MD3 indeterminate 规范：弧度从 270° → 90° 循环）
    if (grow) {
        size_t += dt * 1.5f;
        if (size_t >= 1.0f) { size_t = 1.0f; grow = false; }
    } else {
        size_t -= dt * 1.5f;
        if (size_t <= 0.0f) { size_t = 0.0f; grow = true; }
    }
    float const sweep_frac = 0.25f + 0.5f * size_t; // 90° → 270°

    argb_t const primary_c = sch.get(scheme_role_e::primary);
    int const segments = 48;
    float const start_ang = angle * 2.0f * 3.14159265f;
    float const sweep = sweep_frac * 2.0f * 3.14159265f;

    // 用 Path 描边画弧（带宽度）
    dl->PathClear();
    dl->PathArcTo(center, radius, start_ang, start_ang + sweep, segments);
    ImU32 const col = IM_COL32(primary_c.r, primary_c.g, primary_c.b, primary_c.a);
    dl->PathStroke(col, ImDrawFlags_None, 4.0f);
}

// ============================================================================
// MD3 Linear Progress Indicator（indeterminate，来回滑动的色块）
// ============================================================================
static void md3_linear_progress(float dt) {
    using namespace md3;
    auto const& sch = *g_scheme;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImVec2 const pos = window->DC.CursorPos;
    ImVec2 const size(ImGui::GetContentRegionAvail().x, 4.0f);
    ImGui::ItemSize(ImVec2(size.x, size.y + 8), 0);

    static float t = 0.0f;
    static bool forward = true;
    if (forward) {
        t += dt * 0.8f;
        if (t >= 1.0f) { t = 1.0f; forward = false; }
    } else {
        t -= dt * 0.8f;
        if (t <= 0.0f) { t = 0.0f; forward = true; }
    }

    auto* dl = window->DrawList;
    argb_t const sec_container_c = sch.get(scheme_role_e::surface_container_highest);
    argb_t const primary_c = sch.get(scheme_role_e::primary);

    // 轨道
    ImU32 const track_col = IM_COL32(sec_container_c.r, sec_container_c.g, sec_container_c.b, sec_container_c.a);
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), track_col, size.y * 0.5f);

    // 进度块（宽度 40%，位置随 t 移动）
    float const block_w = size.x * 0.4f;
    float const block_x = pos.x + t * (size.x - block_w);
    ImU32 const prog_col = IM_COL32(primary_c.r, primary_c.g, primary_c.b, primary_c.a);
    dl->AddRectFilled(ImVec2(block_x, pos.y), ImVec2(block_x + block_w, pos.y + size.y),
                      prog_col, size.y * 0.5f);
}

// ============================================================================
// MD3 Badge（小圆点徽章）
// ============================================================================
static void md3_badge(ImVec2 anchor, int count) {
    using namespace md3;
    auto const& sch = *g_scheme;
    auto* dl = ImGui::GetWindowDrawList();

    argb_t const error_c = sch.get(scheme_role_e::error);
    argb_t const on_error_c = sch.get(scheme_role_e::on_error);

    float const r = (count > 9) ? 12.0f : 9.0f;
    ImVec2 const center(anchor.x, anchor.y);

    // 阴影
    draw_shadow(dl, ImVec2(center.x - r, center.y - r), ImVec2(center.x + r, center.y + r), r, 2.0f);

    // 圆
    ImU32 const bg = IM_COL32(error_c.r, error_c.g, error_c.b, error_c.a);
    dl->AddCircleFilled(center, r, bg, 20);

    // 数字
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", count);
    ImVec2 const ts = ImGui::CalcTextSize(buf);
    ImU32 const tc = IM_COL32(on_error_c.r, on_error_c.g, on_error_c.b, on_error_c.a);
    dl->AddText(ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), tc, buf);
}

// ============================================================================
// MD3 Snackbar（底部弹出提示，自动消失）
// ============================================================================
struct snackbar_t {
    std::string text;
    float       life = 0.0f;
    float       max_life = 3.0f;
    bool        active = false;
};
static snackbar_t g_snackbar;

static void show_snackbar(const char* text) {
    g_snackbar.text = text;
    g_snackbar.life = g_snackbar.max_life;
    g_snackbar.active = true;
}

static void render_snackbar() {
    if (!g_snackbar.active) return;
    g_snackbar.life -= ImGui::GetIO().DeltaTime;
    if (g_snackbar.life <= 0.0f) {
        g_snackbar.active = false;
        return;
    }

    using namespace md3;
    auto const& sch = *g_scheme;
    auto* dl = ImGui::GetWindowDrawList();
    ImVec2 const display = ImGui::GetIO().DisplaySize;

    // 入场/出场动画（前 200ms 淡入，后 200ms 淡出）
    float alpha = 1.0f;
    float const in_dur = 0.2f;
    float const out_dur = 0.2f;
    float const elapsed = g_snackbar.max_life - g_snackbar.life;
    if (elapsed < in_dur) {
        alpha = ease_decel(elapsed / in_dur);
    } else if (g_snackbar.life < out_dur) {
        alpha = ease_accel(g_snackbar.life / out_dur);
    }

    ImVec2 const size(280, 48);
    ImVec2 const pos((display.x - size.x) * 0.5f, display.y - size.y - 40.0f + (1.0f - alpha) * 20.0f);

    argb_t const inverse_surface_c = sch.get(scheme_role_e::inverse_surface);
    argb_t const inverse_on_surface_c = sch.get(scheme_role_e::inverse_on_surface);

    // 阴影
    draw_shadow(dl, pos, ImVec2(pos.x + size.x, pos.y + size.y), 8.0f, 6.0f * alpha);

    // 背景
    ImU32 const bg = IM_COL32(inverse_surface_c.r, inverse_surface_c.g, inverse_surface_c.b,
                              (int)(inverse_surface_c.a * alpha));
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bg, 8.0f);

    // 文字
    ImVec2 const ts = ImGui::CalcTextSize(g_snackbar.text.c_str());
    ImU32 const tc = IM_COL32(inverse_on_surface_c.r, inverse_on_surface_c.g, inverse_on_surface_c.b,
                              (int)(inverse_on_surface_c.a * alpha));
    dl->AddText(ImVec2(pos.x + 16, pos.y + (size.y - ts.y) * 0.5f), tc, g_snackbar.text.c_str());
}

// ============================================================================
// 脉冲呼吸动画（用于状态指示器）
// ============================================================================
static void md3_pulse_dot(ImVec2 center, float base_r, float phase) {
    using namespace md3;
    auto const& sch = *g_scheme;
    auto* dl = ImGui::GetWindowDrawList();
    argb_t const primary_c = sch.get(scheme_role_e::primary);

    // 外圈呼吸（3 层错相位）
    for (int i = 0; i < 3; ++i) {
        float const p = phase + i * 0.5f;
        float const t = 0.5f + 0.5f * std::sin(p);
        float const r = base_r + t * 12.0f;
        int const a = (int)(80 * (1.0f - t));
        dl->AddCircleFilled(center, r, IM_COL32(primary_c.r, primary_c.g, primary_c.b, a), 24);
    }
    // 中心实心点
    dl->AddCircleFilled(center, base_r, IM_COL32(primary_c.r, primary_c.g, primary_c.b, 255), 24);
}

// ============================================================================
// 标签页主渲染
// ============================================================================
void render_md3_tab() {
    using namespace md3;
    auto const& sch = *g_scheme;
    float const dt = ImGui::GetIO().DeltaTime;

    // === 标题区 ===
    {
        argb_t const on_surface_c = sch.get(scheme_role_e::on_surface);
        argb_t const on_surface_variant_c = sch.get(scheme_role_e::on_surface_variant);
        ImGui::PushStyleColor(ImGuiCol_Text, on_surface_c.to_imvec());
        ImGui::TextUnformatted("Material Design 3");
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, on_surface_variant_c.to_imvec());
        ImGui::TextUnformatted("现代化组件与动画演示");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 12));
    }

    // === 按钮区（5 种 MD3 button 变体）===
    {
        argb_t const on_surface_variant_c = sch.get(scheme_role_e::on_surface_variant);
        ImGui::PushStyleColor(ImGuiCol_Text, on_surface_variant_c.to_imvec());
        ImGui::TextUnformatted("Buttons");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));

        if (md3_button("Filled", ImVec2(110, 42), button_variant_e::filled, 0)) {
            show_snackbar("Filled button clicked");
        }
        ImGui::SameLine();
        if (md3_button("Tonal", ImVec2(110, 42), button_variant_e::tonal, 1)) {
            show_snackbar("Tonal button clicked");
        }
        ImGui::SameLine();
        if (md3_button("Outlined", ImVec2(110, 42), button_variant_e::outlined, 2)) {
            show_snackbar("Outlined button clicked");
        }
        ImGui::SameLine();
        if (md3_button("Text", ImVec2(90, 42), button_variant_e::text, 3)) {
            show_snackbar("Text button clicked");
        }
        ImGui::Dummy(ImVec2(0, 16));
    }

    // === Switch 区 ===
    {
        argb_t const on_surface_variant_c = sch.get(scheme_role_e::on_surface_variant);
        ImGui::PushStyleColor(ImGuiCol_Text, on_surface_variant_c.to_imvec());
        ImGui::TextUnformatted("Switches");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));

        static bool sw1 = false, sw2 = true, sw3 = false;
        argb_t const on_surface_c = sch.get(scheme_role_e::on_surface);
        ImGui::PushStyleColor(ImGuiCol_Text, on_surface_c.to_imvec());
        ImGui::TextUnformatted("Wi-Fi");
        ImGui::PopStyleColor();
        ImGui::SameLine(140);
        md3_switch(&sw1, "##sw1");
        ImGui::SameLine(220);
        ImGui::PushStyleColor(ImGuiCol_Text, on_surface_c.to_imvec());
        ImGui::TextUnformatted("Bluetooth");
        ImGui::PopStyleColor();
        ImGui::SameLine(340);
        md3_switch(&sw2, "##sw2");

        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PushStyleColor(ImGuiCol_Text, on_surface_c.to_imvec());
        ImGui::TextUnformatted("Hotspot");
        ImGui::PopStyleColor();
        ImGui::SameLine(140);
        md3_switch(&sw3, "##sw3");
        ImGui::Dummy(ImVec2(0, 16));
    }

    // === Chips 区 ===
    {
        argb_t const on_surface_variant_c = sch.get(scheme_role_e::on_surface_variant);
        ImGui::PushStyleColor(ImGuiCol_Text, on_surface_variant_c.to_imvec());
        ImGui::TextUnformatted("Filter Chips");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));

        static int selected_chip = 0;
        const char* chips[] = { "All", "Music", "Video", "Photo", "Doc" };
        for (int i = 0; i < 5; ++i) {
            if (i > 0) ImGui::SameLine(0, 6);
            if (md3_chip(chips[i], selected_chip == i, i)) {
                selected_chip = i;
            }
        }
        ImGui::Dummy(ImVec2(0, 16));
    }

    // === Segmented Button ===
    {
        argb_t const on_surface_variant_c = sch.get(scheme_role_e::on_surface_variant);
        ImGui::PushStyleColor(ImGuiCol_Text, on_surface_variant_c.to_imvec());
        ImGui::TextUnformatted("Segmented Button");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));

        static int seg_sel = 0;
        const char* seg_labels[] = { "Day", "Week", "Month", "Year" };
        md3_segmented(&seg_sel, seg_labels, 4);
        ImGui::Dummy(ImVec2(0, 16));
    }

    // === Slider ===
    {
        argb_t const on_surface_variant_c = sch.get(scheme_role_e::on_surface_variant);
        ImGui::PushStyleColor(ImGuiCol_Text, on_surface_variant_c.to_imvec());
        ImGui::TextUnformatted("Slider");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));

        static float slider_v = 0.4f;
        md3_slider(&slider_v, 0.0f, 1.0f, "##slider1");
        ImGui::Dummy(ImVec2(0, 16));
    }

    // === Cards ===
    {
        argb_t const on_surface_variant_c = sch.get(scheme_role_e::on_surface_variant);
        ImGui::PushStyleColor(ImGuiCol_Text, on_surface_variant_c.to_imvec());
        ImGui::TextUnformatted("Cards");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));

        md3_card("card1", 70, "Elevated Card", "Hover to see elevation change");
        ImGui::Dummy(ImVec2(0, 8));
        md3_card("card2", 70, "Filled Card", "MD3 styled surface container");
        ImGui::Dummy(ImVec2(0, 16));
    }

    // === Progress Indicators ===
    {
        argb_t const on_surface_variant_c = sch.get(scheme_role_e::on_surface_variant);
        ImGui::PushStyleColor(ImGuiCol_Text, on_surface_variant_c.to_imvec());
        ImGui::TextUnformatted("Progress Indicators");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));

        // 圆形进度
        ImVec2 const cursor = ImGui::GetCursorScreenPos();
        md3_circular_progress(ImVec2(cursor.x + 20, cursor.y + 20), 16.0f, dt);
        ImGui::Dummy(ImVec2(40, 40));
        ImGui::SameLine();
        argb_t const on_surface_c = sch.get(scheme_role_e::on_surface);
        ImGui::PushStyleColor(ImGuiCol_Text, on_surface_c.to_imvec());
        ImGui::TextUnformatted("Circular\n(indeterminate)");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 8));

        // 线性进度
        md3_linear_progress(dt);
        ImGui::Dummy(ImVec2(0, 8));
        argb_t const on_surface_variant_c2 = sch.get(scheme_role_e::on_surface_variant);
        ImGui::PushStyleColor(ImGuiCol_Text, on_surface_variant_c2.to_imvec());
        ImGui::TextUnformatted("Linear (indeterminate)");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 16));
    }

    // === FAB + Badge ===
    {
        argb_t const on_surface_variant_c = sch.get(scheme_role_e::on_surface_variant);
        ImGui::PushStyleColor(ImGuiCol_Text, on_surface_variant_c.to_imvec());
        ImGui::TextUnformatted("FAB & Badge");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));

        ImVec2 const cursor = ImGui::GetCursorScreenPos();
        if (md3_fab("+", cursor, 56.0f)) {
            show_snackbar("FAB clicked");
        }
        // 在 FAB 右上角放 badge
        md3_badge(ImVec2(cursor.x + 48, cursor.y + 8), 5);
        ImGui::Dummy(ImVec2(70, 60));
        ImGui::SameLine();
        argb_t const on_surface_c = sch.get(scheme_role_e::on_surface);
        ImGui::PushStyleColor(ImGuiCol_Text, on_surface_c.to_imvec());
        ImGui::TextUnformatted("Floating Action Button\nwith notification badge");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 16));
    }

    // === Pulse Animation ===
    {
        argb_t const on_surface_variant_c = sch.get(scheme_role_e::on_surface_variant);
        ImGui::PushStyleColor(ImGuiCol_Text, on_surface_variant_c.to_imvec());
        ImGui::TextUnformatted("Pulse Indicator");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));

        static float phase = 0.0f;
        phase += dt * 2.5f;
        ImVec2 const cursor = ImGui::GetCursorScreenPos();
        md3_pulse_dot(ImVec2(cursor.x + 20, cursor.y + 20), 6.0f, phase);
        ImGui::Dummy(ImVec2(40, 40));
        ImGui::SameLine();
        argb_t const on_surface_c = sch.get(scheme_role_e::on_surface);
        ImGui::PushStyleColor(ImGuiCol_Text, on_surface_c.to_imvec());
        ImGui::TextUnformatted("Breathing animation\nfor status indicators");
        ImGui::PopStyleColor();
    }

    // === 渲染 Snackbar（覆盖层）===
    render_snackbar();
}

} // namespace md3_ui
