#pragma once
#include "ImGuiMultiTouch.h"
#include "ImGuiKeyboard.h"
#include "json.hpp"
#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <atomic>
#include <chrono>
#include <thread>
#include <functional>
#include <unordered_map>

using json = nlohmann::json;

namespace TouchUI {

enum class ButtonEventType {
    None, Press, Release, Move, Click
};

struct ButtonEvent {
    ButtonEventType type;
    ImVec2 position;
    int touchId;
    ButtonEvent() : type(ButtonEventType::None), position(0, 0), touchId(-1) {}
    ButtonEvent(ButtonEventType t, ImVec2 pos, int id) : type(t), position(pos), touchId(id) {}
};

extern bool g_DebugMode;

using ButtonPressCallback = std::function<void(const std::string& name, const ImVec2& position, int touchId)>;
using ButtonReleaseCallback = std::function<void(const std::string& name, const ImVec2& position, int touchId)>;
using ButtonMoveCallback = std::function<void(const std::string& name, const ImVec2& position, const ImVec2& delta, int touchId)>;
using ButtonClickCallback = std::function<void(const std::string& name, const ImVec2& position, int touchId)>;

class CircularButton {
public:
    CircularButton(const std::string& name, const ImVec2& position, float radius);
    static std::shared_ptr<CircularButton> LoadFromConfig(const json& config);
    ~CircularButton();

    // ========== 事件驱动入口（由 ButtonManager 分发） ==========
    bool HandleTouchDown(int touchId, const ImVec2& position, float pressure);
    bool HandleTouchMove(int touchId, const ImVec2& position, const ImVec2& delta, float pressure);
    bool HandleTouchUp(int touchId, const ImVec2& lastPosition);
    void HandleTouchCancel();
    void HandleTouchStale(int touchId);

    // 只做渲染，不再轮询
    void Render();

    void SetPosition(const ImVec2& pos);
    void SetRadius(float radius);
    void SetEnabled(bool enabled);
    void SetMovable(bool movable);
    void SetTextSize(float size);
    void SetReturnToOrigin(bool returnToOrigin);
    void SetOriginPosition(const ImVec2& pos);
    void SetBorderThickness(float thickness);
    void SetBorderColor(const ImVec4& color);
    void SetTextColor(const ImVec4& color);
    void SetPressedColor(const ImVec4& color);
    void SetFillColor(const ImVec4& color);
    void SetDebugColor(const ImVec4& color);
    void SetDisabledColor(const ImVec4& color);
    
    const std::string& GetName() const { return m_Name; }
    void SetName(const std::string& name) { m_Name = name; }
    ImVec2 GetPosition() const { return m_Position; }
    float GetRadius() const { return m_Radius; }
    bool IsEnabled() const { return m_Enabled; }
    bool IsMovable() const { return m_Movable; }
    float GetTextSize() const { return m_TextSize; }
    bool IsReturnToOrigin() const { return m_ReturnToOrigin; }
    ImVec2 GetOriginPosition() const { return m_OriginalPosition; }
    float GetBorderThickness() const { return m_BorderThickness; }
    ImVec4 GetBorderColor() const { return m_BorderColor; }
    ImVec4 GetTextColor() const { return m_TextColor; }
    ImVec4 GetPressedColor() const { return m_PressedColor; }
    ImVec4 GetFillColor() const { return m_FillColor; }
    ImVec4 GetDebugColor() const { return m_DebugColor; }
    ImVec4 GetDisabledColor() const { return m_DisabledColor; }

    ButtonEvent GetLastEvent() const { return m_LastEvent; }
    void ClearLastEvent() { m_LastEvent = ButtonEvent(); }
    bool IsCurrentlyPressed() const { return m_IsPressed; }
    int GetActiveTouchId() const { return m_ActiveTouchId; }

    void StopLoopActions();
    void SetLoopStarted(bool started);
    bool IsReleased() const;
    std::atomic<bool>& GetLoopStopFlag() { return m_LoopStopFlag; }
    long GetPressDuration() const;

    void SetOnPress(ButtonPressCallback cb) { m_OnPress = std::move(cb); }
    void SetOnRelease(ButtonReleaseCallback cb) { m_OnRelease = std::move(cb); }
    void SetOnMove(ButtonMoveCallback cb) { m_OnMove = std::move(cb); }
    void SetOnClick(ButtonClickCallback cb) { m_OnClick = std::move(cb); }
    void ClearCallbacks();
    bool HasPressCallback() const { return m_OnPress != nullptr; }
    bool HasReleaseCallback() const { return m_OnRelease != nullptr; }
    bool HasMoveCallback() const { return m_OnMove != nullptr; }
    bool HasClickCallback() const { return m_OnClick != nullptr; }

    void SetDisplayName(const std::string& name) { m_DisplayName = name; }
    const std::string& GetDisplayName() const { return m_DisplayName.empty() ? m_Name : m_DisplayName; }
    void ClearDisplayName() { m_DisplayName.clear(); }
    
    // 绑定按键到按钮（按下键 = 按下按钮，松开键 = 松开按钮）
    void BindKey(int keyCode, int modifiers = 0);
    
    // 绑定按键（通过名称，如 "W", "SPACE", "F1"）
    void BindKeyByName(const std::string& keyName, int modifiers = 0);
    
    // 移除按键绑定
    void UnbindKey(int keyCode);
    void UnbindAllKeys();
    
    // 查询绑定
    bool HasKeyBinding() const { return !m_KeyBindings.empty(); }
    const std::vector<ImGuiKeyboard::KeyBinding>& GetKeyBindings() const { return m_KeyBindings; }
    
    // 按键事件处理（由 ButtonManager 分发）
    bool HandleKeyDown(int keyCode, int modifiers);
    bool HandleKeyUp(int keyCode, int modifiers);
    
    // 获取按键绑定显示文本
    std::string GetKeyBindingText() const;
    
    void SetHideWhenKeyBound(bool hide);
    bool IsHideWhenKeyBound() const { return m_HideWhenKeyBound; }
    bool ShouldRender() const;
    bool ShouldHandleTouch() const;

    void ForceReset();
    void SaveToConfig();
    json ToJson() const;

private:
    std::string m_Name;
    ImVec2 m_Position;
    ImVec2 m_OriginalPosition;
    float m_Radius;
    bool m_Enabled;
    bool m_Movable;
    bool m_ReturnToOrigin;
    bool m_IsPressed;
    bool m_IsDebugSelected;
    bool m_IsDragging;
    int m_ActiveTouchId;
    ImVec2 m_PressPosition;
    ImVec2 m_LastTouchPosition;
    float m_TextSize;
    std::string m_DisplayName;

    float m_BorderThickness;
    ImVec4 m_BorderColor;
    ImVec4 m_TextColor;
    ImVec4 m_PressedColor;
    ImVec4 m_FillColor;
    ImVec4 m_DebugColor;
    ImVec4 m_DisabledColor;

    std::atomic<bool> m_LoopStopFlag{false};
    bool m_LoopStarted = false;
    std::chrono::steady_clock::time_point m_PressTime;
    ButtonEvent m_LastEvent;
    static constexpr float MOVE_THRESHOLD = 30.0f;

    ButtonPressCallback m_OnPress;
    ButtonReleaseCallback m_OnRelease;
    ButtonMoveCallback m_OnMove;
    ButtonClickCallback m_OnClick;

    bool IsPointInside(const ImVec2& point) const;
    float GetDistance(const ImVec2& p1, const ImVec2& p2) const;
    void TriggerPressCallback(const ImVec2& position, int touchId);
    void TriggerReleaseCallback(const ImVec2& position, int touchId);
    void TriggerMoveCallback(const ImVec2& position, const ImVec2& delta, int touchId);
    void TriggerClickCallback(const ImVec2& position, int touchId);
    bool CheckCollisionWithOtherButtons(const ImVec2& proposedPos) const;
    ImVec2 ResolveCollision(const ImVec2& proposedPos, const ImVec2& originalPos);
    ImU32 ColorToU32(const ImVec4& color) const;
    void DoRelease(const ImVec2& releasePos);
    
    std::vector<ImGuiKeyboard::KeyBinding> m_KeyBindings;
    bool m_KeyPressed = false;  // 被按键触发的按下状态
    bool m_HideWhenKeyBound = false;
};

// ==================== ButtonManager 实现 ITouchEventListener ====================
class ButtonManager : public ImGuiMultiTouch::ITouchEventListener, 
                       public ImGuiKeyboard::IKeyEventListener {
public:
    static ButtonManager& GetInstance();

    void SetConfigPath(const std::string& path);
    std::shared_ptr<CircularButton> CreateButton(const std::string& name, const ImVec2& position, float radius);
    void LoadAllButtons();
    
    void SaveAllButtons();
    
    bool OnKeyDown(int keyCode, int scanCode, int modifiers) override;
    bool OnKeyUp(int keyCode, int scanCode, int modifiers) override;
    int GetKeyPriority() const override { return 10; }

    // ★ 不再有 UpdateAll，事件由 MultiTouchManager 推送
    void RenderAll();

    std::shared_ptr<CircularButton> GetButton(const std::string& name);
    void RemoveButton(const std::string& name);
    void Clear();
    void SetDebugSelectedButton(CircularButton* button);
    CircularButton* GetDebugSelectedButton() const { return m_DebugSelectedButton; }

    // ===== ITouchEventListener =====
    bool OnTouchDown(int touchId, const ImVec2& position, float pressure) override;
    bool OnTouchMove(int touchId, const ImVec2& position, const ImVec2& delta, float pressure) override;
    bool OnTouchUp(int touchId, const ImVec2& lastPosition) override;
    void OnTouchCancel() override;
    void OnTouchStale(int touchId) override;
    int GetPriority() const override { return 10; }

    bool TryClaimTouch(int touchId, CircularButton* button);
    void ReleaseTouch(int touchId, CircularButton* button);
    bool IsTouchOwnedBy(int touchId, CircularButton* button) const;
    bool IsTouchClaimed(int touchId) const;
    void ClearAllTouchOwnership();

    void SetGlobalOnPress(ButtonPressCallback cb) { m_GlobalOnPress = std::move(cb); }
    void SetGlobalOnRelease(ButtonReleaseCallback cb) { m_GlobalOnRelease = std::move(cb); }
    void SetGlobalOnMove(ButtonMoveCallback cb) { m_GlobalOnMove = std::move(cb); }
    void SetGlobalOnClick(ButtonClickCallback cb) { m_GlobalOnClick = std::move(cb); }
    void ClearGlobalCallbacks();
    const ButtonPressCallback& GetGlobalOnPress() const { return m_GlobalOnPress; }
    const ButtonReleaseCallback& GetGlobalOnRelease() const { return m_GlobalOnRelease; }
    const ButtonMoveCallback& GetGlobalOnMove() const { return m_GlobalOnMove; }
    const ButtonClickCallback& GetGlobalOnClick() const { return m_GlobalOnClick; }

    void SetRenderEnabled(bool enabled);
    bool IsRenderEnabled() const { return m_RenderEnabled; }
    const std::vector<std::shared_ptr<CircularButton>>& GetAllButtons() const { return m_Buttons; }
    bool IsDebugMode() const { return TouchUI::g_DebugMode; }

private:
    ButtonManager();
    ~ButtonManager();

    std::string m_ConfigPath;
    std::vector<std::shared_ptr<CircularButton>> m_Buttons;
    CircularButton* m_DebugSelectedButton = nullptr;
    bool m_RenderEnabled = false;
    bool m_Registered = false;
    
    bool m_KeyListenerRegistered = false;

    std::unordered_map<int, CircularButton*> m_TouchOwnership;
    ButtonPressCallback m_GlobalOnPress;
    ButtonReleaseCallback m_GlobalOnRelease;
    ButtonMoveCallback m_GlobalOnMove;
    ButtonClickCallback m_GlobalOnClick;

    int FindButtonIndex(const std::string& name) const;
    CircularButton* FindButtonByTouchId(int touchId);
};

} // namespace TouchUI
