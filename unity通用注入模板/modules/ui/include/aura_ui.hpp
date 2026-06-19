//
// Aura UI 模块接口
// 移植自 AuraNexus 项目，适配 Vulkan 渲染后端
//

#pragma once
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"

// FontAwesome 图标定义
#define ICON_FA_HOUSE      "\xef\x80\x95"
#define ICON_FA_CROSSHAIRS "\xef\x81\x9b"
#define ICON_FA_GEAR       "\xef\x80\x93"
#define ICON_FA_EYE       "\xef\x81\xae"
#define ICON_FA_EYE_SLASH "\xef\x81\xb0"

namespace aura_ui {

// LOGO 纹理（Vulkan 后端为 VkDescriptorSet）
extern ImTextureID LOGO;

// 初始化：加载 LOGO 纹理（Vulkan 纹理创建）
void init();

// 完整窗口渲染入口
void render_window();

// 灵动岛（窗口上方胶囊，点击切换窗口显示/隐藏，丝滑动画）
void render_dynamic_island();

// 查询灵动岛当前边界（屏幕坐标），用于触摸事件命中判断
bool get_dynamic_island_bounds(float outBounds[4]);

// ---- 自定义 widget ----
bool ButtonWithIcon(const char* label, const char* subtitle, ImTextureID icon, const ImVec2& size_arg, ImGuiButtonFlags flags = 0);
bool ButtonTab(const char* label, const ImVec2& size_arg, ImGuiButtonFlags flags = 0);
bool HorizontalToggleBar(const char* label, int* current_item, const char* const items[], int items_count);

// ---- 容器组件 ----
bool BeginGlassCard(const char* label, const ImVec2& size, bool border = true, ImGuiWindowFlags flags = 0);
void EndGlassCard();
void DrawAuraSectionTitle(const char* title);

// ---- 系统信息组件 ----
void DrawCircleGauge(ImVec2 center, float radius, float progress, ImU32 color, float thickness = 6.0f);

enum class SystemInfoType { FPS, CPU, GPU, RAM };
bool DrawSystemInfoCard(SystemInfoType type, float value, const ImVec2& size = ImVec2(100, 100));

int GetRealFPS();
int GetRealCPU();
int GetRealGPU();
int GetRealRAMPercent();

} // namespace aura_ui
