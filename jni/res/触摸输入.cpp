#include "触摸输入.h"
#include "全局状态.h"
#include "配置初始化.h"
#include "imgui_internal.h"
#include <cmath>
#include <cstring>

// ==================== 旧项目触摸输入模块（适配 Java MotionEvent 转发版） ====================
#define MAX_TOUCH_SLOTS 5
#define TAP_MAX_DIST_PX 25.0f

struct TouchSlot {
    bool active;
    int pointer_id;
    ImVec2 pos;
};

struct TouchDragState {
    bool active;
    char window_name[64];
    ImVec2 offset;
};

struct SlotTapInfo {
    bool fired;
    ImVec2 pos;
    bool is_primary;
    ImVec2 start_pos;
    float max_dist_sq;
    bool tracking;
};

static TouchSlot      g_touch_slots[MAX_TOUCH_SLOTS] = {};
static TouchDragState g_drag_states[MAX_TOUCH_SLOTS] = {};
static SlotTapInfo    g_tap_infos[MAX_TOUCH_SLOTS]   = {};
bool           g_primary_just_released        = false;
bool           g_zero_mouse_delta_next        = false;
static ImVec2         g_last_valid_pos               = ImVec2(0, 0);
int            g_primary_slot_idx             = -1;

static ImGuiWindow* TouchFindWindowAtPos(ImVec2 pos) {
    if (!GImGui) return nullptr;
    ImGuiContext& g = *GImGui;
    for (int i = g.Windows.Size - 1; i >= 0; i--) {
        ImGuiWindow* w = g.Windows[i];
        if (!w->WasActive && !w->Active) continue;
        if (w->Flags & ImGuiWindowFlags_NoMove) continue;
        if (w->Flags & ImGuiWindowFlags_ChildWindow) continue;
        if (w->Flags & ImGuiWindowFlags_Tooltip) continue;
        ImRect wr = w->Collapsed ? w->TitleBarRect() : ImRect(w->Pos, ImVec2(w->Pos.x + w->Size.x, w->Pos.y + w->Size.y));
        if (pos.x >= wr.Min.x && pos.x <= wr.Max.x && pos.y >= wr.Min.y && pos.y <= wr.Max.y)
            return w;
    }
    return nullptr;
}

bool TouchIsWindowDraggedByOther(int skip_slot, ImGuiWindow* win) {
    if (!win) return false;
    for (int i = 0; i < MAX_TOUCH_SLOTS; i++) {
        if (i == skip_slot) continue;
        if (g_drag_states[i].active && strcmp(g_drag_states[i].window_name, win->Name) == 0)
            return true;
    }
    return false;
}

bool TouchIsWindowUsedByPrimary(ImGuiWindow* win) {
    if (!win || g_primary_slot_idx < 0) return false;
    ImGuiWindow* pri_win = TouchFindWindowAtPos(g_touch_slots[g_primary_slot_idx].pos);
    return (pri_win && pri_win->ID == win->ID);
}

int TouchFindSlotByPointerId(int pointer_id) {
    for (int i = 0; i < MAX_TOUCH_SLOTS; i++) {
        if (g_touch_slots[i].active && g_touch_slots[i].pointer_id == pointer_id) return i;
    }
    return -1;
}

int TouchFindFreeSlot() {
    for (int i = 0; i < MAX_TOUCH_SLOTS; i++) {
        if (!g_touch_slots[i].active) return i;
    }
    return -1;
}

void TouchStartTracking(int i, ImVec2 pos, bool is_primary) {
    if (i < 0 || i >= MAX_TOUCH_SLOTS) return;
    g_tap_infos[i].fired       = false;
    g_tap_infos[i].pos         = pos;
    g_tap_infos[i].is_primary  = is_primary;
    g_tap_infos[i].start_pos   = pos;
    g_tap_infos[i].max_dist_sq = 0.0f;
    g_tap_infos[i].tracking    = true;
}

void TouchEndTracking(int i) {
    if (i < 0 || i >= MAX_TOUCH_SLOTS) return;
    if (!g_tap_infos[i].tracking) return;
    const float threshold_sq = TAP_MAX_DIST_PX * TAP_MAX_DIST_PX;
    if (g_tap_infos[i].max_dist_sq <= threshold_sq && !g_drag_states[i].active) {
        g_tap_infos[i].fired = true;
        g_tap_infos[i].pos = g_touch_slots[i].pos;
    }
    g_tap_infos[i].tracking = false;
}

void TouchUpdateMoveDistance(int i, ImVec2 pos) {
    if (i < 0 || i >= MAX_TOUCH_SLOTS) return;
    if (!g_tap_infos[i].tracking) return;
    float dx = pos.x - g_tap_infos[i].start_pos.x;
    float dy = pos.y - g_tap_infos[i].start_pos.y;
    float d2 = dx * dx + dy * dy;
    if (d2 > g_tap_infos[i].max_dist_sq) g_tap_infos[i].max_dist_sq = d2;
}

void TouchHandlePointer(int pointer_id, bool down, ImVec2 pos) {
    if (down) {
        int slot = TouchFindSlotByPointerId(pointer_id);
        if (slot >= 0) {
            g_touch_slots[slot].pos = pos;
            TouchUpdateMoveDistance(slot, pos);
            return;
        }
        slot = TouchFindFreeSlot();
        if (slot < 0) return;
        bool is_primary = (g_primary_slot_idx < 0);
        g_touch_slots[slot].active = true;
        g_touch_slots[slot].pointer_id = pointer_id;
        g_touch_slots[slot].pos = pos;
        if (is_primary) g_primary_slot_idx = slot;
        TouchStartTracking(slot, pos, is_primary);
        return;
    }

    int slot = TouchFindSlotByPointerId(pointer_id);
    if (slot < 0) return;
    g_touch_slots[slot].pos = pos;
    TouchUpdateMoveDistance(slot, pos);
    bool was_primary = (slot == g_primary_slot_idx);
    TouchEndTracking(slot);
    g_touch_slots[slot].active = false;
    g_touch_slots[slot].pointer_id = -1;
    g_drag_states[slot].active = false;

    if (was_primary) {
        g_primary_just_released = true;
        g_zero_mouse_delta_next = true;
        g_primary_slot_idx = -1;
        for (int i = 0; i < MAX_TOUCH_SLOTS; i++) {
            if (g_touch_slots[i].active) {
                g_primary_slot_idx = i;
                g_tap_infos[i].is_primary = true;
                break;
            }
        }
    }
}

void 触摸_更新ImGuiIO(ImGuiIO& io) {
    TouchSlot* primary = nullptr;
    if (g_primary_slot_idx >= 0 && g_primary_slot_idx < MAX_TOUCH_SLOTS && g_touch_slots[g_primary_slot_idx].active)
        primary = &g_touch_slots[g_primary_slot_idx];
    if (!primary) {
        for (int i = 0; i < MAX_TOUCH_SLOTS; i++) {
            if (g_touch_slots[i].active) { primary = &g_touch_slots[i]; break; }
        }
    }

    if (g_primary_just_released) {
        io.MouseDown[0] = false;
        io.MouseDown[1] = false;
        io.MouseDown[2] = false;
        io.MousePos = g_last_valid_pos;
        g_primary_just_released = false;
    } else if (primary) {
        io.MouseDown[0] = true;
        io.MouseDown[1] = false;
        io.MouseDown[2] = false;
        io.MousePos = primary->pos;
        g_last_valid_pos = primary->pos;
    } else {
        io.MouseDown[0] = false;
        io.MouseDown[1] = false;
        io.MouseDown[2] = false;
        io.MousePos = g_last_valid_pos;
    }
}

void 触摸_帧后处理(ImGuiIO& io) {
    if (g_zero_mouse_delta_next) {
        io.MouseDelta = ImVec2(0.0f, 0.0f);
        g_zero_mouse_delta_next = false;
    }

    for (int i = 0; i < MAX_TOUCH_SLOTS; i++) {
        if (!g_touch_slots[i].active) {
            g_drag_states[i].active = false;
            continue;
        }
        if (i == g_primary_slot_idx) continue;

        TouchSlot* slot = &g_touch_slots[i];
        if (!g_drag_states[i].active) {
            const float drag_start_sq = (TAP_MAX_DIST_PX * 0.5f) * (TAP_MAX_DIST_PX * 0.5f);
            if (g_tap_infos[i].tracking && g_tap_infos[i].max_dist_sq < drag_start_sq) continue;
            ImGuiWindow* hit_win = TouchFindWindowAtPos(slot->pos);
            if (hit_win && !TouchIsWindowUsedByPrimary(hit_win) && !TouchIsWindowDraggedByOther(i, hit_win)) {
                g_drag_states[i].active = true;
                strncpy(g_drag_states[i].window_name, hit_win->Name, sizeof(g_drag_states[i].window_name) - 1);
                g_drag_states[i].window_name[sizeof(g_drag_states[i].window_name) - 1] = '\0';
                g_drag_states[i].offset = ImVec2(hit_win->Pos.x - slot->pos.x, hit_win->Pos.y - slot->pos.y);
            }
        }

        if (g_drag_states[i].active) {
            ImVec2 new_pos = ImVec2(slot->pos.x + g_drag_states[i].offset.x, slot->pos.y + g_drag_states[i].offset.y);
            ImGui::SetWindowPos(g_drag_states[i].window_name, new_pos, ImGuiCond_Always);
        }
    }
}

void 触摸_清除点击事件() {
    for (int i = 0; i < MAX_TOUCH_SLOTS; i++) g_tap_infos[i].fired = false;
}

bool 触摸_副指点击(ImVec2 rect_min, ImVec2 rect_max) {
    for (int i = 0; i < MAX_TOUCH_SLOTS; i++) {
        if (i == g_primary_slot_idx) continue;
        if (g_tap_infos[i].is_primary) continue;
        if (!g_tap_infos[i].fired) continue;
        ImVec2 p = g_tap_infos[i].pos;
        if (p.x >= rect_min.x && p.x <= rect_max.x && p.y >= rect_min.y && p.y <= rect_max.y) return true;
    }
    return false;
}

bool 触摸_主指点击(ImVec2 rect_min, ImVec2 rect_max) {
    for (int i = 0; i < MAX_TOUCH_SLOTS; i++) {
        if (!g_tap_infos[i].is_primary) continue;
        if (!g_tap_infos[i].fired) continue;
        ImVec2 p = g_tap_infos[i].pos;
        if (p.x >= rect_min.x && p.x <= rect_max.x && p.y >= rect_min.y && p.y <= rect_max.y) return true;
    }
    return false;
}

bool BeginMainTabItem(const char* label, int index)
{
    ImGuiTabItemFlags flags = (g_main_tab_index == index) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
    bool visible = ImGui::BeginTabItem(label, nullptr, flags);
    ImVec2 rmin = ImGui::GetItemRectMin();
    ImVec2 rmax = ImGui::GetItemRectMax();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || 触摸_主指点击(rmin, rmax) || 触摸_副指点击(rmin, rmax)) {
        g_main_tab_index = index;
        SaveAppConfigNow();
    }
    return visible;
}


