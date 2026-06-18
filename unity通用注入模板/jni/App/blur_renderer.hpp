//
// 高斯模糊 - 渲染模块
// GLES 3.0 双 Pass 高斯模糊（水平 + 垂直）
// 从 blur_capture 获取目标 App 帧缓冲，模糊后渲染为圆角矩形背景
//

#pragma once
#include <cstdint>
#include "imgui.h"  // ImVec2

namespace blur_renderer {

// 初始化（创建 shader / FBO / 纹理，在 ImGui GL 上下文中调用）
void init();

// 渲染高斯模糊背景
// pos/size: 窗口区域（屏幕坐标）
// radius: 圆角半径
// blur_radius: 模糊强度（像素，建议 8-20）
// enabled: 是否启用（false 时清理资源）
void render(const ImVec2& pos, const ImVec2& size, float radius, float blur_radius, bool enabled);

} // namespace blur_renderer
