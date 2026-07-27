#pragma once
// ImGenie 捕获纹理回调模块 —— genie 动画帧捕获

#include "imgui.h"
#include "res/ImGenie.h"
#include <cstdint>

ImTextureRef 捕获_创建纹理(int32_t width, int32_t height, ImDrawData* drawData);
void          捕获_销毁纹理(const ImTextureRef& tex);