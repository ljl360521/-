//
// 高斯模糊 - 帧捕获模块
// Hook eglSwapBuffers，在目标 App 的 GL 上下文中截取帧缓冲
// 通过双缓冲 + 原子指针无锁传递给 ImGui 渲染线程
//

#pragma once
#include <cstdint>

namespace blur_capture {

// 初始化 Hook（在 native 线程启动时调用，会安装 eglSwapBuffers hook）
void init();

// 获取最新捕获的帧数据（线程安全，无锁）
// 返回 nullptr 表示暂无数据；out_w/out_h 输出尺寸
// 调用方应在 ImGui 渲染线程调用，拿到指针后立即用，下一帧可能被覆盖
const uint8_t* get_latest_frame(int* out_w, int* out_h);

// 是否有新帧可用（自上次 get_latest_frame 后）
bool has_new_frame();

} // namespace blur_capture
