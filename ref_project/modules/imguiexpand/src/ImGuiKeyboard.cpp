#include "ImGuiKeyboard.h"
#include <algorithm>
#include <cstring>
#include <android/log.h>

#define LOG_TAG "ImGuiKeyboard"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace ImGuiKeyboard {

// ============================================================================
// 工具
// ============================================================================

uint64_t InputManager::GetCurrentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

InputManager& InputManager::GetInstance() {
    static InputManager instance;
    return instance;
}

void InputManager::Init() {
    m_KeyEventQueue.reserve(32);
    m_KeyProcessingQueue.reserve(32);
    m_MouseEventQueue.reserve(64);
    m_MouseProcessingQueue.reserve(64);
    LOGI("InputManager 初始化完成");
}

// ============================================================================
// 线程安全入队
// ============================================================================

void InputManager::EnqueueKeyDown(int keyCode, int scanCode, int modifiers, int deviceId, int source) {
    std::lock_guard<std::mutex> lock(m_KeyEventMutex);
    m_KeyEventQueue.emplace_back(KeyEventType::Down, keyCode, scanCode, modifiers, deviceId, source, GetCurrentTimeMs());
}

void InputManager::EnqueueKeyUp(int keyCode, int scanCode, int modifiers, int deviceId, int source) {
    std::lock_guard<std::mutex> lock(m_KeyEventMutex);
    m_KeyEventQueue.emplace_back(KeyEventType::Up, keyCode, scanCode, modifiers, deviceId, source, GetCurrentTimeMs());
}

void InputManager::EnqueueMouseMove(float x, float y, int deviceId, int source) {
    std::lock_guard<std::mutex> lock(m_MouseEventMutex);
    MouseEvent e;
    e.type = MouseEventType::Move;
    e.x = x; e.y = y;
    e.deviceId = deviceId; e.source = source;
    e.timestamp = GetCurrentTimeMs();
    m_MouseEventQueue.push_back(e);
}

void InputManager::EnqueueMouseButtonDown(int button, float x, float y, int deviceId, int source) {
    std::lock_guard<std::mutex> lock(m_MouseEventMutex);
    MouseEvent e;
    e.type = MouseEventType::ButtonDown;
    e.button = button; e.x = x; e.y = y;
    e.deviceId = deviceId; e.source = source;
    e.timestamp = GetCurrentTimeMs();
    m_MouseEventQueue.push_back(e);
}

void InputManager::EnqueueMouseButtonUp(int button, float x, float y, int deviceId, int source) {
    std::lock_guard<std::mutex> lock(m_MouseEventMutex);
    MouseEvent e;
    e.type = MouseEventType::ButtonUp;
    e.button = button; e.x = x; e.y = y;
    e.deviceId = deviceId; e.source = source;
    e.timestamp = GetCurrentTimeMs();
    m_MouseEventQueue.push_back(e);
}

void InputManager::EnqueueMouseScroll(float x, float y, float scrollX, float scrollY, int deviceId, int source) {
    std::lock_guard<std::mutex> lock(m_MouseEventMutex);
    MouseEvent e;
    e.type = MouseEventType::Scroll;
    e.x = x; e.y = y;
    e.scrollX = scrollX; e.scrollY = scrollY;
    e.deviceId = deviceId; e.source = source;
    e.timestamp = GetCurrentTimeMs();
    m_MouseEventQueue.push_back(e);
}

// ============================================================================
// 监听器管理
// ============================================================================

void InputManager::AddKeyListener(IKeyEventListener* listener) {
    if (!listener) return;
    for (auto* l : m_KeyListeners) if (l == listener) return;
    m_KeyListeners.push_back(listener);
    m_KeyListenersDirty = true;
}

void InputManager::RemoveKeyListener(IKeyEventListener* listener) {
    m_KeyListeners.erase(
        std::remove(m_KeyListeners.begin(), m_KeyListeners.end(), listener),
        m_KeyListeners.end());
}

void InputManager::AddMouseListener(IMouseEventListener* listener) {
    if (!listener) return;
    for (auto* l : m_MouseListeners) if (l == listener) return;
    m_MouseListeners.push_back(listener);
    m_MouseListenersDirty = true;
}

void InputManager::RemoveMouseListener(IMouseEventListener* listener) {
    m_MouseListeners.erase(
        std::remove(m_MouseListeners.begin(), m_MouseListeners.end(), listener),
        m_MouseListeners.end());
}

void InputManager::SortKeyListenersIfNeeded() {
    if (!m_KeyListenersDirty) return;
    std::stable_sort(m_KeyListeners.begin(), m_KeyListeners.end(),
        [](IKeyEventListener* a, IKeyEventListener* b) {
            return a->GetKeyPriority() < b->GetKeyPriority();
        });
    m_KeyListenersDirty = false;
}

void InputManager::SortMouseListenersIfNeeded() {
    if (!m_MouseListenersDirty) return;
    std::stable_sort(m_MouseListeners.begin(), m_MouseListeners.end(),
        [](IMouseEventListener* a, IMouseEventListener* b) {
            return a->GetMousePriority() < b->GetMousePriority();
        });
    m_MouseListenersDirty = false;
}

// ============================================================================
// 事件分发
// ============================================================================

void InputManager::DispatchKeyDown(int keyCode, int scanCode, int modifiers) {
    SortKeyListenersIfNeeded();
    for (auto* l : m_KeyListeners) {
        if (l->OnKeyDown(keyCode, scanCode, modifiers)) break;
    }
}

void InputManager::DispatchKeyUp(int keyCode, int scanCode, int modifiers) {
    SortKeyListenersIfNeeded();
    for (auto* l : m_KeyListeners) {
        if (l->OnKeyUp(keyCode, scanCode, modifiers)) break;
    }
}

void InputManager::DispatchMouseMove(float x, float y) {
    SortMouseListenersIfNeeded();
    for (auto* l : m_MouseListeners) {
        if (l->OnMouseMove(x, y)) break;
    }
}

void InputManager::DispatchMouseButtonDown(int button, float x, float y) {
    SortMouseListenersIfNeeded();
    for (auto* l : m_MouseListeners) {
        if (l->OnMouseButtonDown(button, x, y)) break;
    }
}

void InputManager::DispatchMouseButtonUp(int button, float x, float y) {
    SortMouseListenersIfNeeded();
    for (auto* l : m_MouseListeners) {
        if (l->OnMouseButtonUp(button, x, y)) break;
    }
}

void InputManager::DispatchMouseScroll(float x, float y, float scrollX, float scrollY) {
    SortMouseListenersIfNeeded();
    for (auto* l : m_MouseListeners) {
        if (l->OnMouseScroll(x, y, scrollX, scrollY)) break;
    }
}

// ============================================================================
// 修饰键更新
// ============================================================================

void InputManager::UpdateModifiers(int keyCode, bool down) {
    int flag = 0;
    switch (keyCode) {
        case AKEYCODE_SHIFT_LEFT:
        case AKEYCODE_SHIFT_RIGHT:
            flag = MOD_SHIFT; break;
        case AKEYCODE_CTRL_LEFT:
        case AKEYCODE_CTRL_RIGHT:
            flag = MOD_CTRL; break;
        case AKEYCODE_ALT_LEFT:
        case AKEYCODE_ALT_RIGHT:
            flag = MOD_ALT; break;
        case AKEYCODE_META_LEFT:
        case AKEYCODE_META_RIGHT:
            flag = MOD_META; break;
        default: return;
    }
    if (down) m_CurrentModifiers |= flag;
    else      m_CurrentModifiers &= ~flag;
}

// ============================================================================
// 处理键盘事件队列
// ============================================================================

void InputManager::ProcessKeyEvents() {
    {
        std::lock_guard<std::mutex> lock(m_KeyEventMutex);
        m_KeyProcessingQueue.swap(m_KeyEventQueue);
    }
    
    for (const auto& event : m_KeyProcessingQueue) {
        switch (event.type) {
            case KeyEventType::Down: {
                UpdateModifiers(event.keyCode, true);
                
                bool wasDown = m_KeyStates[event.keyCode];
                m_KeyStates[event.keyCode] = true;
                if (!wasDown) {
                    m_KeyPressed[event.keyCode] = true;
                }
                
                // ★ 同步更新 ImGuiIO（现在在主线程处理，安全）
                ImGuiKey imguiKey = AndroidKeyToImGuiKey(event.keyCode);
                if (imguiKey != ImGuiKey_None) {
                    ImGui::GetIO().AddKeyEvent(imguiKey, true);
                }
                
                DispatchKeyDown(event.keyCode, event.scanCode, event.modifiers);
                break;
            }
            case KeyEventType::Up: {
                UpdateModifiers(event.keyCode, false);
                
                m_KeyStates[event.keyCode] = false;
                m_KeyReleased[event.keyCode] = true;
                
                // ★ 同步更新 ImGuiIO
                ImGuiKey imguiKey = AndroidKeyToImGuiKey(event.keyCode);
                if (imguiKey != ImGuiKey_None) {
                    ImGui::GetIO().AddKeyEvent(imguiKey, false);
                }
                
                DispatchKeyUp(event.keyCode, event.scanCode, event.modifiers);
                break;
            }
        }
    }
    
    m_KeyProcessingQueue.clear();
}

// ============================================================================
// 处理鼠标事件队列
// ============================================================================

void InputManager::ProcessMouseEvents() {
    {
        std::lock_guard<std::mutex> lock(m_MouseEventMutex);
        m_MouseProcessingQueue.swap(m_MouseEventQueue);
    }
    
    for (const auto& event : m_MouseProcessingQueue) {
        switch (event.type) {
            case MouseEventType::Move:
                m_MousePosition = ImVec2(event.x, event.y);
                DispatchMouseMove(event.x, event.y);
                break;
                
            case MouseEventType::ButtonDown:
                if (event.button >= 0 && event.button < 5)
                    m_MouseButtons[event.button] = true;
                m_MousePosition = ImVec2(event.x, event.y);
                DispatchMouseButtonDown(event.button, event.x, event.y);
                break;
                
            case MouseEventType::ButtonUp:
                if (event.button >= 0 && event.button < 5)
                    m_MouseButtons[event.button] = false;
                m_MousePosition = ImVec2(event.x, event.y);
                DispatchMouseButtonUp(event.button, event.x, event.y);
                break;
                
            case MouseEventType::Scroll:
                m_FrameScrollX += event.scrollX;
                m_FrameScrollY += event.scrollY;
                DispatchMouseScroll(event.x, event.y, event.scrollX, event.scrollY);
                break;
        }
    }
    
    m_MouseProcessingQueue.clear();
}

// ============================================================================
// 更新 ImGui IO（键盘和鼠标事件传递给 ImGui）
// ============================================================================

void InputManager::UpdateImGuiIO() {
    ImGuiIO& io = ImGui::GetIO();
    
    // 修饰键
    io.AddKeyEvent(ImGuiMod_Ctrl,  (m_CurrentModifiers & MOD_CTRL)  != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (m_CurrentModifiers & MOD_SHIFT) != 0);
    io.AddKeyEvent(ImGuiMod_Alt,   (m_CurrentModifiers & MOD_ALT)   != 0);
    io.AddKeyEvent(ImGuiMod_Super, (m_CurrentModifiers & MOD_META)  != 0);
    
    // 鼠标位置和按钮（外接鼠标）
    // 注意：只有当有外接鼠标事件时才更新，避免覆盖触摸事件
    if (m_MousePosition.x != m_LastMousePosition.x || m_MousePosition.y != m_LastMousePosition.y) {
        io.AddMousePosEvent(m_MousePosition.x, m_MousePosition.y);
    }
    
    // 鼠标滚轮
    if (m_FrameScrollX != 0 || m_FrameScrollY != 0) {
        io.AddMouseWheelEvent(m_FrameScrollX, m_FrameScrollY);
    }
}

// ============================================================================
// NewFrame
// ============================================================================

void InputManager::NewFrame() {
    // 清除上一帧的按键/释放标记
    m_KeyPressed.clear();
    m_KeyReleased.clear();
    m_FrameScrollX = 0;
    m_FrameScrollY = 0;
    m_LastMousePosition = m_MousePosition;
    m_MouseDelta = ImVec2(0, 0);
    
    // 处理事件队列
    ProcessKeyEvents();
    ProcessMouseEvents();
    
    // 计算鼠标 delta
    m_MouseDelta = ImVec2(m_MousePosition.x - m_LastMousePosition.x,
                          m_MousePosition.y - m_LastMousePosition.y);
    
    // 更新 ImGui IO
    UpdateImGuiIO();
}

// ============================================================================
// 状态查询
// ============================================================================

bool InputManager::IsKeyDown(int keyCode) const {
    auto it = m_KeyStates.find(keyCode);
    return it != m_KeyStates.end() && it->second;
}

bool InputManager::IsKeyPressed(int keyCode) const {
    auto it = m_KeyPressed.find(keyCode);
    return it != m_KeyPressed.end() && it->second;
}

bool InputManager::IsKeyReleased(int keyCode) const {
    auto it = m_KeyReleased.find(keyCode);
    return it != m_KeyReleased.end() && it->second;
}

bool InputManager::IsMouseButtonDown(int button) const {
    if (button < 0 || button >= 5) return false;
    return m_MouseButtons[button];
}

// ============================================================================
// Android keyCode → ImGuiKey 映射
// ============================================================================

ImGuiKey InputManager::AndroidKeyToImGuiKey(int keyCode) {
    switch (keyCode) {
        case AKEYCODE_TAB:            return ImGuiKey_Tab;
        case AKEYCODE_DPAD_LEFT:      return ImGuiKey_LeftArrow;
        case AKEYCODE_DPAD_RIGHT:     return ImGuiKey_RightArrow;
        case AKEYCODE_DPAD_UP:        return ImGuiKey_UpArrow;
        case AKEYCODE_DPAD_DOWN:      return ImGuiKey_DownArrow;
        case AKEYCODE_PAGE_UP:        return ImGuiKey_PageUp;
        case AKEYCODE_PAGE_DOWN:      return ImGuiKey_PageDown;
        case AKEYCODE_HOME:           return ImGuiKey_Home;
        case AKEYCODE_INSERT:         return ImGuiKey_Insert;
        case AKEYCODE_FORWARD_DEL:    return ImGuiKey_Delete;
        case AKEYCODE_DEL:            return ImGuiKey_Backspace;
        case AKEYCODE_SPACE:          return ImGuiKey_Space;
        case AKEYCODE_ENTER:          return ImGuiKey_Enter;
        case AKEYCODE_ESCAPE:         return ImGuiKey_Escape;
        case AKEYCODE_APOSTROPHE:     return ImGuiKey_Apostrophe;
        case AKEYCODE_COMMA:          return ImGuiKey_Comma;
        case AKEYCODE_MINUS:          return ImGuiKey_Minus;
        case AKEYCODE_PERIOD:         return ImGuiKey_Period;
        case AKEYCODE_SLASH:          return ImGuiKey_Slash;
        case AKEYCODE_SEMICOLON:      return ImGuiKey_Semicolon;
        case AKEYCODE_EQUALS:         return ImGuiKey_Equal;
        case AKEYCODE_LEFT_BRACKET:   return ImGuiKey_LeftBracket;
        case AKEYCODE_BACKSLASH:      return ImGuiKey_Backslash;
        case AKEYCODE_RIGHT_BRACKET:  return ImGuiKey_RightBracket;
        case AKEYCODE_GRAVE:          return ImGuiKey_GraveAccent;
        case AKEYCODE_CAPS_LOCK:      return ImGuiKey_CapsLock;
        case AKEYCODE_SCROLL_LOCK:    return ImGuiKey_ScrollLock;
        case AKEYCODE_NUM_LOCK:       return ImGuiKey_NumLock;
        case AKEYCODE_SHIFT_LEFT:     return ImGuiKey_LeftShift;
        case AKEYCODE_SHIFT_RIGHT:    return ImGuiKey_RightShift;
        case AKEYCODE_CTRL_LEFT:      return ImGuiKey_LeftCtrl;
        case AKEYCODE_CTRL_RIGHT:     return ImGuiKey_RightCtrl;
        case AKEYCODE_ALT_LEFT:       return ImGuiKey_LeftAlt;
        case AKEYCODE_ALT_RIGHT:      return ImGuiKey_RightAlt;
        case AKEYCODE_META_LEFT:      return ImGuiKey_LeftSuper;
        case AKEYCODE_META_RIGHT:     return ImGuiKey_RightSuper;
        default:
            if (keyCode >= AKEYCODE_0 && keyCode <= AKEYCODE_9)
                return (ImGuiKey)(ImGuiKey_0 + (keyCode - AKEYCODE_0));
            if (keyCode >= AKEYCODE_A && keyCode <= AKEYCODE_Z)
                return (ImGuiKey)(ImGuiKey_A + (keyCode - AKEYCODE_A));
            if (keyCode >= AKEYCODE_F1 && keyCode <= AKEYCODE_F12)
                return (ImGuiKey)(ImGuiKey_F1 + (keyCode - AKEYCODE_F1));
            if (keyCode >= AKEYCODE_NUMPAD_0 && keyCode <= AKEYCODE_NUMPAD_9)
                return (ImGuiKey)(ImGuiKey_Keypad0 + (keyCode - AKEYCODE_NUMPAD_0));
            return ImGuiKey_None;
    }
}

// ============================================================================
// 按键名称
// ============================================================================

const char* InputManager::GetKeyName(int keyCode) {
    switch (keyCode) {
        case AKEYCODE_BACK:          return "BACK";
        case AKEYCODE_DPAD_UP:       return "DPAD_UP";
        case AKEYCODE_DPAD_DOWN:     return "DPAD_DOWN";
        case AKEYCODE_DPAD_LEFT:     return "DPAD_LEFT";
        case AKEYCODE_DPAD_RIGHT:    return "DPAD_RIGHT";
        case AKEYCODE_DPAD_CENTER:   return "DPAD_CENTER";
        case AKEYCODE_TAB:           return "TAB";
        case AKEYCODE_SPACE:         return "SPACE";
        case AKEYCODE_ENTER:         return "ENTER";
        case AKEYCODE_DEL:           return "BACKSPACE";
        case AKEYCODE_ESCAPE:        return "ESC";
        case AKEYCODE_FORWARD_DEL:   return "DELETE";
        case AKEYCODE_INSERT:        return "INSERT";
        case AKEYCODE_CAPS_LOCK:     return "CAPS_LOCK";
        case AKEYCODE_SHIFT_LEFT:    return "L_SHIFT";
        case AKEYCODE_SHIFT_RIGHT:   return "R_SHIFT";
        case AKEYCODE_CTRL_LEFT:     return "L_CTRL";
        case AKEYCODE_CTRL_RIGHT:    return "R_CTRL";
        case AKEYCODE_ALT_LEFT:      return "L_ALT";
        case AKEYCODE_ALT_RIGHT:     return "R_ALT";
        case AKEYCODE_META_LEFT:     return "L_META";
        case AKEYCODE_META_RIGHT:    return "R_META";
        case AKEYCODE_PAGE_UP:       return "PAGE_UP";
        case AKEYCODE_PAGE_DOWN:     return "PAGE_DOWN";
        case AKEYCODE_VOLUME_UP:     return "VOL_UP";
        case AKEYCODE_VOLUME_DOWN:   return "VOL_DOWN";
        case AKEYCODE_VOLUME_MUTE:   return "VOL_MUTE";
        default:
            if (keyCode >= AKEYCODE_0 && keyCode <= AKEYCODE_9) {
                static char buf[2]; buf[0] = '0' + (keyCode - AKEYCODE_0); buf[1] = 0; return buf;
            }
            if (keyCode >= AKEYCODE_A && keyCode <= AKEYCODE_Z) {
                static char buf[2]; buf[0] = 'A' + (keyCode - AKEYCODE_A); buf[1] = 0; return buf;
            }
            if (keyCode >= AKEYCODE_F1 && keyCode <= AKEYCODE_F12) {
                static char buf[4]; snprintf(buf, sizeof(buf), "F%d", keyCode - AKEYCODE_F1 + 1); return buf;
            }
            if (keyCode >= AKEYCODE_NUMPAD_0 && keyCode <= AKEYCODE_NUMPAD_9) {
                static char buf[8]; snprintf(buf, sizeof(buf), "NUM_%d", keyCode - AKEYCODE_NUMPAD_0); return buf;
            }
            static char unk[16]; snprintf(unk, sizeof(unk), "KEY_%d", keyCode); return unk;
    }
}

int InputManager::GetKeyCodeByName(const char* name) {
    if (!name || !name[0]) return AKEYCODE_UNKNOWN;
    
    // 单个字符：字母或数字
    if (name[1] == 0) {
        if (name[0] >= 'A' && name[0] <= 'Z') return AKEYCODE_A + (name[0] - 'A');
        if (name[0] >= 'a' && name[0] <= 'z') return AKEYCODE_A + (name[0] - 'a');
        if (name[0] >= '0' && name[0] <= '9') return AKEYCODE_0 + (name[0] - '0');
    }
    
    if (strcmp(name, "SPACE") == 0)     return AKEYCODE_SPACE;
    if (strcmp(name, "ENTER") == 0)     return AKEYCODE_ENTER;
    if (strcmp(name, "TAB") == 0)       return AKEYCODE_TAB;
    if (strcmp(name, "ESC") == 0)       return AKEYCODE_ESCAPE;
    if (strcmp(name, "BACKSPACE") == 0) return AKEYCODE_DEL;
    if (strcmp(name, "DELETE") == 0)    return AKEYCODE_FORWARD_DEL;
    
    // F1-F12
    if (name[0] == 'F' && name[1] >= '1' && name[1] <= '9') {
        int num = atoi(name + 1);
        if (num >= 1 && num <= 12) return AKEYCODE_F1 + num - 1;
    }
    
    return AKEYCODE_UNKNOWN;
}

// ============================================================================
// 全局函数
// ============================================================================

void InitInput()     { InputManager::GetInstance().Init(); }
void ShutdownInput() { /* nothing to clean up */ }
void InputNewFrame() { InputManager::GetInstance().NewFrame(); }

} // namespace ImGuiKeyboard