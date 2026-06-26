#pragma once
#include <string>
#include <jni.h>

namespace ImGuiTools {

/**
 * 初始化工具类（在 JNI_OnLoad 中调用一次）
 */
void Init(JavaVM* vm);

/**
 * 打开网页
 * 
 * @param url       网页地址
 * @param internal  true = 应用内 WebView 打开, false = 外部浏览器打开
 */
void OpenURL(const char* url, bool internal = false);

/**
 * 关闭应用内 WebView（仅在 internal 模式下有效）
 */
void CloseWebView();

/**
 * 应用内 WebView 是否正在显示
 */
bool IsWebViewVisible();

/**
 * 清理资源
 */
void Shutdown();

} // namespace ImGuiTools