//
// Aura UI 模块接口
// 完美移植自 AuraNexus 项目（MainDefinition.h + imgui_widgets.cpp + MainUI.h）
// 所有 widget 实现、配色、动画参数、窗口结构与原版完全一致
//

#pragma once
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"

// FontAwesome 图标定义（移植自原版 Program/Font/Icon.h）
// U+f015 / U+f05b / U+f013
#define ICON_FA_HOUSE      "\xef\x80\x95"
#define ICON_FA_CROSSHAIRS "\xef\x81\x9b"
#define ICON_FA_GEAR       "\xef\x80\x93"

namespace aura_ui {

// LOGO 纹理（原版 extern ImTextureID LOGO）
extern ImTextureID LOGO;

// 初始化：加载 LOGO 纹理（原版 LoadImages）
void init();

// 完整窗口渲染入口（原版 Draw_Meun -> MainUI.h）
void render_window();

// ---- 背景模糊窗口（原版 UpdateBlurWindow，OpenGL 后端空实现）----
void UpdateBlurWindow(const ImVec2& pos, const ImVec2& size, float radius, bool enabled, int blurStrength = 255);

// ---- 自定义 widget（移植自 imgui_widgets.cpp，原版为 ImGui 命名空间成员）----
// 带图标和副标题的按钮（原版 ImGui::ButtonWithIcon）
bool ButtonWithIcon(const char* label, const char* subtitle, ImTextureID icon, const ImVec2& size_arg, ImGuiButtonFlags flags = 0);

// 标签按钮（原版 ImGui::ButtonTab，带滑动选中动画）
bool ButtonTab(const char* label, const ImVec2& size_arg, ImGuiButtonFlags flags = 0);

// 水平切换栏（原版 ImGui::HorizontalToggleBar）
bool HorizontalToggleBar(const char* label, int* current_item, const char* const items[], int items_count);

// ---- 容器组件（移植自 MainDefinition.h）----
bool BeginGlassCard(const char* label, const ImVec2& size, bool border = true, ImGuiWindowFlags flags = 0);
void EndGlassCard();

// 带左侧蓝色竖条的标题（原版 DrawAuraSectionTitle）
void DrawAuraSectionTitle(const char* title);

// ---- 系统信息组件（移植自 MainDefinition.h）----
void DrawCircleGauge(ImVec2 center, float radius, float progress, ImU32 color, float thickness = 6.0f);

enum class SystemInfoType { FPS, CPU, GPU, RAM };
bool DrawSystemInfoCard(SystemInfoType type, float value, const ImVec2& size = ImVec2(100, 100));

// 系统信息读取（原版 GetRealFPS/GetRealCPU/GetRealGPU/GetRealRAMPercent）
int GetRealFPS();
int GetRealCPU();
int GetRealGPU();
int GetRealRAMPercent();

} // namespace aura_ui
