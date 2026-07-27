#include "触摸窗口.h"
#include <vector>

static std::vector<ImGuiWindow*> g_touch_windows;
// 触摸穿透修复：旧值 160px 会把 ImGui 窗口外一圈透明区域也纳入 Java 触摸拦截范围，
// 导致窗口旁边的宿主按钮/摇杆等区域触摸不了。这里只保留很小的边缘容差，兼顾拖动/缩放连续性。
static const float kTouchResizePadding = 48.0f;

static ImRect GetTouchRectForWindow(ImGuiWindow* window) {
    if (!window) return ImRect();
    if (window->Collapsed) {
        // 折叠后只保留标题栏/折叠按钮的小区域给 ImGui，标题栏以外全部穿透给游戏。
        ImRect title = window->TitleBarRect();
        const float collapsedPadding = 8.0f;
        title.Min.x -= collapsedPadding;
        title.Min.y -= collapsedPadding;
        title.Max.x += collapsedPadding;
        title.Max.y += collapsedPadding;
        return title;
    }
    return ImRect(
        ImVec2(window->Pos.x - kTouchResizePadding, window->Pos.y - kTouchResizePadding),
        ImVec2(window->Pos.x + window->Size.x + kTouchResizePadding, window->Pos.y + window->Size.y + kTouchResizePadding)
    );
}

bool WindowContainsWithPadding(ImGuiWindow* window, float x, float y) {
    if (!window) return false;
    ImRect rect = GetTouchRectForWindow(window);
    return x >= rect.Min.x && x <= rect.Max.x && y >= rect.Min.y && y <= rect.Max.y;
}

bool MergeWindowBounds(ImGuiWindow* window, float* bounds, bool hasBounds) {
    if (!window || !bounds) return hasBounds;
    ImRect rect = GetTouchRectForWindow(window);
    float left   = rect.Min.x;
    float top    = rect.Min.y;
    float right  = rect.Max.x;
    float bottom = rect.Max.y;
    if (!hasBounds) {
        bounds[0] = left;
        bounds[1] = top;
        bounds[2] = right;
        bounds[3] = bottom;
        return true;
    }
    if (left < bounds[0]) bounds[0] = left;
    if (top < bounds[1]) bounds[1] = top;
    if (right > bounds[2]) bounds[2] = right;
    if (bottom > bounds[3]) bounds[3] = bottom;
    return true;
}

void ClearTouchWindows() {
    g_touch_windows.clear();
}

void RegisterTouchWindow(ImGuiWindow* window) {
    if (!window) return;
    for (ImGuiWindow* existing : g_touch_windows) {
        if (existing == window) return;
    }
    g_touch_windows.push_back(window);
}

bool AnyRegisteredWindowContains(float x, float y) {
    for (ImGuiWindow* window : g_touch_windows) {
        if (WindowContainsWithPadding(window, x, y)) return true;
    }
    return false;
}

bool MergeRegisteredWindowBounds(float* bounds) {
    bool hasBounds = false;
    for (ImGuiWindow* window : g_touch_windows) {
        hasBounds = MergeWindowBounds(window, bounds, hasBounds);
    }
    return hasBounds;
}

bool HasRegisteredTouchWindows() {
    return !g_touch_windows.empty();
}
