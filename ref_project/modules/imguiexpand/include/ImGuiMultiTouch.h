#pragma once
#include "imgui.h"
#include <vector>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <functional>

#include <android/input.h>

namespace ImGuiMultiTouch {

constexpr int MAX_TOUCH_POINTS = 10;

// ==================== 触摸事件类型 ====================
enum class TouchEventType {
    Down,
    Move,
    Up,
    Cancel
};

// ==================== 触摸事件结构 ====================
struct TouchEvent {
    TouchEventType type;
    int pointerId;
    float x;
    float y;
    float pressure;
    uint64_t timestamp;

    TouchEvent() : type(TouchEventType::Down), pointerId(-1), x(0), y(0),
                   pressure(0), timestamp(0) {}
    TouchEvent(TouchEventType t, int id, float _x, float _y, float p, uint64_t ts)
        : type(t), pointerId(id), x(_x), y(_y), pressure(p), timestamp(ts) {}
};

// ==================== 触控点数据（只读快照，供渲染/兼容代码用） ====================
struct TouchPoint {
    int id;
    ImVec2 position;
    ImVec2 delta;
    bool isDown;
    float pressure;
    uint64_t lastEventTime;
    bool stale;
    bool pendingRemove;

    TouchPoint() : id(-1), position(0,0), delta(0,0), isDown(false),
                   pressure(0.0f), lastEventTime(0), stale(false), pendingRemove(false) {}
};

// ==================== 事件监听器接口 ====================
// 由 MultiTouchManager 在处理事件时直接调用
// 返回 true = 消费事件，后续监听器不再收到
class ITouchEventListener {
public:
    virtual ~ITouchEventListener() = default;

    virtual bool OnTouchDown(int touchId, const ImVec2& position, float pressure) = 0;
    virtual bool OnTouchMove(int touchId, const ImVec2& position, const ImVec2& delta, float pressure) = 0;
    virtual bool OnTouchUp(int touchId, const ImVec2& lastPosition) = 0;
    virtual void OnTouchCancel() = 0;
    virtual void OnTouchStale(int touchId) = 0;

    // 优先级，值越小越先收到事件
    virtual int GetPriority() const { return 100; }
};

// ==================== 多点触控管理器 ====================
class MultiTouchManager {
public:
    static MultiTouchManager& GetInstance();

    void Init();

    // ===== 线程安全事件入队（JNI 回调线程调用） =====
    void EnqueueDown(int pointerId, float x, float y, float pressure = 1.0f);
    void EnqueueMove(int pointerId, float x, float y, float pressure = 1.0f);
    void EnqueueUp(int pointerId);
    void EnqueueCancel();

    // ===== 主线程每帧调用 =====
    void NewFrame();

    // ===== 监听器管理 =====
    void AddListener(ITouchEventListener* listener);
    void RemoveListener(ITouchEventListener* listener);

    // ===== 只读查询（供渲染和兼容代码使用） =====
    const std::vector<TouchPoint>& GetActiveTouchPoints() const;
    bool IsTouchActive(int pointerId) const;
    const TouchPoint* GetTouchPoint(int pointerId) const;

    // ===== 兼容旧接口（内部转为入队） =====
    void AddTouchEvent(int pointerId, float x, float y, bool isDown, float pressure = 1.0f);
    void UpdateTouchPosition(int pointerId, float x, float y, float pressure = 1.0f);
    void RemoveTouchPoint(int pointerId);
    void ClearAllTouchPoints();
    void ForceRemoveAllTouchPoints();

    static uint64_t GetCurrentTimeMs();

private:
    MultiTouchManager() = default;
    ~MultiTouchManager() = default;

    // 触控点状态
    std::vector<TouchPoint> m_ActiveTouchPoints;
    std::unordered_map<int, size_t> m_TouchIdToIndex;

    // 线程安全事件队列
    std::mutex m_EventQueueMutex;
    std::vector<TouchEvent> m_EventQueue;
    std::vector<TouchEvent> m_ProcessingQueue;

    // 事件监听器
    std::vector<ITouchEventListener*> m_Listeners;
    bool m_ListenersDirty = false;

    // 内部方法
    void ProcessEventQueue();
    void CleanupPendingRemove();
    void DetectStaleTouchPoints(uint64_t timeoutMs = 2000);
    void SortListenersIfNeeded();

    void DispatchDown(int touchId, const ImVec2& pos, float pressure);
    void DispatchMove(int touchId, const ImVec2& pos, const ImVec2& delta, float pressure);
    void DispatchUp(int touchId, const ImVec2& lastPos);
    void DispatchCancel();
    void DispatchStale(int touchId);

    TouchPoint* FindTouchPoint(int pointerId);

    void EnqueueEvent(const TouchEvent& event);
};

// 全局函数
void Initialize();
void Shutdown();
void NewFrame();

bool HandleAndroidTouchEvent(const AInputEvent* event);

namespace Widgets {
    bool MultiTouchButton(const char* label, const ImVec2& size = ImVec2(0, 0));
    bool MultiTouchSlider(const char* label, float* value, float min = 0.0f, float max = 1.0f,
                          const char* format = "%.3f", ImGuiSliderFlags flags = 0);
    bool MultiTouchDragFloat(const char* label, float* value, float speed = 1.0f,
                             float min = 0.0f, float max = 0.0f, const char* format = "%.3f",
                             ImGuiSliderFlags flags = 0);
    bool MultiTouchKnob(const char* label, float* p_value, float v_min, float v_max,
                        const char* format = "%.1f", float size = 0.0f);
    bool MultiTouchDragArea(const char* id, ImVec2* offset, const ImVec2& size);
    bool MultiTouchZoomArea(const char* id, float* scale, const ImVec2& size);
}

} // namespace ImGuiMultiTouch


