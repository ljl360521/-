//
// MD3 UI 渲染模块
// 基于 MD3 颜色系统，实现 Material Design 3 风格的 UI 组件和动画
//

#pragma once

// 启用 ImGui 数学运算符重载（必须在 include imgui.h 之前定义）
#define IMGUI_DEFINE_MATH_OPERATORS

#include "md3_color.hpp"
#include "imgui.h"

namespace md3_ui {

// 初始化 MD3 配色方案（用蓝色作为种子色）
void init();

// 渲染 MD3 动画标签页内容
void render_md3_tab();

// 获取 MD3 配色方案
md3::scheme const& get_scheme();

} // namespace md3_ui
