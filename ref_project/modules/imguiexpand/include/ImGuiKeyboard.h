#pragma once
#include "imgui.h"
#include <vector>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <string>
#include <chrono>

namespace ImGuiKeyboard {

// ============================================================================
// Android KeyEvent 常量映射 (android/keycodes.h 子集 + 常用键)
// ============================================================================
enum AndroidKeyCode {
    AKEYCODE_UNKNOWN         = 0,
    AKEYCODE_SOFT_LEFT       = 1,
    AKEYCODE_SOFT_RIGHT      = 2,
    AKEYCODE_HOME            = 3,
    AKEYCODE_BACK            = 4,
    AKEYCODE_0               = 7,
    AKEYCODE_1               = 8,
    AKEYCODE_2               = 9,
    AKEYCODE_3               = 10,
    AKEYCODE_4               = 11,
    AKEYCODE_5               = 12,
    AKEYCODE_6               = 13,
    AKEYCODE_7               = 14,
    AKEYCODE_8               = 15,
    AKEYCODE_9               = 16,
    AKEYCODE_STAR            = 17,
    AKEYCODE_POUND           = 18,
    AKEYCODE_DPAD_UP         = 19,
    AKEYCODE_DPAD_DOWN       = 20,
    AKEYCODE_DPAD_LEFT       = 21,
    AKEYCODE_DPAD_RIGHT      = 22,
    AKEYCODE_DPAD_CENTER     = 23,
    AKEYCODE_VOLUME_UP       = 24,
    AKEYCODE_VOLUME_DOWN     = 25,
    AKEYCODE_A               = 29,
    AKEYCODE_B               = 30,
    AKEYCODE_C               = 31,
    AKEYCODE_D               = 32,
    AKEYCODE_E               = 33,
    AKEYCODE_F               = 34,
    AKEYCODE_G               = 35,
    AKEYCODE_H               = 36,
    AKEYCODE_I               = 37,
    AKEYCODE_J               = 38,
    AKEYCODE_K               = 39,
    AKEYCODE_L               = 40,
    AKEYCODE_M               = 41,
    AKEYCODE_N               = 42,
    AKEYCODE_O               = 43,
    AKEYCODE_P               = 44,
    AKEYCODE_Q               = 45,
    AKEYCODE_R               = 46,
    AKEYCODE_S               = 47,
    AKEYCODE_T               = 48,
    AKEYCODE_U               = 49,
    AKEYCODE_V               = 50,
    AKEYCODE_W               = 51,
    AKEYCODE_X               = 52,
    AKEYCODE_Y               = 53,
    AKEYCODE_Z               = 54,
    AKEYCODE_COMMA           = 55,
    AKEYCODE_PERIOD          = 56,
    AKEYCODE_ALT_LEFT        = 57,
    AKEYCODE_ALT_RIGHT       = 58,
    AKEYCODE_SHIFT_LEFT      = 59,
    AKEYCODE_SHIFT_RIGHT     = 60,
    AKEYCODE_TAB             = 61,
    AKEYCODE_SPACE           = 62,
    AKEYCODE_ENTER           = 66,
    AKEYCODE_DEL             = 67,  // Backspace
    AKEYCODE_GRAVE           = 68,  // `
    AKEYCODE_MINUS           = 69,
    AKEYCODE_EQUALS          = 70,
    AKEYCODE_LEFT_BRACKET    = 71,
    AKEYCODE_RIGHT_BRACKET   = 72,
    AKEYCODE_BACKSLASH       = 73,
    AKEYCODE_SEMICOLON       = 74,
    AKEYCODE_APOSTROPHE      = 75,
    AKEYCODE_SLASH           = 76,
    AKEYCODE_ESCAPE          = 111,
    AKEYCODE_FORWARD_DEL     = 112,
    AKEYCODE_CTRL_LEFT       = 113,
    AKEYCODE_CTRL_RIGHT      = 114,
    AKEYCODE_CAPS_LOCK       = 115,
    AKEYCODE_SCROLL_LOCK     = 116,
    AKEYCODE_META_LEFT       = 117,
    AKEYCODE_META_RIGHT      = 118,
    AKEYCODE_INSERT          = 124,
    AKEYCODE_F1              = 131,
    AKEYCODE_F2              = 132,
    AKEYCODE_F3              = 133,
    AKEYCODE_F4              = 134,
    AKEYCODE_F5              = 135,
    AKEYCODE_F6              = 136,
    AKEYCODE_F7              = 137,
    AKEYCODE_F8              = 138,
    AKEYCODE_F9              = 139,
    AKEYCODE_F10             = 140,
    AKEYCODE_F11             = 141,
    AKEYCODE_F12             = 142,
    AKEYCODE_NUM_LOCK        = 143,
    AKEYCODE_NUMPAD_0        = 144,
    AKEYCODE_NUMPAD_1        = 145,
    AKEYCODE_NUMPAD_2        = 146,
    AKEYCODE_NUMPAD_3        = 147,
    AKEYCODE_NUMPAD_4        = 148,
    AKEYCODE_NUMPAD_5        = 149,
    AKEYCODE_NUMPAD_6        = 150,
    AKEYCODE_NUMPAD_7        = 151,
    AKEYCODE_NUMPAD_8        = 152,
    AKEYCODE_NUMPAD_9        = 153,
    AKEYCODE_VOLUME_MUTE     = 164,
    AKEYCODE_PAGE_UP         = 92,
    AKEYCODE_PAGE_DOWN       = 93,
};

// ============================================================================
// 修饰键标志
// ============================================================================
enum KeyModifiers : int {
    MOD_NONE    = 0,
    MOD_SHIFT   = 1 << 0,
    MOD_CTRL    = 1 << 1,
    MOD_ALT     = 1 << 2,
    MOD_META    = 1 << 3,  // Windows/Command 键
};

// ============================================================================
// 键盘事件类型
// ============================================================================
enum class KeyEventType {
    Down,
    Up,
};

// ============================================================================
// 键盘事件结构
// ============================================================================
struct KeyEvent {
    KeyEventType type;
    int keyCode;       // Android keyCode
    int scanCode;      // 硬件扫描码
    int modifiers;     // KeyModifiers 组合
    int deviceId;      // 设备ID（区分不同键盘）
    int source;        // 输入源 (InputDevice.SOURCE_*)
    uint64_t timestamp;

    KeyEvent()
        : type(KeyEventType::Down), keyCode(0), scanCode(0),
          modifiers(0), deviceId(0), source(0), timestamp(0) {}

    KeyEvent(KeyEventType t, int kc, int sc, int mod, int devId, int src, uint64_t ts)
        : type(t), keyCode(kc), scanCode(sc), modifiers(mod),
          deviceId(devId), source(src), timestamp(ts) {}
};

// ============================================================================
// 鼠标事件类型
// ============================================================================
enum class MouseEventType {
    Move,
    ButtonDown,
    ButtonUp,
    Scroll,
};

// ============================================================================
// 鼠标按钮
// ============================================================================
enum MouseButton : int {
    MOUSE_BUTTON_LEFT    = 0,
    MOUSE_BUTTON_RIGHT   = 1,
    MOUSE_BUTTON_MIDDLE  = 2,
    MOUSE_BUTTON_BACK    = 3,
    MOUSE_BUTTON_FORWARD = 4,
};

// ============================================================================
// 鼠标事件结构
// ============================================================================
struct MouseEvent {
    MouseEventType type;
    float x, y;            // 位置
    float scrollX, scrollY; // 滚轮增量
    int button;             // MouseButton
    int deviceId;
    int source;
    uint64_t timestamp;

    MouseEvent()
        : type(MouseEventType::Move), x(0), y(0), scrollX(0), scrollY(0),
          button(0), deviceId(0), source(0), timestamp(0) {}
};

// ============================================================================
// 按键绑定结构（用于CircularButton绑定）
// ============================================================================
struct KeyBinding {
    int keyCode;
    int modifiers;  // 修饰键要求（0表示忽略修饰键）
    
    KeyBinding() : keyCode(0), modifiers(0) {}
    KeyBinding(int kc, int mod = 0) : keyCode(kc), modifiers(mod) {}
    
    bool Matches(int kc, int mod) const {
        if (keyCode != kc) return false;
        if (modifiers == 0) return true;  // 不要求修饰键
        return (mod & modifiers) == modifiers;
    }
    
    bool operator==(const KeyBinding& other) const {
        return keyCode == other.keyCode && modifiers == other.modifiers;
    }
};

// ============================================================================
// 按键事件监听器接口
// ============================================================================
class IKeyEventListener {
public:
    virtual ~IKeyEventListener() = default;
    
    // 返回 true = 消费事件
    virtual bool OnKeyDown(int keyCode, int scanCode, int modifiers) = 0;
    virtual bool OnKeyUp(int keyCode, int scanCode, int modifiers) = 0;
    
    virtual int GetKeyPriority() const { return 100; }
};

// ============================================================================
// 鼠标事件监听器接口
// ============================================================================
class IMouseEventListener {
public:
    virtual ~IMouseEventListener() = default;
    
    virtual bool OnMouseMove(float x, float y) { return false; }
    virtual bool OnMouseButtonDown(int button, float x, float y) { return false; }
    virtual bool OnMouseButtonUp(int button, float x, float y) { return false; }
    virtual bool OnMouseScroll(float x, float y, float scrollX, float scrollY) { return false; }
    
    virtual int GetMousePriority() const { return 100; }
};

// ============================================================================
// 键盘鼠标输入管理器（单例）
// ============================================================================
class InputManager {
public:
    static InputManager& GetInstance();
    
    void Init();
    
    // ===== 线程安全事件入队（JNI回调线程调用） =====
    void EnqueueKeyDown(int keyCode, int scanCode, int modifiers, int deviceId, int source);
    void EnqueueKeyUp(int keyCode, int scanCode, int modifiers, int deviceId, int source);
    void EnqueueMouseMove(float x, float y, int deviceId, int source);
    void EnqueueMouseButtonDown(int button, float x, float y, int deviceId, int source);
    void EnqueueMouseButtonUp(int button, float x, float y, int deviceId, int source);
    void EnqueueMouseScroll(float x, float y, float scrollX, float scrollY, int deviceId, int source);
    
    // ===== 主线程每帧调用 =====
    void NewFrame();
    
    // ===== 监听器管理 =====
    void AddKeyListener(IKeyEventListener* listener);
    void RemoveKeyListener(IKeyEventListener* listener);
    void AddMouseListener(IMouseEventListener* listener);
    void RemoveMouseListener(IMouseEventListener* listener);
    
    // ===== 按键状态查询 =====
    bool IsKeyDown(int keyCode) const;
    bool IsKeyPressed(int keyCode) const;   // 本帧刚按下
    bool IsKeyReleased(int keyCode) const;  // 本帧刚释放
    int  GetCurrentModifiers() const { return m_CurrentModifiers; }
    
    // ===== 鼠标状态查询 =====
    ImVec2 GetMousePosition() const { return m_MousePosition; }
    ImVec2 GetMouseDelta() const { return m_MouseDelta; }
    bool IsMouseButtonDown(int button) const;
    float GetScrollX() const { return m_FrameScrollX; }
    float GetScrollY() const { return m_FrameScrollY; }
    
    // ===== 按键名称工具 =====
    static const char* GetKeyName(int keyCode);
    static int GetKeyCodeByName(const char* name);
    
    static uint64_t GetCurrentTimeMs();
    
    // Android keyCode → ImGuiKey 映射
    static ImGuiKey AndroidKeyToImGuiKey(int keyCode);

private:
    InputManager() = default;
    ~InputManager() = default;
    
    // 键盘事件队列
    std::mutex m_KeyEventMutex;
    std::vector<KeyEvent> m_KeyEventQueue;
    std::vector<KeyEvent> m_KeyProcessingQueue;
    
    // 鼠标事件队列
    std::mutex m_MouseEventMutex;
    std::vector<MouseEvent> m_MouseEventQueue;
    std::vector<MouseEvent> m_MouseProcessingQueue;
    
    // 按键状态
    std::unordered_map<int, bool> m_KeyStates;       // 当前按下状态
    std::unordered_map<int, bool> m_KeyPressed;      // 本帧刚按下
    std::unordered_map<int, bool> m_KeyReleased;     // 本帧刚释放
    int m_CurrentModifiers = 0;
    
    // 鼠标状态
    ImVec2 m_MousePosition = {0, 0};
    ImVec2 m_LastMousePosition = {0, 0};
    ImVec2 m_MouseDelta = {0, 0};
    bool m_MouseButtons[5] = {false};
    float m_FrameScrollX = 0;
    float m_FrameScrollY = 0;
    
    // 监听器
    std::vector<IKeyEventListener*> m_KeyListeners;
    std::vector<IMouseEventListener*> m_MouseListeners;
    bool m_KeyListenersDirty = false;
    bool m_MouseListenersDirty = false;
    
    // 内部方法
    void ProcessKeyEvents();
    void ProcessMouseEvents();
    void UpdateImGuiIO();
    void UpdateModifiers(int keyCode, bool down);
    void SortKeyListenersIfNeeded();
    void SortMouseListenersIfNeeded();
    
    void DispatchKeyDown(int keyCode, int scanCode, int modifiers);
    void DispatchKeyUp(int keyCode, int scanCode, int modifiers);
    void DispatchMouseMove(float x, float y);
    void DispatchMouseButtonDown(int button, float x, float y);
    void DispatchMouseButtonUp(int button, float x, float y);
    void DispatchMouseScroll(float x, float y, float scrollX, float scrollY);
    
};

// ============================================================================
// 全局便捷函数
// ============================================================================
void InitInput();
void ShutdownInput();
void InputNewFrame();

} // namespace ImGuiKeyboard