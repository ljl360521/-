#include "ImGuiMultiTouch.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <chrono>

namespace ImGuiMultiTouch {

uint64_t MultiTouchManager::GetCurrentTimeMs() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

MultiTouchManager& MultiTouchManager::GetInstance() {
    static MultiTouchManager instance;
    return instance;
}

void MultiTouchManager::Init() {
    m_ActiveTouchPoints.reserve(MAX_TOUCH_POINTS);
    m_TouchIdToIndex.reserve(MAX_TOUCH_POINTS);
    m_EventQueue.reserve(64);
    m_ProcessingQueue.reserve(64);
}

// ==================== 线程安全入队 ====================

void MultiTouchManager::EnqueueEvent(const TouchEvent& event) {
    std::lock_guard<std::mutex> lock(m_EventQueueMutex);
    m_EventQueue.push_back(event);
}

void MultiTouchManager::EnqueueDown(int pointerId, float x, float y, float pressure) {
    EnqueueEvent(TouchEvent(TouchEventType::Down, pointerId, x, y, pressure, GetCurrentTimeMs()));
}

void MultiTouchManager::EnqueueMove(int pointerId, float x, float y, float pressure) {
    EnqueueEvent(TouchEvent(TouchEventType::Move, pointerId, x, y, pressure, GetCurrentTimeMs()));
}

void MultiTouchManager::EnqueueUp(int pointerId) {
    EnqueueEvent(TouchEvent(TouchEventType::Up, pointerId, 0, 0, 0, GetCurrentTimeMs()));
}

void MultiTouchManager::EnqueueCancel() {
    EnqueueEvent(TouchEvent(TouchEventType::Cancel, -1, 0, 0, 0, GetCurrentTimeMs()));
}

// ==================== 监听器管理 ====================

void MultiTouchManager::AddListener(ITouchEventListener* listener) {
    if (!listener) return;
    for (auto* l : m_Listeners) {
        if (l == listener) return;
    }
    m_Listeners.push_back(listener);
    m_ListenersDirty = true;
}

void MultiTouchManager::RemoveListener(ITouchEventListener* listener) {
    m_Listeners.erase(
        std::remove(m_Listeners.begin(), m_Listeners.end(), listener),
        m_Listeners.end()
    );
}

void MultiTouchManager::SortListenersIfNeeded() {
    if (!m_ListenersDirty) return;
    std::stable_sort(m_Listeners.begin(), m_Listeners.end(),
        [](ITouchEventListener* a, ITouchEventListener* b) {
            return a->GetPriority() < b->GetPriority();
        });
    m_ListenersDirty = false;
}

// ==================== 事件分发 ====================

void MultiTouchManager::DispatchDown(int touchId, const ImVec2& pos, float pressure) {
    SortListenersIfNeeded();
    for (auto* listener : m_Listeners) {
        if (listener->OnTouchDown(touchId, pos, pressure)) break;
    }
}

void MultiTouchManager::DispatchMove(int touchId, const ImVec2& pos, const ImVec2& delta, float pressure) {
    SortListenersIfNeeded();
    for (auto* listener : m_Listeners) {
        if (listener->OnTouchMove(touchId, pos, delta, pressure)) break;
    }
}

void MultiTouchManager::DispatchUp(int touchId, const ImVec2& lastPos) {
    SortListenersIfNeeded();
    for (auto* listener : m_Listeners) {
        if (listener->OnTouchUp(touchId, lastPos)) break;
    }
}

void MultiTouchManager::DispatchCancel() {
    for (auto* listener : m_Listeners) {
        listener->OnTouchCancel();
    }
}

void MultiTouchManager::DispatchStale(int touchId) {
    for (auto* listener : m_Listeners) {
        listener->OnTouchStale(touchId);
    }
}

// ==================== 触控点查找 ====================

TouchPoint* MultiTouchManager::FindTouchPoint(int pointerId) {
    auto it = m_TouchIdToIndex.find(pointerId);
    if (it != m_TouchIdToIndex.end()) {
        return &m_ActiveTouchPoints[it->second];
    }
    return nullptr;
}

// ==================== 核心：事件处理 + 即时分发 ====================

void MultiTouchManager::ProcessEventQueue() {
    {
        std::lock_guard<std::mutex> lock(m_EventQueueMutex);
        m_ProcessingQueue.swap(m_EventQueue);
    }

    for (const auto& event : m_ProcessingQueue) {
        switch (event.type) {

        case TouchEventType::Down: {
            TouchPoint* existing = FindTouchPoint(event.pointerId);
            if (existing) {
                // 触控点还在（上次 Up 丢失），先补发 Up
                if (existing->isDown) {
                    DispatchUp(existing->id, existing->position);
                }
                existing->position = ImVec2(event.x, event.y);
                existing->delta = ImVec2(0, 0);
                existing->isDown = true;
                existing->pressure = event.pressure;
                existing->lastEventTime = event.timestamp;
                existing->stale = false;
                existing->pendingRemove = false;
            } else if (m_ActiveTouchPoints.size() < MAX_TOUCH_POINTS) {
                TouchPoint tp;
                tp.id = event.pointerId;
                tp.position = ImVec2(event.x, event.y);
                tp.delta = ImVec2(0, 0);
                tp.isDown = true;
                tp.pressure = event.pressure;
                tp.lastEventTime = event.timestamp;
                tp.stale = false;
                tp.pendingRemove = false;
                m_ActiveTouchPoints.push_back(tp);
                m_TouchIdToIndex[event.pointerId] = m_ActiveTouchPoints.size() - 1;
            }
            // ★ 状态更新完毕后立即分发
            DispatchDown(event.pointerId, ImVec2(event.x, event.y), event.pressure);
            break;
        }

        case TouchEventType::Move: {
            TouchPoint* tp = FindTouchPoint(event.pointerId);
            if (tp && tp->isDown) {
                ImVec2 delta(event.x - tp->position.x, event.y - tp->position.y);
                tp->delta.x += delta.x;
                tp->delta.y += delta.y;
                tp->position = ImVec2(event.x, event.y);
                tp->pressure = event.pressure;
                tp->lastEventTime = event.timestamp;
                tp->stale = false;
                DispatchMove(event.pointerId, tp->position, delta, event.pressure);
            }
            break;
        }

        case TouchEventType::Up: {
            TouchPoint* tp = FindTouchPoint(event.pointerId);
            ImVec2 lastPos(0, 0);
            if (tp) {
                lastPos = tp->position;
                tp->isDown = false;
                tp->lastEventTime = event.timestamp;
                tp->pendingRemove = true;
            }
            // ★ 即使触控点不存在也分发，确保监听器能清理
            DispatchUp(event.pointerId, lastPos);
            break;
        }

        case TouchEventType::Cancel: {
            for (auto& tp : m_ActiveTouchPoints) {
                tp.isDown = false;
                tp.pendingRemove = true;
            }
            DispatchCancel();
            break;
        }

        } // switch
    }

    m_ProcessingQueue.clear();
}

// ==================== 清理待删除触控点 ====================

void MultiTouchManager::CleanupPendingRemove() {
    for (int i = (int)m_ActiveTouchPoints.size() - 1; i >= 0; --i) {
        if (m_ActiveTouchPoints[i].pendingRemove) {
            int id = m_ActiveTouchPoints[i].id;
            if ((size_t)i < m_ActiveTouchPoints.size() - 1) {
                std::swap(m_ActiveTouchPoints[i], m_ActiveTouchPoints.back());
                m_TouchIdToIndex[m_ActiveTouchPoints[i].id] = i;
            }
            m_ActiveTouchPoints.pop_back();
            m_TouchIdToIndex.erase(id);
        }
    }
}

// ==================== 过时检测 ====================

void MultiTouchManager::DetectStaleTouchPoints(uint64_t timeoutMs) {
    uint64_t now = GetCurrentTimeMs();

    for (int i = (int)m_ActiveTouchPoints.size() - 1; i >= 0; --i) {
        TouchPoint& tp = m_ActiveTouchPoints[i];
        if (tp.lastEventTime == 0 || tp.pendingRemove) continue;

        uint64_t elapsed = now - tp.lastEventTime;
        if (elapsed > timeoutMs) {
            if (!tp.stale) {
                tp.stale = true;
                DispatchStale(tp.id);
            } else {
                // 过时超过两帧，强制 Up + 删除
                DispatchUp(tp.id, tp.position);
                int id = tp.id;
                if ((size_t)i < m_ActiveTouchPoints.size() - 1) {
                    std::swap(m_ActiveTouchPoints[i], m_ActiveTouchPoints.back());
                    m_TouchIdToIndex[m_ActiveTouchPoints[i].id] = i;
                }
                m_ActiveTouchPoints.pop_back();
                m_TouchIdToIndex.erase(id);
            }
        }
    }
}

// ==================== NewFrame ====================

void MultiTouchManager::NewFrame() {
    // 1. 清理上一帧标记待删除的触控点
    CleanupPendingRemove();

    // 2. 清除上一帧 delta
    for (auto& touch : m_ActiveTouchPoints) {
        touch.delta = ImVec2(0, 0);
    }

    // 3. 处理事件队列（内部立即分发给监听器）
    ProcessEventQueue();

    // 4. 过时检测
    DetectStaleTouchPoints(2000);
}

// ==================== 兼容旧接口 ====================

void MultiTouchManager::AddTouchEvent(int pointerId, float x, float y, bool isDown, float pressure) {
    if (isDown) EnqueueDown(pointerId, x, y, pressure);
    else EnqueueUp(pointerId);
}

void MultiTouchManager::UpdateTouchPosition(int pointerId, float x, float y, float pressure) {
    EnqueueMove(pointerId, x, y, pressure);
}

void MultiTouchManager::RemoveTouchPoint(int pointerId) {
    EnqueueUp(pointerId);
}

void MultiTouchManager::ClearAllTouchPoints() {
    EnqueueCancel();
}

void MultiTouchManager::ForceRemoveAllTouchPoints() {
    m_ActiveTouchPoints.clear();
    m_TouchIdToIndex.clear();
    std::lock_guard<std::mutex> lock(m_EventQueueMutex);
    m_EventQueue.clear();
}

const std::vector<TouchPoint>& MultiTouchManager::GetActiveTouchPoints() const {
    return m_ActiveTouchPoints;
}

bool MultiTouchManager::IsTouchActive(int pointerId) const {
    return m_TouchIdToIndex.find(pointerId) != m_TouchIdToIndex.end();
}

const TouchPoint* MultiTouchManager::GetTouchPoint(int pointerId) const {
    auto it = m_TouchIdToIndex.find(pointerId);
    if (it != m_TouchIdToIndex.end()) return &m_ActiveTouchPoints[it->second];
    return nullptr;
}

// ==================== 全局函数 ====================

void Initialize() { MultiTouchManager::GetInstance().Init(); }
void Shutdown() { MultiTouchManager::GetInstance().ForceRemoveAllTouchPoints(); }
void NewFrame() { MultiTouchManager::GetInstance().NewFrame(); }

bool HandleAndroidTouchEvent(const AInputEvent* input_event) {
    if (AInputEvent_getType(input_event) != AINPUT_EVENT_TYPE_MOTION) return false;
    int32_t action = AMotionEvent_getAction(input_event);
    int32_t actionMasked = action & AMOTION_EVENT_ACTION_MASK;
    int32_t pointerIndex = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
    size_t pointerCount = AMotionEvent_getPointerCount(input_event);
    MultiTouchManager& tm = MultiTouchManager::GetInstance();
    switch (actionMasked) {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN: {
            int32_t id = AMotionEvent_getPointerId(input_event, pointerIndex);
            tm.EnqueueDown(id, AMotionEvent_getX(input_event, pointerIndex), AMotionEvent_getY(input_event, pointerIndex), AMotionEvent_getPressure(input_event, pointerIndex));
            break;
        }
        case AMOTION_EVENT_ACTION_MOVE:
            for (size_t i = 0; i < pointerCount; ++i) {
                int32_t id = AMotionEvent_getPointerId(input_event, i);
                tm.EnqueueMove(id, AMotionEvent_getX(input_event, i), AMotionEvent_getY(input_event, i), AMotionEvent_getPressure(input_event, i));
            }
            break;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP:
            tm.EnqueueUp(AMotionEvent_getPointerId(input_event, pointerIndex));
            break;
        case AMOTION_EVENT_ACTION_CANCEL:
            tm.EnqueueCancel();
            break;
    }
    ImGuiIO& io = ImGui::GetIO();
    if (pointerCount > 0) {
        io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
        io.AddMousePosEvent(AMotionEvent_getX(input_event, 0), AMotionEvent_getY(input_event, 0));
        if (actionMasked == AMOTION_EVENT_ACTION_DOWN || actionMasked == AMOTION_EVENT_ACTION_UP)
            io.AddMouseButtonEvent(0, actionMasked == AMOTION_EVENT_ACTION_DOWN);
    }
    return true;
}

// ==================== Widgets ====================
namespace Widgets {

bool MultiTouchButton(const char* label, const ImVec2& size) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    ImVec2 button_size = ImGui::CalcItemSize(size, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);
    const ImRect bb(window->DC.CursorPos, ImVec2(window->DC.CursorPos.x + button_size.x, window->DC.CursorPos.y + button_size.y));
    ImGui::ItemSize(button_size, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, window->GetID(label))) return false;
    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, window->GetID(label), &hovered, &held);
    const auto& tp = MultiTouchManager::GetInstance().GetActiveTouchPoints();
    for (const auto& t : tp) { if (t.isDown && !t.stale && bb.Contains(t.position)) { pressed = true; break; } }
    ImGui::RenderNavHighlight(bb, window->GetID(label));
    ImGui::RenderFrame(bb.Min, bb.Max, ImGui::GetColorU32(pressed ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button), true, style.FrameRounding);
    ImGui::RenderTextClipped(ImVec2(bb.Min.x+style.FramePadding.x, bb.Min.y+style.FramePadding.y), ImVec2(bb.Max.x-style.FramePadding.x, bb.Max.y-style.FramePadding.y), label, NULL, &label_size, style.ButtonTextAlign, &bb);
    return pressed;
}

bool MultiTouchSlider(const char* label, float* value, float min, float max, const char* format, ImGuiSliderFlags flags) {
    bool changed = ImGui::SliderFloat(label, value, min, max, format, flags);
    ImRect bb(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    for (const auto& t : MultiTouchManager::GetInstance().GetActiveTouchPoints()) {
        if (t.isDown && !t.stale && bb.Contains(t.position)) {
            float nv = min + ImClamp((t.position.x - bb.Min.x) / (bb.Max.x - bb.Min.x), 0.0f, 1.0f) * (max - min);
            if (*value != nv) { *value = nv; changed = true; }
        }
    }
    return changed;
}

bool MultiTouchDragFloat(const char* label, float* value, float speed, float min, float max, const char* format, ImGuiSliderFlags flags) {
    bool changed = ImGui::DragFloat(label, value, speed, min, max, format, flags);
    ImRect bb(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    for (const auto& t : MultiTouchManager::GetInstance().GetActiveTouchPoints()) {
        if (t.isDown && !t.stale && bb.Contains(t.position) && t.delta.x != 0.0f) {
            float nv = *value + t.delta.x * speed;
            if (min < max) nv = ImClamp(nv, min, max);
            if (*value != nv) { *value = nv; changed = true; }
        }
    }
    return changed;
}

bool MultiTouchKnob(const char* label, float* p_value, float v_min, float v_max, const char* format, float size) {
    ImGuiWindow* w = ImGui::GetCurrentWindow(); if (w->SkipItems) return false;
    ImGuiContext& g = *GImGui; const ImGuiStyle& s = g.Style;
    float r = (size > 0) ? size*0.5f : ImGui::GetTextLineHeight()*2.0f;
    ImVec2 p = w->DC.CursorPos; ImRect bb(p, ImVec2(p.x+r*2, p.y+r*2));
    ImGui::ItemSize(bb, s.FramePadding.y); if (!ImGui::ItemAdd(bb, w->GetID(label))) return false;
    bool vc = false; ImVec2 c(bb.Min.x+r, bb.Min.y+r);
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) { auto mp=ImGui::GetIO().MousePos; float dx=mp.x-c.x,dy=mp.y-c.y; if(dx||dy){*p_value=v_min+((atan2f(dy,dx)+IM_PI)/(2*IM_PI))*(v_max-v_min);vc=true;}}
    float t=(*p_value-v_min)/(v_max-v_min), am=IM_PI*0.75f, ax=IM_PI*2.25f, av=am+(ax-am)*t;
    w->DrawList->AddCircleFilled(c,r,ImGui::GetColorU32(ImGuiCol_FrameBg),32);
    w->DrawList->AddLine(c,ImVec2(c.x+cosf(av)*r*0.8f,c.y+sinf(av)*r*0.8f),ImGui::GetColorU32(ImGuiCol_SliderGrabActive),2);
    w->DrawList->AddCircleFilled(c,r*0.2f,ImGui::GetColorU32(ImGuiCol_SliderGrabActive),12);
    if(format){char b[64];ImFormatString(b,64,format,*p_value);ImGui::RenderTextClipped(ImVec2(bb.Min.x,bb.Max.y+s.ItemInnerSpacing.y),ImVec2(bb.Max.x,bb.Max.y+s.ItemInnerSpacing.y+g.FontSize),b,0,0,ImVec2(0.5f,0));}
    if(label&&label[0])ImGui::RenderText(ImVec2(bb.Min.x,bb.Min.y-g.FontSize-s.ItemInnerSpacing.y),label);
    return vc;
}

bool MultiTouchDragArea(const char* id, ImVec2* offset, const ImVec2& size) {
    ImGuiWindow* w = ImGui::GetCurrentWindow(); if(w->SkipItems) return false;
    ImRect bb(w->DC.CursorPos, ImVec2(w->DC.CursorPos.x+size.x, w->DC.CursorPos.y+size.y));
    ImGui::ItemSize(size); if(!ImGui::ItemAdd(bb, w->GetID(id))) return false;
    bool vc=false;
    if(ImGui::IsItemActive()&&ImGui::IsMouseDragging(0)){offset->x+=ImGui::GetIO().MouseDelta.x;offset->y+=ImGui::GetIO().MouseDelta.y;vc=true;}
    ImGui::RenderFrame(bb.Min, bb.Max, ImGui::GetColorU32(ImGuiCol_FrameBg), true, ImGui::GetStyle().FrameRounding);
    return vc;
}

bool MultiTouchZoomArea(const char* id, float* scale, const ImVec2& size) {
    ImGuiWindow* w = ImGui::GetCurrentWindow(); if(w->SkipItems) return false;
    ImRect bb(w->DC.CursorPos, ImVec2(w->DC.CursorPos.x+size.x, w->DC.CursorPos.y+size.y));
    ImGui::ItemSize(size); if(!ImGui::ItemAdd(bb, w->GetID(id))) return false;
    bool vc=false;
    const auto& tp=MultiTouchManager::GetInstance().GetActiveTouchPoints();
    if(tp.size()>=2){static float ld=0;static bool hl=false;float dx=tp[1].position.x-tp[0].position.x,dy=tp[1].position.y-tp[0].position.y,d=sqrtf(dx*dx+dy*dy);if(hl&&ld>0){float r=d/ld;if(r>0.5f&&r<2){*scale*=r;vc=true;}}ld=d;hl=true;}
    else{static bool hl=false;hl=false;}
    ImGui::RenderFrame(bb.Min,bb.Max,ImGui::GetColorU32(ImGuiCol_FrameBg),true,ImGui::GetStyle().FrameRounding);
    char b[32];ImFormatString(b,32,"%.2fx",*scale);ImGui::RenderTextClipped(bb.Min,bb.Max,b,0,0,ImVec2(0.5f,0.5f));
    return vc;
}

} // Widgets
} // ImGuiMultiTouch