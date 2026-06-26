#pragma once
#include <string>
#include <jni.h>

namespace ImGuiIME {

/**
 * 初始化 IME 系统（在 JNI_OnLoad 中调用一次）
 */
void Init(JavaVM* vm);

/**
 * 自定义输入框控件
 * 用法和 ImGui::InputText 类似，但点击后弹出 Android 输入法
 * 
 * @param label    标签（支持 ##id 语法）
 * @param buf      字符缓冲区
 * @param bufSize  缓冲区大小
 * @param width    输入框宽度（-1 为自动）
 * @return         文本是否发生变化
 */
bool InputText(const char* label, char* buf, int bufSize, float width = -1.0f);

/**
 * 关闭当前激活的输入法（可选，通常自动处理）
 */
void HideKeyboard();

/**
 * 清理资源（在 shutdown 时调用）
 */
void Shutdown();

} // namespace ImGuiIME