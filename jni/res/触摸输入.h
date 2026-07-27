#pragma once
#include "imgui.h"
void TouchHandlePointer(int pointer_id, bool down, ImVec2 pos);
void 触摸_更新ImGuiIO(ImGuiIO& io);
void 触摸_帧后处理(ImGuiIO& io);
void 触摸_清除点击事件();
bool 触摸_副指点击(ImVec2 rect_min, ImVec2 rect_max);
bool 触摸_主指点击(ImVec2 rect_min, ImVec2 rect_max);
bool BeginMainTabItem(const char* label, int index);
