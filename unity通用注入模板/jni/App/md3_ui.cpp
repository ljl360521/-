//
// MD3 UI 渲染模块实现
// 展示 Material Design 3 风格的动画和组件
//

// 启用 ImGui 数学运算符重载（必须在所有 imgui.h include 之前定义）
#define IMGUI_DEFINE_MATH_OPERATORS

#include "md3_ui.hpp"
#include "imgui_internal.h"

#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

namespace md3_ui {

// 全局配色方案
static md3::palette_bundle_t* g_palette = nullptr;
static md3::scheme* g_scheme = nullptr;

// 动画状态
struct anim_state_t {
    float ripple[4] = { 0, 0, 0, 0 };      // 4 个按钮的波纹进度
    bool ripple_active[4] = { false, false, false, false };
    float card_hover = 0.0f;                // 卡片悬浮动画
    float loading_angle = 0.0f;             // 加载旋转角度
    float progress = 0.0f;                  // 进度条
    bool progress_dir = true;               // 进度方向
    float switch_anim = 0.0f;               // 开关动画
    bool switch_on = false;                 // 开关状态
    int selected_chip = 0;                  // 选中的芯片
    float chip_anim[5] = { 0, 0, 0, 0, 0 }; // 芯片选中动画
    float pulse = 0.0f;                     // 脉冲动画
};

static anim_state_t g_anim;

void init() {
    // 用 Material Blue (#1976D2) 作为种子色生成 MD3 配色
    md3::argb_t seed{ 0xff, 0x40, 0x79, 0xff };
    g_palette = new md3::palette_bundle_t(md3::palette_bundle_t::from_source(seed));
    g_scheme = new md3::scheme(g_palette->dark);

    // 应用 MD3 样式到 ImGui
    md3::imgui_style::apply(*g_scheme, &ImGui::GetStyle());

    // 额外样式调整
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScrollbarRounding = 3;
    style.ScrollbarSize = 6;
    style.CircleTessellationMaxError = 0.01f;
    style.WindowRounding = 16;
    style.ChildRounding = 16;
    style.FrameRounding = 12;
    style.GrabRounding = 12;
    style.PopupRounding = 12;
}

md3::scheme const& get_scheme() {
    return *g_scheme;
}

// 线性插值（带速度）
static float lerp_anim(float current, float target, float speed) {
    float const t = std::clamp(speed * ImGui::GetIO().DeltaTime, 0.0f, 1.0f);
    return current + (target - current) * t;
}

// MD3 风格按钮（带波纹动画）
static bool md3_button(const char* label, ImVec2 size, int idx) {
    using namespace md3;

    auto const& sch = *g_scheme;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiID const id = window->GetID(label);
    ImVec2 const pos = window->DC.CursorPos;
    ImRect const bb(pos, pos + size);
    ImGui::ItemSize(size, ImGui::GetStyle().FramePadding.y);
    if (!ImGui::ItemAdd(bb, id))
        return false;

    bool hovered = false, held = false;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

    // 波纹动画
    if (pressed) {
        g_anim.ripple_active[idx] = true;
        g_anim.ripple[idx] = 0.0f;
    }
    if (g_anim.ripple_active[idx]) {
        g_anim.ripple[idx] = lerp_anim(g_anim.ripple[idx], 1.0f, 8.0f);
        if (g_anim.ripple[idx] > 0.95f) {
            g_anim.ripple_active[idx] = false;
            g_anim.ripple[idx] = 0.0f;
        }
    }

    auto* dl = window->DrawList;
    argb_t const primary_c = sch.get(scheme_role_e::primary);
    argb_t const on_primary_c = sch.get(scheme_role_e::on_primary);

    // 按钮背景（带悬浮/按下状态混合）
    argb_t bg = primary_c;
    if (hovered) bg = imgui_style::mix(primary_c, on_primary_c, 0.08f);
    if (held) bg = imgui_style::mix(primary_c, on_primary_c, 0.12f);

    dl->AddRectFilled(bb.Min, bb.Max, IM_COL32(bg.r, bg.g, bg.b, bg.a), 12.0f);

    // 波纹效果
    if (g_anim.ripple_active[idx] && g_anim.ripple[idx] > 0.01f) {
        float const len = std::sqrt(size.x * size.x + size.y * size.y);
        float const r = g_anim.ripple[idx] * len * 0.7f;
        ImVec2 const center = bb.GetCenter();
        ImU32 const ripple_col = IM_COL32(on_primary_c.r, on_primary_c.g, on_primary_c.b, (int)(80 * (1.0f - g_anim.ripple[idx])));
        dl->AddCircleFilled(center, r, ripple_col, 24);
    }

    // 文字
    ImVec2 const text_size = ImGui::CalcTextSize(label);
    ImVec2 const text_pos = bb.GetCenter() - text_size * 0.5f;
    argb_t const text_c = on_primary_c;
    dl->AddText(text_pos, IM_COL32(text_c.r, text_c.g, text_c.b, text_c.a), label);

    return pressed;
}

// MD3 风格卡片
static void md3_card(const char* id_str, float height, const char* content) {
    using namespace md3;

    auto const& sch = *g_scheme;
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    ImVec2 const size = ImGui::GetContentRegionAvail();
    ImVec2 const pos = window->DC.CursorPos;
    ImRect const bb(pos, pos + ImVec2(size.x, height));

    ImGuiID const id = window->GetID(id_str);
    ImGui::ItemSize(ImVec2(size.x, height), 0);
    if (!ImGui::ItemAdd(bb, id))
        return;

    bool const hovered = ImGui::IsMouseHoveringRect(bb.Min, bb.Max);
    float const target = hovered ? 1.0f : 0.0f;
    g_anim.card_hover = lerp_anim(g_anim.card_hover, target, 10.0f);

    auto* dl = window->DrawList;
    argb_t const container_c = sch.get(scheme_role_e::surface_container);
    argb_t const container_high_c = sch.get(scheme_role_e::surface_container_high);
    argb_t const mixed = imgui_style::mix(container_c, container_high_c, g_anim.card_hover);

    // 卡片背景
    dl->AddRectFilled(bb.Min, bb.Max, IM_COL32(mixed.r, mixed.g, mixed.b, mixed.a), 16.0f);

    // 边框
    argb_t const outline_c = sch.get(scheme_role_e::outline_variant);
    ImU32 const border_col = IM_COL32(outline_c.r, outline_c.g, outline_c.b, (int)(outline_c.a * 0.5f));
    dl->AddRect(bb.Min, bb.Max, border_col, 16.0f, 0, 1.0f);

    // 内容文字
    argb_t const on_surface_c = sch.get(scheme_role_e::on_surface);
    ImVec2 const text_pos = bb.Min + ImVec2(16, 16);
    dl->AddText(text_pos, IM_COL32(on_surface_c.r, on_surface_c.g, on_surface_c.b, on_surface_c.a), content);

    // 装饰条
    argb_t const primary_c = sch.get(scheme_role_e::primary);
    dl->AddRectFilled(ImVec2(bb.Min.x, bb.Min.y), ImVec2(bb.Min.x + 4, bb.Max.y),
                      IM_COL32(primary_c.r, primary_c.g, primary_c.b, primary_c.a), 16.0f);

    ImGui::Dummy(ImVec2(0, 8));
}

// MD3 风格开关（带动画）
static void md3_switch(bool* v) {
    using namespace md3;

    auto const& sch = *g_scheme;
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    ImVec2 const track_size(52, 32);
    ImVec2 const pos = window->DC.CursorPos;
    ImRect const bb(pos, pos + track_size);

    ImGuiID const id = window->GetID("##md3_switch");
    ImGui::ItemSize(track_size, ImGui::GetStyle().FramePadding.y);
    if (!ImGui::ItemAdd(bb, id))
        return;

    bool hovered = false, held = false;
    bool const pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    if (pressed)
        *v = !*v;

    // 动画
    g_anim.switch_on = *v;
    float const target = *v ? 1.0f : 0.0f;
    g_anim.switch_anim = lerp_anim(g_anim.switch_anim, target, 15.0f);

    auto* dl = window->DrawList;
    float const t = g_anim.switch_anim;

    // 轨道
    argb_t const outline_c = sch.get(scheme_role_e::outline_variant);
    argb_t const primary_c = sch.get(scheme_role_e::primary);
    argb_t const on_surface_c = sch.get(scheme_role_e::on_surface);
    argb_t const surface_c = sch.get(scheme_role_e::surface);

    // 混合轨道颜色
    ImU32 track_col_off = IM_COL32(outline_c.r, outline_c.g, outline_c.b, outline_c.a);
    ImU32 track_col_on = IM_COL32(primary_c.r, primary_c.g, primary_c.b, primary_c.a);
    ImVec4 const off_v = ImGui::ColorConvertU32ToFloat4(track_col_off);
    ImVec4 const on_v = ImGui::ColorConvertU32ToFloat4(track_col_on);
    ImVec4 const track_v(
        off_v.x + (on_v.x - off_v.x) * t,
        off_v.y + (on_v.y - off_v.y) * t,
        off_v.z + (on_v.z - off_v.z) * t,
        off_v.w + (on_v.w - off_v.w) * t
    );
    ImU32 const track_col = ImGui::ColorConvertFloat4ToU32(track_v);

    float const track_h = 16 + 16 * t;
    ImVec2 const track_min(pos.x, pos.y + (track_size.y - track_h) * 0.5f);
    ImVec2 const track_max(pos.x + track_size.x, pos.y + (track_size.y + track_h) * 0.5f);
    dl->AddRectFilled(track_min, track_max, track_col, track_h * 0.5f, ImDrawFlags_RoundCornersAll);

    // 滑块（带状态边框）
    float const thumb_r = 8.0f + 8.0f * t;
    float const thumb_x = pos.x + 16 + (track_size.x - 32) * t;
    float const thumb_y = pos.y + track_size.y * 0.5f;

    if (t < 0.5f) {
        // 关闭状态：带边框的小圆
        ImU32 const thumb_col = IM_COL32(surface_c.r, surface_c.g, surface_c.b, surface_c.a);
        dl->AddCircleFilled(ImVec2(thumb_x, thumb_y), thumb_r, thumb_col, 16);
        dl->AddCircle(ImVec2(thumb_x, thumb_y), thumb_r, track_col_off, 16, 2.0f);
    } else {
        // 开启状态：实心大圆
        argb_t const on_primary_c = sch.get(scheme_role_e::on_primary);
        ImU32 const thumb_col = IM_COL32(on_primary_c.r, on_primary_c.g, on_primary_c.b, on_primary_c.a);
        dl->AddCircleFilled(ImVec2(thumb_x, thumb_y), thumb_r, thumb_col, 16);
    }
}

// MD3 风格芯片（Filter Chip）
static bool md3_chip(const char* label, bool selected, int idx) {
    using namespace md3;

    auto const& sch = *g_scheme;
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    ImVec2 const text_size = ImGui::CalcTextSize(label);
    ImVec2 const size(text_size.x + 24, 32);
    ImVec2 const pos = window->DC.CursorPos;
    ImRect const bb(pos, pos + size);

    ImGuiID const id = window->GetID(label);
    ImGui::ItemSize(size, ImGui::GetStyle().FramePadding.y);
    if (!ImGui::ItemAdd(bb, id))
        return false;

    bool hovered = false, held = false;
    bool const pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

    // 选中动画
    float const target = selected ? 1.0f : 0.0f;
    g_anim.chip_anim[idx] = lerp_anim(g_anim.chip_anim[idx], target, 12.0f);

    auto* dl = window->DrawList;
    float const t = g_anim.chip_anim[idx];

    argb_t const sec_container_c = sch.get(scheme_role_e::secondary_container);
    argb_t const surface_c = sch.get(scheme_role_e::surface);
    argb_t const on_sec_container_c = sch.get(scheme_role_e::on_secondary_container);
    argb_t const on_surface_variant_c = sch.get(scheme_role_e::on_surface_variant);

    // 混合背景
    ImVec4 const bg_off = surface_c.to_imvec();
    ImVec4 const bg_on = sec_container_c.to_imvec();
    ImVec4 bg_v(
        bg_off.x + (bg_on.x - bg_off.x) * t,
        bg_off.y + (bg_on.y - bg_off.y) * t,
        bg_off.z + (bg_on.z - bg_off.z) * t,
        bg_off.w + (bg_on.w - bg_off.w) * t
    );

    if (hovered) {
        bg_v.x += 0.05f; bg_v.y += 0.05f; bg_v.z += 0.05f;
    }

    ImU32 const bg_col = ImGui::ColorConvertFloat4ToU32(bg_v);
    dl->AddRectFilled(bb.Min, bb.Max, bg_col, 8.0f);

    // 边框（未选中时）
    if (t < 0.9f) {
        argb_t const outline_c = sch.get(scheme_role_e::outline_variant);
        ImU32 const border_col = IM_COL32(outline_c.r, outline_c.g, outline_c.b, (int)(outline_c.a * (1.0f - t)));
        dl->AddRect(bb.Min, bb.Max, border_col, 8.0f, 0, 1.0f);
    }

    // 文字
    ImVec4 const text_off = on_surface_variant_c.to_imvec();
    ImVec4 const text_on = on_sec_container_c.to_imvec();
    ImVec4 const text_v(
        text_off.x + (text_on.x - text_off.x) * t,
        text_off.y + (text_on.y - text_off.y) * t,
        text_off.z + (text_on.z - text_off.z) * t,
        text_off.w + (text_on.w - text_off.w) * t
    );
    ImU32 const text_col = ImGui::ColorConvertFloat4ToU32(text_v);
    dl->AddText(ImVec2(pos.x + 12, pos.y + (size.y - text_size.y) * 0.5f), text_col, label);

    return pressed;
}

// MD3 加载动画（旋转圆环）
static void md3_loading_spinner(ImVec2 center, float radius) {
    using namespace md3;

    auto const& sch = *g_scheme;
    auto* dl = ImGui::GetWindowDrawList();

    g_anim.loading_angle += ImGui::GetIO().DeltaTime * 3.0f;
    if (g_anim.loading_angle > 1.0f)
        g_anim.loading_angle -= 1.0f;

    argb_t const primary_c = sch.get(scheme_role_e::primary);

    // 画 270 度的弧线，留 90 度缺口
    int const segments = 48;
    float const start_angle = g_anim.loading_angle * 2.0f * 3.14159265f;
    float const sweep = 1.5f * 3.14159265f; // 270 度

    for (int i = 0; i < segments; ++i) {
        float const t1 = (float)i / segments;
        float const t2 = (float)(i + 1) / segments;
        float const a1 = start_angle + sweep * t1;
        float const a2 = start_angle + sweep * t2;
        // 渐变透明度
        float const alpha = t1;
        ImVec2 const p1(center.x + std::cos(a1) * radius, center.y + std::sin(a1) * radius);
        ImVec2 const p2(center.x + std::cos(a2) * radius, center.y + std::sin(a2) * radius);
        ImU32 const col = IM_COL32(primary_c.r, primary_c.g, primary_c.b, (int)(255 * alpha));
        dl->AddLine(p1, p2, col, 4.0f);
    }
}

// MD3 进度条（带动画）
static void md3_progress_bar() {
    using namespace md3;

    auto const& sch = *g_scheme;
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    ImVec2 const size = ImGui::GetContentRegionAvail();
    ImVec2 const pos = window->DC.CursorPos;
    float const height = 4.0f;

    ImGui::ItemSize(ImVec2(size.x, height + 8), 0);

    auto* dl = window->DrawList;

    // 更新进度
    if (g_anim.progress_dir) {
        g_anim.progress += ImGui::GetIO().DeltaTime * 0.3f;
        if (g_anim.progress >= 1.0f) {
            g_anim.progress = 1.0f;
            g_anim.progress_dir = false;
        }
    } else {
        g_anim.progress -= ImGui::GetIO().DeltaTime * 0.3f;
        if (g_anim.progress <= 0.0f) {
            g_anim.progress = 0.0f;
            g_anim.progress_dir = true;
        }
    }

    // 轨道
    argb_t const surface_c = sch.get(scheme_role_e::surface_container_highest);
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + height),
                      IM_COL32(surface_c.r, surface_c.g, surface_c.b, surface_c.a), height * 0.5f);

    // 进度
    argb_t const primary_c = sch.get(scheme_role_e::primary);
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x * g_anim.progress, pos.y + height),
                      IM_COL32(primary_c.r, primary_c.g, primary_c.b, primary_c.a), height * 0.5f);
}

void render_md3_tab() {
    using namespace md3;

    auto const& sch = *g_scheme;

    // 标题
    {
        argb_t const on_surface_c = sch.get(scheme_role_e::on_surface);
        ImGui::PushStyleColor(ImGuiCol_Text, on_surface_c.to_imvec());
        ImGui::TextUnformatted("Material Design 3 动画演示");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 8));
    }

    // === 按钮区域 ===
    {
        argb_t const on_surface_variant_c = sch.get(scheme_role_e::on_surface_variant);
        ImGui::PushStyleColor(ImGuiCol_Text, on_surface_variant_c.to_imvec());
        ImGui::TextUnformatted("按钮 (带波纹动画)");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));

        // 4 个 MD3 按钮
        if (md3_button("Filled", ImVec2(110, 40), 0)) {}
        ImGui::SameLine();
        if (md3_button("Tonale", ImVec2(110, 40), 1)) {}
        ImGui::SameLine();
        if (md3_button("Outlined", ImVec2(110, 40), 2)) {}
        ImGui::SameLine();
        if (md3_button("Text", ImVec2(110, 40), 3)) {}
        ImGui::Dummy(ImVec2(0, 12));
    }

    // === 开关 ===
    {
        argb_t const on_surface_variant_c = sch.get(scheme_role_e::on_surface_variant);
        ImGui::PushStyleColor(ImGuiCol_Text, on_surface_variant_c.to_imvec());
        ImGui::TextUnformatted("开关 (带滑动动画)");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));

        static bool sw1 = false, sw2 = true;
        ImGui::TextUnformatted("Wi-Fi");
        ImGui::SameLine(120);
        md3_switch(&sw1);
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::TextUnformatted("蓝牙");
        ImGui::SameLine(120);
        md3_switch(&sw2);
        ImGui::Dummy(ImVec2(0, 12));
    }

    // === 芯片 ===
    {
        argb_t const on_surface_variant_c = sch.get(scheme_role_e::on_surface_variant);
        ImGui::PushStyleColor(ImGuiCol_Text, on_surface_variant_c.to_imvec());
        ImGui::TextUnformatted("筛选芯片 (带选中动画)");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));

        const char* chips[] = { "全部", "音乐", "视频", "图片", "文档" };
        for (int i = 0; i < 5; ++i) {
            if (i > 0) ImGui::SameLine(8, 8);
            if (md3_chip(chips[i], g_anim.selected_chip == i, i)) {
                g_anim.selected_chip = i;
            }
        }
        ImGui::Dummy(ImVec2(0, 12));
    }

    // === 卡片 ===
    {
        argb_t const on_surface_variant_c = sch.get(scheme_role_e::on_surface_variant);
        ImGui::PushStyleColor(ImGuiCol_Text, on_surface_variant_c.to_imvec());
        ImGui::TextUnformatted("卡片 (带悬浮动画)");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));

        md3_card("card1", 70, "Elevated Card - 悬浮查看背景变化");
        md3_card("card2", 70, "Filled Card - MD3 风格卡片示例");
        ImGui::Dummy(ImVec2(0, 8));
    }

    // === 加载动画 ===
    {
        argb_t const on_surface_variant_c = sch.get(scheme_role_e::on_surface_variant);
        ImGui::PushStyleColor(ImGuiCol_Text, on_surface_variant_c.to_imvec());
        ImGui::TextUnformatted("加载动画 (Circular Progress)");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));

        ImVec2 const cursor = ImGui::GetCursorScreenPos();
        ImVec2 const spinner_pos(cursor.x + 30, cursor.y + 30);
        md3_loading_spinner(spinner_pos, 24.0f);
        ImGui::Dummy(ImVec2(60, 60));
        ImGui::SameLine();

        argb_t const on_surface_c = sch.get(scheme_role_e::on_surface);
        ImGui::PushStyleColor(ImGuiCol_Text, on_surface_c.to_imvec());
        ImGui::TextUnformatted("加载中...\n旋转动画演示");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 12));
    }

    // === 进度条 ===
    {
        argb_t const on_surface_variant_c = sch.get(scheme_role_e::on_surface_variant);
        ImGui::PushStyleColor(ImGuiCol_Text, on_surface_variant_c.to_imvec());
        ImGui::TextUnformatted("线性进度条 (来回动画)");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));
        md3_progress_bar();
        ImGui::Dummy(ImVec2(0, 12));
    }

    // === 脉冲动画 ===
    {
        argb_t const on_surface_variant_c = sch.get(scheme_role_e::on_surface_variant);
        ImGui::PushStyleColor(ImGuiCol_Text, on_surface_variant_c.to_imvec());
        ImGui::TextUnformatted("脉冲动画 (Pulse)");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));

        g_anim.pulse += ImGui::GetIO().DeltaTime * 2.0f;
        float const pulse_scale = 0.5f + 0.5f * std::sin(g_anim.pulse);

        ImVec2 const cursor = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        argb_t const primary_c = sch.get(scheme_role_e::primary);

        // 三个脉冲圆
        for (int i = 0; i < 3; ++i) {
            float const phase = g_anim.pulse + i * 0.5f;
            float const scale = 0.5f + 0.5f * std::sin(phase);
            float const r = 10 + scale * 15;
            int const alpha = (int)(255 * (1.0f - scale));
            ImVec2 const center(cursor.x + 30 + i * 70, cursor.y + 30);
            dl->AddCircleFilled(center, r, IM_COL32(primary_c.r, primary_c.g, primary_c.b, alpha), 24);
            dl->AddCircleFilled(center, 10, IM_COL32(primary_c.r, primary_c.g, primary_c.b, 255), 24);
        }
        ImGui::Dummy(ImVec2(0, 60));
    }
}

} // namespace md3_ui
