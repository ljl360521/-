//
// Aura UI 模块实现
// 移植自 AuraNexus 项目的 Mac 风格毛玻璃 UI
// 完美还原原项目的视觉风格、动画、布局
//

#define IMGUI_DEFINE_MATH_OPERATORS
#include "aura_ui.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <unordered_map>

namespace aura_ui {

// ============================================================================
// Mac 风格配色常量（原项目核心配色）
// ============================================================================
namespace mac {
    constexpr ImU32 BLUE        = IM_COL32(0, 122, 255, 255);   // Mac 蓝色
    constexpr ImU32 BLUE_LIGHT  = IM_COL32(22, 119, 255, 255);
    constexpr ImU32 GREEN       = IM_COL32(52, 199, 89, 255);   // Mac 绿色
    constexpr ImU32 ORANGE      = IM_COL32(255, 159, 10, 255);  // Mac 橙色
    constexpr ImU32 PURPLE      = IM_COL32(191, 90, 242, 255);  // Mac 紫色
    constexpr ImU32 RED         = IM_COL32(255, 69, 58, 255);   // Mac 红色

    constexpr ImU32 TEXT_LIGHT  = IM_COL32(235, 235, 240, 255); // 主文字
    constexpr ImU32 TEXT_DIM    = IM_COL32(200, 200, 205, 255); // 次文字
    constexpr ImU32 TEXT_GRAY   = IM_COL32(128, 128, 128, 255); // 灰色副文本
    constexpr ImU32 TEXT_DARK   = IM_COL32(89, 89, 89, 255);    // 未选中文字

    // 毛玻璃背景层次
    constexpr ImU32 GLASS_BG    = IM_COL32(255, 255, 255, 128); // 半透明白色 (1,1,1,0.5)
    constexpr ImU32 CARD_BG     = IM_COL32(20, 20, 24, 160);    // 深色卡片
    constexpr ImU32 CARD_HOVER  = IM_COL32(44, 44, 48, 200);
    constexpr ImU32 CARD_ACTIVE = IM_COL32(28, 28, 32, 220);
    constexpr ImU32 CARD_SEL    = IM_COL32(0, 122, 255, 30);    // 选中态

    constexpr float ROUND_LARGE = 40.0f;
    constexpr float ROUND_MED   = 10.0f;
}

// ============================================================================
// 初始化 - 应用 Mac 风格样式
// ============================================================================
void init() {
    ImGuiStyle& style = ImGui::GetStyle();
    // Mac 风格圆角
    style.WindowRounding    = mac::ROUND_LARGE;
    style.ChildRounding     = mac::ROUND_LARGE;
    style.FrameRounding     = mac::ROUND_MED;
    style.GrabRounding      = mac::ROUND_MED;
    style.PopupRounding     = mac::ROUND_MED;
    style.ScrollbarRounding = mac::ROUND_MED;
    // 无边框
    style.WindowBorderSize  = 0;
    style.ChildBorderSize   = 0;
    style.FrameBorderSize   = 0;
    // 间距
    style.WindowPadding     = ImVec2(10, 10);
    style.FramePadding      = ImVec2(10, 8);
    style.ItemSpacing       = ImVec2(8, 8);

    // 深色毛玻璃配色
    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg]        = ImVec4(0.04f, 0.04f, 0.06f, 0.85f);
    c[ImGuiCol_ChildBg]         = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_PopupBg]         = ImVec4(0.08f, 0.08f, 0.10f, 0.95f);
    c[ImGuiCol_Border]          = ImVec4(1.0f, 1.0f, 1.0f, 0.0f);
    c[ImGuiCol_Text]            = ImVec4(0.92f, 0.92f, 0.94f, 1.0f);
    c[ImGuiCol_TextDisabled]    = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    c[ImGuiCol_FrameBg]         = ImVec4(0.10f, 0.10f, 0.12f, 0.8f);
    c[ImGuiCol_FrameBgHovered]  = ImVec4(0.15f, 0.15f, 0.18f, 0.9f);
    c[ImGuiCol_FrameBgActive]   = ImVec4(0.20f, 0.20f, 0.24f, 1.0f);
    c[ImGuiCol_Button]          = ImVec4(0.12f, 0.12f, 0.15f, 0.8f);
    c[ImGuiCol_ButtonHovered]   = ImVec4(0.0f, 0.48f, 1.0f, 0.8f);
    c[ImGuiCol_ButtonActive]    = ImVec4(0.0f, 0.40f, 0.85f, 1.0f);
    c[ImGuiCol_Header]          = ImVec4(0.0f, 0.48f, 1.0f, 0.4f);
    c[ImGuiCol_HeaderHovered]   = ImVec4(0.0f, 0.48f, 1.0f, 0.6f);
    c[ImGuiCol_HeaderActive]    = ImVec4(0.0f, 0.48f, 1.0f, 0.8f);
    c[ImGuiCol_CheckMark]       = ImVec4(0.0f, 0.48f, 1.0f, 1.0f);
    c[ImGuiCol_SliderGrab]      = ImVec4(0.0f, 0.48f, 1.0f, 1.0f);
    c[ImGuiCol_SliderGrabActive]= ImVec4(0.0f, 0.60f, 1.0f, 1.0f);
    c[ImGuiCol_Separator]       = ImVec4(1.0f, 1.0f, 1.0f, 0.1f);
    c[ImGuiCol_Tab]             = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_TabHovered]      = ImVec4(0.0f, 0.48f, 1.0f, 0.3f);
    c[ImGuiCol_TabActive]       = ImVec4(0.0f, 0.48f, 1.0f, 0.5f);
    c[ImGuiCol_ScrollbarBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_ScrollbarGrab]   = ImVec4(1.0f, 1.0f, 1.0f, 0.2f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.3f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.0f, 0.48f, 1.0f, 0.6f);
}

// ============================================================================
// DrawAuraSectionTitle - 带左侧蓝色竖条的标题
// ============================================================================
void DrawAuraSectionTitle(const char* title) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

    float title_height = ImGui::CalcTextSize(title).y;
    float line_width = 4.0f;
    float spacing = 8.0f;

    ImVec2 cursor_pos = window->DC.CursorPos;

    // 左侧竖条 - Mac 蓝色
    ImVec2 line_p1(cursor_pos.x, cursor_pos.y);
    ImVec2 line_p2(cursor_pos.x + line_width, cursor_pos.y + title_height);
    window->DrawList->AddRectFilled(line_p1, line_p2, mac::BLUE, 2.0f);

    // 标题文字 - Mac 风格白色
    ImVec2 text_pos(cursor_pos.x + line_width + spacing, cursor_pos.y);
    window->DrawList->AddText(text_pos, mac::TEXT_LIGHT, title);

    ImGui::ItemSize(ImVec2(0, title_height + style.ItemSpacing.y), 0.0f);
}

// ============================================================================
// BeginGlassCard / EndGlassCard - 玻璃卡片容器
// ============================================================================
bool BeginGlassCard(const char* label, const ImVec2& size, bool border, ImGuiWindowFlags flags) {
    ImGuiStyle& style = ImGui::GetStyle();
    // 保存当前样式
    ImVec4 saved_child_bg    = style.Colors[ImGuiCol_ChildBg];
    ImVec4 saved_border      = style.Colors[ImGuiCol_Border];
    float  saved_rounding    = style.ChildRounding;
    float  saved_border_size = style.ChildBorderSize;
    ImVec2 saved_padding     = style.WindowPadding;

    // 应用玻璃卡片样式：半透明白色 + 圆角 40
    style.Colors[ImGuiCol_ChildBg]   = ImVec4(1.0f, 1.0f, 1.0f, 0.08f);
    style.Colors[ImGuiCol_Border]    = ImVec4(1.0f, 1.0f, 1.0f, 0.12f);
    style.ChildRounding              = mac::ROUND_LARGE;
    style.ChildBorderSize            = 1.5f;
    style.WindowPadding              = ImVec2(16, 16);

    bool ret = ImGui::BeginChild(label, size, border, flags);

    // 恢复样式
    style.Colors[ImGuiCol_ChildBg]   = saved_child_bg;
    style.Colors[ImGuiCol_Border]    = saved_border;
    style.ChildRounding              = saved_rounding;
    style.ChildBorderSize            = saved_border_size;
    style.WindowPadding              = saved_padding;
    return ret;
}

void EndGlassCard() {
    ImGui::EndChild();
}

// ============================================================================
// ButtonWithIcon - 带图标和副标题的按钮
// 原项目用 LOGO 纹理，这里用文字图标代替
// ============================================================================
bool ButtonWithIcon(const char* label, const char* subtitle, const ImVec2& size_arg) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    const ImVec2 subtitle_size = ImGui::CalcTextSize(subtitle, NULL, true);

    ImVec2 pos = window->DC.CursorPos;

    // 计算所需尺寸（图标用文字圆圈代替，70x70）
    float icon_size = 60.0f;
    float left_margin = 12.0f;
    float icon_text_spacing = 14.0f;
    float text_width = ImMax(label_size.x, subtitle_size.x);
    float text_height = label_size.y + subtitle_size.y + 4.0f;

    ImVec2 size = ImGui::CalcItemSize(size_arg,
        left_margin + icon_size + icon_text_spacing + text_width + style.FramePadding.x * 2.0f,
        ImMax(icon_size, text_height) + style.FramePadding.y * 2.0f);

    const ImRect bb(pos, pos + size);
    ImGui::ItemSize(size, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, 0);

    ImDrawList* draw_list = window->DrawList;

    // 动画：hover 上浮 + 缩放
    float* anim_y     = ImGui::GetStateStorage()->GetFloatRef(id + 0x1000, 0.0f);
    float* anim_scale = ImGui::GetStateStorage()->GetFloatRef(id + 0x2000, 1.0f);
    const float dt = g.IO.DeltaTime;
    const float speed = 8.0f;
    float target_y = (hovered && !held) ? -2.0f : 0.0f;
    *anim_y = ImLerp(*anim_y, target_y, dt * speed);
    float target_scale = (held && hovered) ? 0.98f : 1.0f;
    *anim_scale = ImLerp(*anim_scale, target_scale, dt * speed);

    ImVec2 center = bb.GetCenter();
    ImVec2 anim_size = ImVec2(size.x * *anim_scale, size.y * *anim_scale);
    ImRect anim_bb(
        ImVec2(center.x - anim_size.x * 0.5f, center.y - anim_size.y * 0.5f + *anim_y),
        ImVec2(center.x + anim_size.x * 0.5f, center.y + anim_size.y * 0.5f + *anim_y)
    );

    // Mac 风格背景
    ImU32 col_bg, col_border;
    if (held && hovered) {
        col_bg = mac::CARD_ACTIVE;
        col_border = IM_COL32(80, 80, 90, 200);
    } else if (hovered) {
        col_bg = mac::CARD_HOVER;
        col_border = IM_COL32(60, 60, 66, 200);
    } else {
        col_bg = mac::CARD_BG;
        col_border = IM_COL32(50, 50, 55, 120);
    }
    draw_list->AddRectFilled(anim_bb.Min, anim_bb.Max, col_bg, mac::ROUND_MED);
    draw_list->AddRect(anim_bb.Min, anim_bb.Max, col_border, mac::ROUND_MED, 0, 1.0f);

    // 图标区域（用蓝色圆圈 + 首字母代替 LOGO）
    float icon_x = anim_bb.Min.x + left_margin;
    float icon_y = anim_bb.Min.y + (anim_size.y - icon_size) * 0.5f;
    ImVec2 icon_center(icon_x + icon_size * 0.5f, icon_y + icon_size * 0.5f);
    float icon_r = icon_size * 0.5f;

    // 蓝色渐变圆
    ImU32 icon_bg = hovered ? IM_COL32(0, 140, 255, 255) : mac::BLUE;
    draw_list->AddCircleFilled(icon_center, icon_r, icon_bg, 32);
    draw_list->AddCircleFilled(icon_center, icon_r * 0.85f, IM_COL32(0, 100, 220, 200), 32);

    // 首字母（取 label 第一个字符）
    char first_char[8] = {0};
    if (label && label[0]) {
        // 处理 UTF-8 首字符
        int len = 0;
        if ((unsigned char)label[0] < 0x80) len = 1;
        else if (((unsigned char)label[0] & 0xE0) == 0xC0) len = 2;
        else if (((unsigned char)label[0] & 0xF0) == 0xE0) len = 3;
        else len = 1;
        memcpy(first_char, label, len);
        first_char[len] = 0;
    }
    ImVec2 char_size = ImGui::CalcTextSize(first_char);
    draw_list->AddText(ImVec2(icon_center.x - char_size.x * 0.5f,
                              icon_center.y - char_size.y * 0.5f),
                       IM_COL32(255, 255, 255, 255), first_char);

    // 文本位置
    float text_start_x = icon_x + icon_size + icon_text_spacing;
    float total_text_height = label_size.y + subtitle_size.y + 4.0f;
    float text_start_y = anim_bb.Min.y + (anim_size.y - total_text_height) * 0.5f;

    // 主文本
    ImU32 label_color = hovered ? mac::TEXT_LIGHT : mac::TEXT_LIGHT;
    draw_list->AddText(ImVec2(text_start_x, text_start_y), label_color, label);

    // 副文本（灰色）
    ImU32 subtitle_color = hovered ? IM_COL32(160, 160, 160, 255) : mac::TEXT_GRAY;
    draw_list->AddText(ImVec2(text_start_x, text_start_y + label_size.y + 4.0f),
                       subtitle_color, subtitle);

    return pressed;
}

// ============================================================================
// ButtonTab - 标签按钮（带滑动选中动画）
// 完美还原原项目的滑动选中背景动画
// ============================================================================
// 全局状态（原项目用 static 变量，这里用全局保持一致）
static ImGuiID g_tab_current_selected = 0;
static ImVec2  g_tab_target_pos = ImVec2(0, 0);
static ImVec2  g_tab_current_pos = ImVec2(0, 0);
static ImVec2  g_tab_target_size = ImVec2(0, 0);
static ImVec2  g_tab_current_size = ImVec2(0, 0);
static float   g_tab_anim_alpha = 0.4f;
static bool    g_tab_first_frame = true;

bool ButtonTab(const char* label, const ImVec2& size_arg, int tab_id, int* current_tab) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size = ImGui::CalcItemSize(size_arg,
        label_size.x + style.FramePadding.x * 2.0f,
        label_size.y + style.FramePadding.y * 2.0f);

    const ImRect bb(pos, pos + size);
    ImGui::ItemSize(size, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, 0);

    // 首次调用时默认选中第一个 tab
    if (g_tab_first_frame) {
        g_tab_target_pos = pos;
        g_tab_current_pos = g_tab_target_pos;
        g_tab_target_size = size;
        g_tab_current_size = g_tab_target_size;
        g_tab_first_frame = false;
        g_tab_anim_alpha = 1.0f;
        g_tab_current_selected = id;
    }

    bool selected = (g_tab_current_selected == id);
    if (selected) {
        g_tab_target_pos = pos;
        g_tab_target_size = size;
    }

    // 滑动动画（0.08 插值速度，与原项目一致）
    float move_t = 0.08f;
    g_tab_current_pos.x += (g_tab_target_pos.x - g_tab_current_pos.x) * move_t;
    g_tab_current_pos.y += (g_tab_target_pos.y - g_tab_current_pos.y) * move_t;
    g_tab_current_size.x += (g_tab_target_size.x - g_tab_current_size.x) * move_t;
    g_tab_current_size.y += (g_tab_target_size.y - g_tab_current_size.y) * move_t;

    // 透明度动画
    float target_alpha = selected ? 1.0f : 0.5f;
    if (g_tab_anim_alpha < target_alpha)
        g_tab_anim_alpha = ImMin(g_tab_anim_alpha + g.IO.DeltaTime * 4.0f, target_alpha);
    else if (g_tab_anim_alpha > target_alpha)
        g_tab_anim_alpha = ImMax(g_tab_anim_alpha - g.IO.DeltaTime * 4.0f, target_alpha);

    // 绘制 Mac 蓝色毛玻璃选中背景
    float radius = g_tab_current_size.y * 0.5f;
    ImColor bg_color = ImColor(0.0f, 122.0f / 255.0f, 1.0f,
                               selected ? 0.75f : g_tab_anim_alpha * 0.5f);
    window->DrawList->AddRectFilled(g_tab_current_pos,
                                    g_tab_current_pos + g_tab_current_size,
                                    (ImU32)bg_color, radius,
                                    ImDrawFlags_RoundCornersRight);

    // 文本（水平垂直居中）
    ImVec4 text_color = selected
        ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
        : ImVec4(89.0f / 255.0f, 89.0f / 255.0f, 89.0f / 255.0f, 1.0f);
    float text_x = pos.x + (size.x - label_size.x) * 0.5f;
    float text_y = pos.y + (size.y - label_size.y) * 0.5f;

    ImGui::PushStyleColor(ImGuiCol_Text, text_color);
    ImGui::RenderText(ImVec2(text_x, text_y), label);
    ImGui::PopStyleColor();

    if (pressed) {
        g_tab_current_selected = id;
        g_tab_target_pos = pos;
        g_tab_target_size = size;
        g_tab_anim_alpha = 1.0f;
        if (current_tab) *current_tab = tab_id;
    }

    return pressed;
}

// ============================================================================
// HorizontalToggleBar - 水平切换栏（带滑动动画）
// ============================================================================
bool HorizontalToggleBar(const char* label, int* current_item,
                         const char* const items[], int items_count) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

    ImGui::PushID(label);
    const ImGuiID id = window->GetID(label);

    // 尺寸参数（与原项目一致）
    const float bar_height = 60.0f;
    const float toggle_height = 40.0f;
    const float rounding = toggle_height * 0.5f;
    const float left_margin = 10.0f;
    const float right_margin = 10.0f;
    const float toggle_area_width = 300.0f;

    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);

    ImVec2 size_arg(-1.0f, bar_height);
    ImVec2 actual_size = ImGui::CalcItemSize(size_arg,
        label_size.x + toggle_area_width + left_margin + right_margin, bar_height);

    const float total_width = actual_size.x;
    const float total_height = actual_size.y;

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect total_bb(pos, pos + ImVec2(total_width, total_height));

    ImGui::ItemSize(total_bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(total_bb, id)) {
        ImGui::PopID();
        return false;
    }

    // 文本区域（左侧）
    ImRect text_bb(
        ImVec2(pos.x + left_margin, pos.y),
        ImVec2(pos.x + left_margin + label_size.x, pos.y + total_height));

    // 切换条区域（右侧）
    ImRect toggle_bb(
        ImVec2(pos.x + total_width - right_margin - toggle_area_width, pos.y + 10.0f),
        ImVec2(pos.x + total_width - right_margin, pos.y + total_height - 10.0f));

    const float toggle_total_width = toggle_bb.Max.x - toggle_bb.Min.x;
    const float item_width = toggle_total_width / items_count;

    // 交互
    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(total_bb, id, &hovered, &held);

    bool value_changed = false;
    if (pressed && hovered) {
        ImVec2 local_mouse_pos = ImGui::GetMousePos();
        float toggle_left = toggle_bb.Min.x;
        for (int i = 0; i < items_count; i++) {
            ImRect item_bb(
                ImVec2(toggle_left + item_width * i, toggle_bb.Min.y),
                ImVec2(toggle_left + item_width * (i + 1), toggle_bb.Max.y));
            if (item_bb.Contains(local_mouse_pos)) {
                if (*current_item != i) {
                    *current_item = i;
                    value_changed = true;
                    ImGui::MarkItemEdited(id);
                }
                break;
            }
        }
    }

    // 动画状态
    ImGuiStorage* storage = window->DC.StateStorage;
    float* animation_pos = storage->GetFloatRef(id + items_count, 0.0f);
    const float target_pos = item_width * (*current_item);
    const float animation_speed = 0.06f;

    if (*animation_pos != target_pos) {
        float delta = target_pos - *animation_pos;
        *animation_pos += delta * animation_speed;
        if (fabs(*animation_pos - target_pos) < 0.5f)
            *animation_pos = target_pos;
    }

    // hover/press 缩放动画
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
        ImVec2(center.x + animated_size.x * 0.5f, center.y + animated_size.y * 0.5f + *anim_y));

    // 配色
    ImU32 col_text;
    bool* is_active = storage->GetBoolRef(id, false);
    if (pressed) *is_active = !(*is_active);
    if (held && hovered)       col_text = IM_COL32(255, 255, 255, 255);
    else if (*is_active)       col_text = IM_COL32(255, 255, 255, 255);
    else if (hovered)          col_text = mac::BLUE_LIGHT;
    else                       col_text = IM_COL32(102, 102, 102, 255);

    ImU32 col_toggle_border = IM_COL32(200, 200, 200, 255);
    ImU32 col_toggle_active = mac::BLUE_LIGHT;

    // 标签文本
    ImVec2 animated_text_pos(
        animated_bb.Min.x + left_margin,
        animated_bb.Min.y + (animated_bb.GetHeight() - label_size.y) * 0.5f);
    window->DrawList->AddText(animated_text_pos, col_text, label);

    // 切换条边框
    window->DrawList->AddRect(
        ImVec2(animated_bb.Max.x - right_margin - toggle_area_width, animated_bb.Min.y + 10.0f),
        ImVec2(animated_bb.Max.x - right_margin, animated_bb.Max.y - 10.0f),
        col_toggle_border, rounding, 0, 1.5f);

    // 活跃滑块
    float anim_toggle_left = animated_bb.Max.x - right_margin - toggle_area_width;
    float anim_item_width = toggle_area_width / items_count;
    ImRect slider_bb(
        ImVec2(anim_toggle_left + *animation_pos, animated_bb.Min.y + 10.0f),
        ImVec2(anim_toggle_left + *animation_pos + anim_item_width, animated_bb.Max.y - 10.0f));
    window->DrawList->AddRectFilled(slider_bb.Min, slider_bb.Max, col_toggle_active, rounding);

    // 选项文本
    for (int i = 0; i < items_count; i++) {
        const char* item_text = items[i];
        ImVec2 text_size = ImGui::CalcTextSize(item_text, NULL, true);
        float item_center_x = anim_toggle_left + anim_item_width * i + anim_item_width * 0.5f;
        ImVec2 item_text_pos(
            item_center_x - text_size.x * 0.5f,
            animated_bb.Min.y + 10.0f + (toggle_height - text_size.y) * 0.5f);
        bool is_over_slider = (item_center_x >= slider_bb.Min.x && item_center_x <= slider_bb.Max.x);
        ImU32 item_color = is_over_slider ? IM_COL32(255, 255, 255, 255) : IM_COL32(102, 102, 102, 255);
        window->DrawList->AddText(item_text_pos, item_color, item_text);
    }

    ImGui::PopID();
    return value_changed;
}

// ============================================================================
// DrawCircleGauge - 圆环进度
// ============================================================================
void DrawCircleGauge(ImVec2 center, float radius, float progress, ImU32 color, float thickness) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;
    progress = ImClamp(progress, 0.0f, 1.0f);
    // 背景圆环
    window->DrawList->PathArcTo(center, radius, 0.0f, IM_PI * 2.0f, 32);
    window->DrawList->PathStroke(IM_COL32(60, 60, 66, 100), 0, thickness);
    // 进度圆环
    if (progress > 0.0f) {
        float angle_start = -IM_PI * 0.5f;
        float angle_end = angle_start + progress * IM_PI * 2.0f;
        window->DrawList->PathArcTo(center, radius, angle_start, angle_end, 32);
        window->DrawList->PathStroke(color, 0, thickness);
    }
}

// ============================================================================
// DrawSystemInfoCard - 系统信息卡片（FPS/CPU/GPU/RAM）
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
        case SystemInfoType::FPS: title = "FPS"; unit = "fps"; max_value = 240.0f; break;
        case SystemInfoType::CPU: title = "CPU"; unit = "%"; max_value = 100.0f; break;
        case SystemInfoType::GPU: title = "GPU"; unit = "%"; max_value = 100.0f; break;
        case SystemInfoType::RAM: title = "RAM"; unit = "%"; max_value = 100.0f; break;
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

    // 动画
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
        ImVec2(center.x + animated_size.x * 0.5f, center.y + animated_size.y * 0.5f + *anim_y));

    // Mac 风格配色
    ImU32 col_bg, col_border, col_text, col_sub_text;
    if (held && hovered) {
        col_bg = mac::CARD_ACTIVE; col_border = IM_COL32(80, 80, 90, 200);
        col_text = IM_COL32(255, 255, 255, 255); col_sub_text = IM_COL32(255, 255, 255, 153);
    } else if (*is_active) {
        col_bg = mac::CARD_SEL; col_border = mac::BLUE;
        col_text = IM_COL32(255, 255, 255, 255); col_sub_text = IM_COL32(255, 255, 255, 180);
    } else if (hovered) {
        col_bg = mac::CARD_HOVER; col_border = IM_COL32(60, 60, 66, 200);
        col_text = mac::TEXT_LIGHT; col_sub_text = IM_COL32(235, 235, 240, 153);
    } else {
        col_bg = mac::CARD_BG; col_border = IM_COL32(50, 50, 55, 120);
        col_text = mac::TEXT_DIM; col_sub_text = IM_COL32(200, 200, 205, 140);
    }

    ImGui::RenderFrame(animated_bb.Min, animated_bb.Max, col_bg, true, mac::ROUND_MED);
    window->DrawList->AddRect(animated_bb.Min, animated_bb.Max, col_border, mac::ROUND_MED, 0, 1.0f);

    // 圆环
    ImVec2 content_center = animated_bb.GetCenter();
    float gauge_radius = ImMin(size_final.x, size_final.y) * 0.30f;
    float progress = value / max_value;

    ImU32 gauge_color;
    switch (type) {
        case SystemInfoType::FPS: gauge_color = mac::GREEN; break;
        case SystemInfoType::CPU: gauge_color = mac::ORANGE; break;
        case SystemInfoType::GPU: gauge_color = mac::PURPLE; break;
        case SystemInfoType::RAM: gauge_color = mac::BLUE; break;
    }
    DrawCircleGauge(content_center, gauge_radius, progress, gauge_color, 5.0f);

    // 数值
    char value_buf[16];
    snprintf(value_buf, sizeof(value_buf), "%.0f", value);
    ImVec2 value_size = ImGui::CalcTextSize(value_buf);
    ImVec2 title_size = ImGui::CalcTextSize(title);
    float text_spacing = 2.0f;
    float total_text_height = value_size.y + text_spacing + title_size.y;
    ImVec2 value_pos(content_center.x - value_size.x * 0.5f,
                     content_center.y - total_text_height * 0.5f);
    ImVec2 title_pos(content_center.x - title_size.x * 0.5f,
                     value_pos.y + value_size.y + text_spacing);

    ImU32 value_color = (*is_active || hovered || held)
        ? IM_COL32(255, 255, 255, 255) : mac::TEXT_LIGHT;
    window->DrawList->AddText(value_pos, value_color, value_buf);
    window->DrawList->AddText(title_pos, col_sub_text, title);

    return pressed;
}

// ============================================================================
// 简易系统信息获取（模拟，因为没有真实的 /proc 读取需求）
// ============================================================================
static float get_fps() { return ImGui::GetIO().Framerate; }
static float get_cpu() {
    // 简单读取 /proc/stat
    static float cached = 0.0f;
    static double last_time = 0;
    double now = ImGui::GetTime();
    if (now - last_time < 1.0) return cached;
    last_time = now;
    FILE* f = fopen("/proc/stat", "r");
    if (!f) return cached;
    unsigned long long user, nice, sys, idle;
    if (fscanf(f, "cpu %llu %llu %llu %llu", &user, &nice, &sys, &idle) == 4) {
        static unsigned long long lu = 0, ln = 0, ls = 0, li = 0;
        unsigned long long du = user - lu, dn = nice - ln, ds = sys - ls, di = idle - li;
        unsigned long long total = du + dn + ds;
        unsigned long long all = total + di;
        if (all > 0) cached = (float)total / all * 100.0f;
        lu = user; ln = nice; ls = sys; li = idle;
    }
    fclose(f);
    return cached;
}
static float get_ram() {
    static float cached = 0.0f;
    static double last_time = 0;
    double now = ImGui::GetTime();
    if (now - last_time < 2.0) return cached;
    last_time = now;
    FILE* f = fopen("/proc/meminfo", "r");
    if (!f) return cached;
    long total = 0, avail = 0;
    char buf[256];
    while (fgets(buf, sizeof(buf), f)) {
        if (!total) sscanf(buf, "MemTotal: %ld kB", &total);
        if (sscanf(buf, "MemAvailable: %ld kB", &avail)) break;
    }
    fclose(f);
    if (total <= 0) return 0.0f;
    cached = 100.0f - (float)avail / total * 100.0f;
    return cached;
}
static float get_gpu() {
    // 简单读取 GPU busy
    static float cached = 0.0f;
    static double last_time = 0;
    double now = ImGui::GetTime();
    if (now - last_time < 1.0) return cached;
    last_time = now;
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
        if (fscanf(f, "%d", &val) == 1) { fclose(f); cached = (float)val; return cached; }
        fclose(f);
    }
    return cached;
}

// ============================================================================
// render_aura_tab - 主渲染（三栏布局 + 3 个标签页）
// 完美还原原项目 MainUI.h 的布局结构
// ============================================================================
void render_aura_tab() {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // 标签页状态
    static int tab_main = 1;
    static int prev_tab_main = 1;
    static float tab_alpha = 1.0f;
    static bool tab_animating = false;

    // 标签切换动画
    if (prev_tab_main != tab_main) {
        tab_animating = true;
        tab_alpha = 0.0f;
        prev_tab_main = tab_main;
    }
    if (tab_animating) {
        tab_alpha += ImGui::GetIO().DeltaTime * 2.0f;
        if (tab_alpha >= 1.0f) { tab_alpha = 1.0f; tab_animating = false; }
    }

    // === 顶部卡片（带 LOGO 的 ButtonWithIcon）===
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::BeginChild("##AuraTopCard", ImVec2(-1, 100), false, ImGuiWindowFlags_NoScrollbar)) {
        ImVec2 child_pos = ImGui::GetWindowPos();
        ImVec2 child_size = ImGui::GetWindowSize();
        // 半透明白色背景 + 顶部圆角
        draw_list->AddRectFilled(child_pos,
            ImVec2(child_pos.x + child_size.x, child_pos.y + child_size.y),
            mac::GLASS_BG, mac::ROUND_LARGE, ImDrawFlags_RoundCornersTop);

        ImGui::SetCursorPosX(10.0f);
        ImGui::SetCursorPosY(10.0f);
        float card_w = ImGui::GetContentRegionAvail().x - 10.0f;
        float card_h = ImGui::GetContentRegionAvail().y - 10.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));
        if (ImGui::BeginChild("##AuraTopInner", ImVec2(card_w, card_h), false, ImGuiWindowFlags_NoScrollbar)) {
            if (ButtonWithIcon("\xe5\x8f\xaf\xe4\xb8\x80\xe5\xae\x9a",
                               "Unity \xe9\x80\x9a\xe7\x94\xa8\xe6\xb3\xa8\xe5\x85\xa5\xe6\xa8\xa1\xe6\x9d\xbf",
                               ImVec2(-1, -1))) {
                // 点击无操作
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    // === 左侧标签栏 + 右侧内容区 ===
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    // 左侧标签栏（200 宽）
    if (ImGui::BeginChild("##AuraLeftBar", ImVec2(200, -1), false, ImGuiWindowFlags_NoScrollbar)) {
        ImVec2 child_pos = ImGui::GetWindowPos();
        ImVec2 child_size = ImGui::GetWindowSize();
        draw_list->AddRectFilled(child_pos,
            ImVec2(child_pos.x + child_size.x, child_pos.y + child_size.y),
            mac::GLASS_BG, mac::ROUND_LARGE, ImDrawFlags_RoundCornersBottomLeft);

        ImGui::SetCursorPosX(0.0f);
        ImGui::SetCursorPosY(15.0f);
        float card_w = ImGui::GetContentRegionAvail().x;
        float card_h = ImGui::GetContentRegionAvail().y - 10.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));
        if (ImGui::BeginChild("##AuraLeftInner", ImVec2(card_w, card_h), false, ImGuiWindowFlags_NoScrollbar)) {
            // 三个标签按钮（用文字代替图标）
            if (ButtonTab("\xe4\xb8\xbb\xe9\xa1\xb5", ImVec2(-1, 80), 1, &tab_main)) {}
            if (ButtonTab("\xe5\x8a\x9f\xe8\x83\xbd", ImVec2(-1, 80), 2, &tab_main)) {}
            if (ButtonTab("\xe8\xae\xbe\xe7\xbd\xae", ImVec2(-1, 80), 3, &tab_main)) {}
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    ImGui::SameLine();

    // 右侧内容区
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::BeginChild("##AuraContent", ImVec2(-1, -1), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground)) {
        // 左上角圆角装饰（原项目的 PathArcTo 装饰）
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImVec2 o = origin;
        ImVec2 a = ImVec2(origin.x + 0.5f, origin.y + 40);
        ImVec2 b = ImVec2(origin.x + 40, origin.y);
        ImVec2 center = ImVec2(origin.x + 40, origin.y + 40);
        float radius = 40.0f;
        draw_list->PathClear();
        draw_list->PathLineTo(o);
        draw_list->PathLineTo(a);
        draw_list->PathArcTo(center, radius, IM_PI, IM_PI * 1.5f, 16);
        draw_list->PathFillConvex(IM_COL32(255, 255, 255, 32));

        ImGui::SetCursorPosX(10.0f);
        ImGui::SetCursorPosY(15.0f);
        float card_w = ImGui::GetContentRegionAvail().x - 10.0f;
        float card_h = ImGui::GetContentRegionAvail().y - 10.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, tab_alpha);

        if (ImGui::BeginChild("##AuraContentInner", ImVec2(card_w, card_h), false, ImGuiWindowFlags_NoScrollbar)) {
            // === 标签页 1：主页 ===
            if (tab_main == 1) {
                ImGui::Columns(2, nullptr, false);
                if (BeginGlassCard("##HomeCard1", ImVec2(-1, -1), true, ImGuiWindowFlags_NoScrollbar)) {
                    DrawAuraSectionTitle("\xe7\xb3\xbb\xe7\xbb\x9f\xe4\xbf\xa1\xe6\x81\xaf");
                    ImGui::Spacing();
                    // 4 个系统信息卡片
                    DrawSystemInfoCard(SystemInfoType::FPS, get_fps(), ImVec2(-1, 100));
                    ImGui::NextColumn();
                    if (BeginGlassCard("##HomeCard2", ImVec2(-1, -1), true, ImGuiWindowFlags_NoScrollbar)) {
                        DrawAuraSectionTitle("\xe9\xa1\xb9\xe7\x9b\xae\xe4\xbf\xa1\xe6\x81\xaf");
                        ImGui::Spacing();
                        ImGui::TextUnformatted("\xe9\xa1\xb9\xe7\x9b\xae: \xe5\x8f\xaf\xe4\xb8\x80\xe5\xae\x9a");
                        ImGui::Separator();
                        ImGui::TextUnformatted("Unity \xe9\x80\x9a\xe7\x94\xa8\xe6\xb3\xa8\xe5\x85\xa5\xe6\xa8\xa1\xe6\x9d\xbf");
                        ImGui::Separator();
                        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
                        ImGui::Separator();
                        const char* modes[] = {"\xe6\x9c\x89\xe5\x90\x8e\xe5\x8f\xb0", "\xe6\x97\xa0\xe5\x90\x8e\xe5\x8f\xb0"};
                        static int bg_mode = 0;
                        HorizontalToggleBar("\xe5\x90\x8e\xe5\x8f\xb0\xe8\xae\xbe\xe7\xbd\xae", &bg_mode, modes, 2);
                    }
                    EndGlassCard();
                    ImGui::NextColumn();
                    if (BeginGlassCard("##HomeCard3", ImVec2(-1, -1), true, ImGuiWindowFlags_NoScrollbar)) {
                        DrawAuraSectionTitle("\xe6\x80\xa7\xe8\x83\xbd\xe7\x9b\x91\xe6\x8e\xa7");
                        ImGui::Spacing();
                        DrawSystemInfoCard(SystemInfoType::CPU, get_cpu(), ImVec2(95, 95));
                        ImGui::SameLine();
                        DrawSystemInfoCard(SystemInfoType::GPU, get_gpu(), ImVec2(95, 95));
                        ImGui::SameLine();
                        DrawSystemInfoCard(SystemInfoType::RAM, get_ram(), ImVec2(95, 95));
                    }
                    EndGlassCard();
                    ImGui::Columns(1);
                }
                EndGlassCard();
            }
            // === 标签页 2：功能 ===
            else if (tab_main == 2) {
                ImGui::Columns(2, nullptr, false);
                if (BeginGlassCard("##FuncCard1", ImVec2(-1, -1), true, ImGuiWindowFlags_NoScrollbar)) {
                    DrawAuraSectionTitle("\xe5\x8a\x9f\xe8\x83\xbd\xe5\xbc\x80\xe5\x85\xb3");
                    ImGui::Spacing();
                    static bool feat1 = false, feat2 = false, feat3 = false, feat4 = false;
                    ImGui::Checkbox("\xe5\x8a\x9f\xe8\x83\xbd\xe4\xb8\x80", &feat1);
                    ImGui::Separator();
                    ImGui::Checkbox("\xe5\x8a\x9f\xe8\x83\xbd\xe4\xba\x8c", &feat2);
                    ImGui::Separator();
                    ImGui::Checkbox("\xe5\x8a\x9f\xe8\x83\xbd\xe4\xb8\x89", &feat3);
                    ImGui::Separator();
                    ImGui::Checkbox("\xe5\x8a\x9f\xe8\x83\xbd\xe5\x9b\x9b", &feat4);
                    ImGui::Separator();
                    static float slider1 = 0.5f;
                    ImGui::SliderFloat("\xe6\x95\xb0\xe5\x80\xbc\xe8\xb0\x83\xe8\x8a\x82", &slider1, 0.0f, 1.0f);
                }
                EndGlassCard();
                ImGui::NextColumn();
                if (BeginGlassCard("##FuncCard2", ImVec2(-1, -1), true, ImGuiWindowFlags_NoScrollbar)) {
                    DrawAuraSectionTitle("\xe9\xab\x98\xe7\xba\xa7\xe9\x80\x89\xe9\xa1\xb9");
                    ImGui::Spacing();
                    const char* modes[] = {"\xe6\xa8\xa1\xe5\xbc\x8f\xe4\xb8\x80", "\xe6\xa8\xa1\xe5\xbc\x8f\xe4\xba\x8c", "\xe6\xa8\xa1\xe5\xbc\x8f\xe4\xb8\x89"};
                    static int mode = 0;
                    HorizontalToggleBar("\xe8\xbf\x90\xe8\xa1\x8c\xe6\xa8\xa1\xe5\xbc\x8f", &mode, modes, 3);
                    ImGui::Separator();
                    const char* modes2[] = {"\xe5\xbc\x80", "\xe5\x85\xb3"};
                    static int mode2 = 0;
                    HorizontalToggleBar("\xe8\xb0\x83\xe8\xaf\x95\xe6\xa8\xa1\xe5\xbc\x8f", &mode2, modes2, 2);
                }
                EndGlassCard();
                ImGui::Columns(1);
            }
            // === 标签页 3：设置 ===
            else if (tab_main == 3) {
                ImGui::Columns(2, nullptr, false);
                if (BeginGlassCard("##SetCard1", ImVec2(-1, -1), true, ImGuiWindowFlags_NoScrollbar)) {
                    DrawAuraSectionTitle("\xe6\x98\xbe\xe7\xa4\xba\xe8\xae\xbe\xe7\xbd\xae");
                    ImGui::Spacing();
                    static float fps_limit = 60.0f;
                    ImGui::SliderFloat("\xe5\xb8\xa7\xe7\x8e\x87\xe9\x99\x90\xe5\x88\xb6", &fps_limit, 30.0f, 165.0f, "%.0f");
                    ImGui::Separator();
                    static float ui_scale = 1.0f;
                    ImGui::SliderFloat("UI \xe7\xbc\xa9\xe6\x94\xbe", &ui_scale, 0.5f, 2.0f, "%.2f");
                    ImGui::Separator();
                    const char* bg_modes[] = {"\xe6\x9c\x89\xe5\x90\x8e\xe5\x8f\xb0", "\xe6\x97\xa0\xe5\x90\x8e\xe5\x8f\xb0"};
                    static int bg_mode = 0;
                    HorizontalToggleBar("\xe5\x90\x8e\xe5\x8f\xb0\xe8\xae\xbe\xe7\xbd\xae", &bg_mode, bg_modes, 2);
                }
                EndGlassCard();
                ImGui::NextColumn();
                if (BeginGlassCard("##SetCard2", ImVec2(-1, -1), true, ImGuiWindowFlags_NoScrollbar)) {
                    DrawAuraSectionTitle("\xe7\xbb\x98\xe5\x9b\xbe\xe8\xae\xbe\xe7\xbd\xae");
                    ImGui::Spacing();
                    static float esp_size = 0.7f;
                    ImGui::SliderFloat("\xe7\xbb\x98\xe5\x9b\xbe\xe5\xa4\xa7\xe5\xb0\x8f", &esp_size, 0.3f, 1.5f, "%.1f");
                    ImGui::Separator();
                    if (ImGui::Button("\xe4\xbf\x9d\xe5\xad\x98\xe9\x85\x8d\xe7\xbd\xae", ImVec2(-1, 50))) {}
                    ImGui::Separator();
                    if (ImGui::Button("\xe5\x8a\xa0\xe8\xbd\xbd\xe9\x85\x8d\xe7\xbd\xae", ImVec2(-1, 50))) {}
                }
                EndGlassCard();
                ImGui::Columns(1);
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

} // namespace aura_ui
