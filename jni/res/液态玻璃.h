#pragma once

// ==================== 液态玻璃（Apple Liquid Glass）展示标签页 ====================
// 覆盖层是盖在游戏之上的独立透明 GLSurfaceView，它的 GL 上下文读不到游戏画面像素，
// 所以这里自己在标签页内部画一层生动的动态背景，再让折射/模糊/高光作用于这层自绘背景。

// 标签页正文绘制入口（不含 BeginTabItem/EndTabItem，与 音频标签页.h 的约定一致）。
void DrawLiquidGlassTab();

// 主动释放本模块占用的 GL 资源。
// 只在“当前 EGL 上下文仍然是创建这些资源的那个上下文”时才真正删除，
// 上下文已经销毁时只清空句柄（对死上下文调用 glDelete* 是未定义行为）。
// 可选挂载点：imguijni.cpp 中 ImGui_ImplOpenGL3_Shutdown() 之前。
void LiquidGlass_ReleaseGLObjects();
