#pragma once
#include "imgui_internal.h"
// 触摸窗口注册与命中检测（供 Java 触摸穿透判断）
void ClearTouchWindows();
void RegisterTouchWindow(ImGuiWindow* window);
bool AnyRegisteredWindowContains(float x, float y);
bool MergeRegisteredWindowBounds(float* bounds);
bool HasRegisteredTouchWindows();
