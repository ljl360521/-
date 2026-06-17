//
// Aura UI 模块接口
// 移植自 AuraNexus 项目的 Mac 风格毛玻璃 UI
//

#pragma once
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"

namespace aura_ui {

// 初始化（应用 Mac 风格样式）
void init();

// 主渲染入口（三栏布局 + 标签页）
void render_aura_tab();

// ---- 自定义 widget（移植自 imgui_widgets.cpp）----
// 带图标和副标题的按钮（图标用文字代替）
bool ButtonWithIcon(const char* label, const char* subtitle, const ImVec2& size_arg);

// 标签按钮（带滑动选中动画）
bool ButtonTab(const char* label, const ImVec2& size_arg, int tab_id, int* current_tab);

// 水平切换栏（带滑动动画）
bool HorizontalToggleBar(const char* label, int* current_item, const char* const items[], int items_count);

// ---- 容器组件 ----
// 玻璃卡片容器（半透明白色 + 圆角 40）
bool BeginGlassCard(const char* label, const ImVec2& size, bool border = true, ImGuiWindowFlags flags = 0);
void EndGlassCard();

// 带左侧蓝色竖条的标题
void DrawAuraSectionTitle(const char* title);

// ---- 系统信息组件 ----
// 圆环进度
void DrawCircleGauge(ImVec2 center, float radius, float progress, ImU32 color, float thickness = 6.0f);

// 系统信息卡片（FPS/CPU/GPU/RAM）
enum class SystemInfoType { FPS, CPU, GPU, RAM };
bool DrawSystemInfoCard(SystemInfoType type, float value, const ImVec2& size = ImVec2(100, 100));

} // namespace aura_ui
