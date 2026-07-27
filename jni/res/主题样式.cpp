#include "主题样式.h"
#include "imgui.h"

// ==================== 旧项目主题（排除自定义） ====================
void ApplyLiquidGlassTheme()
{
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 22.0f;
    style.ChildRounding = 18.0f;
    style.FrameRounding = 14.0f;
    style.PopupRounding = 18.0f;
    style.ScrollbarRounding = 12.0f;
    style.GrabRounding = 12.0f;
    style.TabRounding = 15.0f;
    style.WindowBorderSize = 1.4f;
    style.ChildBorderSize = 1.2f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.WindowPadding = ImVec2(18.0f, 16.0f);
    style.DisplayWindowPadding = ImVec2(0.0f, 0.0f);
    style.DisplaySafeAreaPadding = ImVec2(0.0f, 0.0f);
    style.FramePadding = ImVec2(13.0f, 8.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.WindowMinSize = ImVec2(58.0f, 24.0f); // 刚好完整容纳左上角折叠按钮，避免压过按钮

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = ImVec4(0.90f, 0.97f, 1.00f, 1.00f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.52f, 0.66f, 0.76f, 1.00f);
    c[ImGuiCol_WindowBg] = ImVec4(0.020f, 0.035f, 0.055f, 0.00f);
    c[ImGuiCol_ChildBg] = ImVec4(0.030f, 0.060f, 0.095f, 0.00f);
    c[ImGuiCol_PopupBg] = ImVec4(0.040f, 0.075f, 0.115f, 0.18f);
    c[ImGuiCol_Border] = ImVec4(0.62f, 0.90f, 1.00f, 0.50f);
    c[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.28f, 0.36f, 0.20f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.38f, 0.54f, 0.70f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.58f, 0.76f, 0.86f);
    c[ImGuiCol_TitleBg] = ImVec4(0.06f, 0.11f, 0.17f, 0.00f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.22f, 0.34f, 0.00f);
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.04f, 0.07f, 0.11f, 0.00f);
    c[ImGuiCol_MenuBarBg] = ImVec4(0.08f, 0.15f, 0.22f, 0.52f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.04f, 0.06f, 0.20f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.26f, 0.55f, 0.76f, 0.55f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.36f, 0.72f, 0.95f, 0.72f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.55f, 0.86f, 1.00f, 0.88f);
    c[ImGuiCol_CheckMark] = ImVec4(0.48f, 0.88f, 1.00f, 1.00f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.40f, 0.78f, 1.00f, 0.78f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.62f, 0.95f, 1.00f, 1.00f);
    c[ImGuiCol_Button] = ImVec4(0.22f, 0.38f, 0.48f, 0.26f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.58f, 0.80f, 0.76f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.42f, 0.78f, 0.96f, 0.92f);
    c[ImGuiCol_Header] = ImVec4(0.18f, 0.38f, 0.52f, 0.24f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.18f, 0.48f, 0.70f, 0.62f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.28f, 0.62f, 0.86f, 0.78f);
    c[ImGuiCol_Separator] = ImVec4(0.45f, 0.80f, 1.00f, 0.24f);
    c[ImGuiCol_SeparatorHovered] = ImVec4(0.55f, 0.90f, 1.00f, 0.55f);
    c[ImGuiCol_SeparatorActive] = ImVec4(0.70f, 0.96f, 1.00f, 0.85f);
    c[ImGuiCol_ResizeGrip] = ImVec4(0.30f, 0.62f, 0.82f, 0.35f);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(0.45f, 0.82f, 1.00f, 0.65f);
    c[ImGuiCol_ResizeGripActive] = ImVec4(0.70f, 0.95f, 1.00f, 0.95f);
    c[ImGuiCol_Tab] = ImVec4(0.16f, 0.34f, 0.46f, 0.26f);
    c[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.62f, 0.84f, 0.84f);
    c[ImGuiCol_TabActive] = ImVec4(0.26f, 0.64f, 0.86f, 0.48f);
    c[ImGuiCol_TabUnfocused] = ImVec4(0.06f, 0.12f, 0.18f, 0.42f);
    c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.10f, 0.24f, 0.36f, 0.62f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.30f, 0.70f, 1.00f, 0.36f);
    c[ImGuiCol_NavHighlight] = ImVec4(0.45f, 0.85f, 1.00f, 0.92f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.02f, 0.04f, 0.06f, 0.45f);
}


void ApplyMusicUITheme()
{
    // 来自用户上传包 布局.cpp / YouXiStyle 的 UI 风格。
    // 注意：这里只移植样式，不调用 CreateContext、不重复加载字体，避免破坏当前稳定输入/字体/生命周期。
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    colors[ImGuiCol_WindowBg]         = ImVec4(0.09f, 0.11f, 0.17f, 1.00f);
    colors[ImGuiCol_ChildBg]          = ImVec4(0.12f, 0.14f, 0.20f, 0.50f);
    colors[ImGuiCol_PopupBg]          = ImVec4(0.10f, 0.12f, 0.18f, 1.00f);
    colors[ImGuiCol_TitleBg]          = ImVec4(0.13f, 0.16f, 0.24f, 1.00f);
    colors[ImGuiCol_TitleBgActive]    = ImVec4(0.14f, 0.16f, 0.25f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.10f, 0.12f, 0.18f, 1.00f);
    colors[ImGuiCol_Border]           = ImVec4(0.20f, 0.45f, 0.85f, 0.35f);
    colors[ImGuiCol_BorderShadow]     = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_Text]             = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TextDisabled]     = ImVec4(0.55f, 0.60f, 0.70f, 1.00f);
    colors[ImGuiCol_Button]           = ImVec4(0.18f, 0.22f, 0.32f, 1.00f);
    colors[ImGuiCol_ButtonHovered]    = ImVec4(0.25f, 0.50f, 0.90f, 1.00f);
    colors[ImGuiCol_ButtonActive]     = ImVec4(0.15f, 0.35f, 0.70f, 1.00f);
    colors[ImGuiCol_FrameBg]          = ImVec4(0.14f, 0.16f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]   = ImVec4(0.20f, 0.45f, 0.85f, 0.40f);
    colors[ImGuiCol_FrameBgActive]    = ImVec4(0.20f, 0.45f, 0.85f, 0.60f);
    colors[ImGuiCol_SliderGrab]       = ImVec4(0.25f, 0.50f, 0.90f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.30f, 0.60f, 1.00f, 1.00f);
    colors[ImGuiCol_Header]           = ImVec4(0.25f, 0.50f, 0.90f, 0.35f);
    colors[ImGuiCol_HeaderHovered]    = ImVec4(0.25f, 0.50f, 0.90f, 0.60f);
    colors[ImGuiCol_HeaderActive]     = ImVec4(0.25f, 0.50f, 0.90f, 0.80f);
    colors[ImGuiCol_Tab]              = ImVec4(0.13f, 0.16f, 0.24f, 1.00f);
    colors[ImGuiCol_TabHovered]       = ImVec4(0.25f, 0.50f, 0.90f, 0.50f);
    colors[ImGuiCol_TabActive]        = ImVec4(0.20f, 0.45f, 0.85f, 1.00f);
    colors[ImGuiCol_TabUnfocused]     = ImVec4(0.10f, 0.12f, 0.18f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.16f, 0.25f, 0.38f, 1.00f);
    colors[ImGuiCol_CheckMark]        = ImVec4(0.25f, 0.50f, 0.90f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]      = ImVec4(0.08f, 0.10f, 0.15f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]    = ImVec4(0.25f, 0.50f, 0.90f, 0.40f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.25f, 0.50f, 0.90f, 0.60f);
    colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.30f, 0.60f, 1.00f, 0.80f);
    colors[ImGuiCol_Separator]        = ImVec4(0.20f, 0.45f, 0.85f, 0.35f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.25f, 0.50f, 0.90f, 0.60f);
    colors[ImGuiCol_SeparatorActive]  = ImVec4(0.30f, 0.60f, 1.00f, 0.80f);
    colors[ImGuiCol_ResizeGrip]       = ImVec4(0.25f, 0.50f, 0.90f, 0.35f);
    colors[ImGuiCol_ResizeGripHovered]= ImVec4(0.25f, 0.50f, 0.90f, 0.65f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.30f, 0.60f, 1.00f, 0.90f);
    colors[ImGuiCol_TextSelectedBg]   = ImVec4(0.25f, 0.50f, 0.90f, 0.35f);
    colors[ImGuiCol_NavHighlight]     = ImVec4(0.30f, 0.60f, 1.00f, 0.90f);

    style.WindowRounding    = 10.0f;
    style.ChildRounding     = 10.0f;
    style.FrameRounding     = 8.0f;
    style.PopupRounding     = 8.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding      = 6.0f;
    style.TabRounding       = 8.0f;
    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.WindowPadding     = ImVec2(16.0f, 16.0f);
    style.FramePadding      = ImVec2(12.0f, 8.0f);
    style.ItemSpacing       = ImVec2(12.0f, 10.0f);
    style.ItemInnerSpacing  = ImVec2(8.0f, 6.0f);
    style.ScrollbarSize     = 18.0f;
    // 保留 1332 完美版窗口最小尺寸：刚好完整容纳左上角折叠按钮。
    style.WindowMinSize     = ImVec2(58.0f, 24.0f);
}

void ApplyOldProjectTheme(int themeIndex)
{
    if (themeIndex < Theme_Classic || themeIndex > Theme_MusicUI) themeIndex = Theme_LiquidGlass;
    switch ((ThemeType)themeIndex)
    {
    case Theme_Classic:
        ImGui::StyleColorsDark();
        break;
    case Theme_Light:
        ImGui::StyleColorsLight();
        {
            ImGuiStyle& style = ImGui::GetStyle();
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.98f, 0.98f, 0.98f, 1.00f);
            style.Colors[ImGuiCol_ChildBg] = ImVec4(0.96f, 0.96f, 0.96f, 1.00f);
            style.Colors[ImGuiCol_PopupBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.98f);
            style.Colors[ImGuiCol_Border] = ImVec4(0.70f, 0.70f, 0.80f, 0.40f);
            style.Colors[ImGuiCol_FrameBg] = ImVec4(0.90f, 0.90f, 0.95f, 1.00f);
            style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.85f, 0.85f, 0.90f, 1.00f);
            style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.80f, 0.80f, 0.85f, 1.00f);
            style.Colors[ImGuiCol_TitleBg] = ImVec4(0.90f, 0.90f, 0.95f, 1.00f);
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.80f, 0.80f, 0.90f, 1.00f);
            style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.95f, 0.95f, 0.98f, 1.00f);
            style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.95f, 0.95f, 0.98f, 1.00f);
            style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.70f, 0.70f, 0.80f, 1.00f);
            style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.60f, 0.60f, 0.70f, 1.00f);
            style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.50f, 0.50f, 0.60f, 1.00f);
            style.Colors[ImGuiCol_CheckMark] = ImVec4(0.20f, 0.50f, 0.90f, 1.00f);
            style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.30f, 0.60f, 1.00f, 1.00f);
            style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.20f, 0.50f, 0.90f, 1.00f);
            style.Colors[ImGuiCol_Button] = ImVec4(0.75f, 0.80f, 0.90f, 1.00f);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.65f, 0.70f, 0.85f, 1.00f);
            style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.55f, 0.60f, 0.75f, 1.00f);
            style.Colors[ImGuiCol_Header] = ImVec4(0.75f, 0.80f, 0.90f, 0.50f);
            style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.65f, 0.70f, 0.85f, 0.60f);
            style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.55f, 0.60f, 0.75f, 0.70f);
            style.Colors[ImGuiCol_Tab] = ImVec4(0.85f, 0.85f, 0.90f, 1.00f);
            style.Colors[ImGuiCol_TabHovered] = ImVec4(0.75f, 0.75f, 0.85f, 1.00f);
            style.Colors[ImGuiCol_TabActive] = ImVec4(0.65f, 0.70f, 0.85f, 1.00f);
            style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.90f, 0.90f, 0.95f, 1.00f);
            style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.80f, 0.80f, 0.90f, 1.00f);
            style.Colors[ImGuiCol_Text] = ImVec4(0.10f, 0.10f, 0.20f, 1.00f);
            style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.60f, 1.00f);
            style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.30f, 0.60f, 1.00f, 0.30f);
            style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.30f, 0.60f, 1.00f, 1.00f);
            style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.75f, 0.80f, 0.90f, 1.00f);
            style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.65f, 0.70f, 0.85f, 1.00f);
            style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.55f, 0.60f, 0.75f, 1.00f);
        }
        break;
    case Theme_Pink:
        ImGui::StyleColorsDark();
        {
            ImGuiStyle& style = ImGui::GetStyle();
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.98f, 0.90f, 0.95f, 1.00f);
            style.Colors[ImGuiCol_ChildBg] = ImVec4(0.96f, 0.88f, 0.93f, 1.00f);
            style.Colors[ImGuiCol_PopupBg] = ImVec4(0.98f, 0.92f, 0.96f, 0.98f);
            style.Colors[ImGuiCol_Border] = ImVec4(0.90f, 0.60f, 0.75f, 0.50f);
            style.Colors[ImGuiCol_FrameBg] = ImVec4(0.90f, 0.70f, 0.80f, 0.40f);
            style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.95f, 0.80f, 0.85f, 0.60f);
            style.Colors[ImGuiCol_FrameBgActive] = ImVec4(1.00f, 0.85f, 0.90f, 0.80f);
            style.Colors[ImGuiCol_TitleBg] = ImVec4(0.95f, 0.70f, 0.80f, 1.00f);
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.98f, 0.80f, 0.85f, 1.00f);
            style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.90f, 0.65f, 0.75f, 1.00f);
            style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.95f, 0.85f, 0.90f, 1.00f);
            style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.90f, 0.60f, 0.75f, 1.00f);
            style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.95f, 0.70f, 0.80f, 1.00f);
            style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(1.00f, 0.80f, 0.85f, 1.00f);
            style.Colors[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.40f, 0.65f, 1.00f);
            style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.95f, 0.50f, 0.70f, 1.00f);
            style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.60f, 0.75f, 1.00f);
            style.Colors[ImGuiCol_Button] = ImVec4(0.95f, 0.60f, 0.75f, 1.00f);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.98f, 0.70f, 0.80f, 1.00f);
            style.Colors[ImGuiCol_ButtonActive] = ImVec4(1.00f, 0.80f, 0.85f, 1.00f);
            style.Colors[ImGuiCol_Header] = ImVec4(0.95f, 0.65f, 0.75f, 0.50f);
            style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.98f, 0.75f, 0.80f, 0.60f);
            style.Colors[ImGuiCol_HeaderActive] = ImVec4(1.00f, 0.80f, 0.85f, 0.70f);
            style.Colors[ImGuiCol_Tab] = ImVec4(0.95f, 0.65f, 0.75f, 1.00f);
            style.Colors[ImGuiCol_TabHovered] = ImVec4(0.98f, 0.75f, 0.80f, 1.00f);
            style.Colors[ImGuiCol_TabActive] = ImVec4(1.00f, 0.80f, 0.85f, 1.00f);
            style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.90f, 0.60f, 0.70f, 1.00f);
            style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.95f, 0.70f, 0.80f, 1.00f);
            style.Colors[ImGuiCol_Text] = ImVec4(0.30f, 0.10f, 0.15f, 1.00f);
            style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.30f, 0.40f, 1.00f);
            style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(1.00f, 0.70f, 0.85f, 0.50f);
            style.Colors[ImGuiCol_NavHighlight] = ImVec4(1.00f, 0.60f, 0.80f, 1.00f);
            style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.95f, 0.60f, 0.75f, 1.00f);
            style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.98f, 0.70f, 0.80f, 1.00f);
            style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(1.00f, 0.80f, 0.85f, 1.00f);
        }
        break;
    case Theme_LiquidGlass:
        ApplyLiquidGlassTheme();
        break;
    case Theme_MusicUI:
        ApplyMusicUITheme();
        break;
    default:
        ApplyLiquidGlassTheme();
        break;
    }
}

