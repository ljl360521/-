#include "CircularButton.h"
#include "imgui.h"
#include <cmath>
#include "logger.h"

#include <sstream>
#include <iomanip>
#include "imguiSimpleToast.hpp"
#include "ImGuiDraw.h"
#include <unordered_set>
// CircularButton.cpp 顶部或合适位置
#include "ImGuiDraw.h"  // 为了访问 g_JavaVM, getJNIEnv, g_DexClassLoader

static void UpdateVolumeKeyIntercept() {
    // 检查是否有任何按钮绑定了音量键
    bool needIntercept = false;
    auto& mgr = TouchUI::ButtonManager::GetInstance();
    for (const auto& btn : mgr.GetAllButtons()) {
        if (!btn) continue;
        for (const auto& binding : btn->GetKeyBindings()) {
            if (binding.keyCode == 24 ||   // VOLUME_UP
                binding.keyCode == 25 ||   // VOLUME_DOWN
                binding.keyCode == 164) {  // VOLUME_MUTE
                needIntercept = true;
                break;
            }
        }
        if (needIntercept) break;
    }
    
    // 通过 JNI 调用 Java 设置标志
    JNIEnv* env = getJNIEnv();
    if (!env || !g_DexClassLoader) return;
    
    jclass dexClass = env->FindClass("dalvik/system/DexClassLoader");
    if (!dexClass) { env->ExceptionClear(); return; }
    
    jmethodID loadClass = env->GetMethodID(dexClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    if (!loadClass) { env->ExceptionClear(); env->DeleteLocalRef(dexClass); return; }
    
    jstring clsName = env->NewStringUTF("com.example.imgui.GLES3JNIView");
    jclass viewClass = (jclass)env->CallObjectMethod(g_DexClassLoader, loadClass, clsName);
    env->DeleteLocalRef(clsName);
    env->DeleteLocalRef(dexClass);
    
    if (!viewClass) { env->ExceptionClear(); return; }
    
    jmethodID setMethod = env->GetStaticMethodID(viewClass, "setInterceptVolumeKeys", "(Z)V");
    if (setMethod) {
        env->CallStaticVoidMethod(viewClass, setMethod, needIntercept ? JNI_TRUE : JNI_FALSE);
    }
    
    env->DeleteLocalRef(viewClass);
    if (env->ExceptionCheck()) env->ExceptionClear();
}
namespace TouchUI {

bool g_DebugMode = false;

CircularButton::CircularButton(const std::string& name, const ImVec2& position, float radius)
    : m_Name(name), m_Position(position), m_OriginalPosition(position), m_Radius(radius)
    , m_Enabled(true), m_Movable(false), m_ReturnToOrigin(false)
    , m_IsPressed(false), m_IsDebugSelected(false), m_IsDragging(false)
    , m_ActiveTouchId(-1), m_PressPosition(0,0), m_LastTouchPosition(0,0)
    , m_TextSize(1.0f)
    , m_BorderThickness(3.0f)
    , m_BorderColor(1,1,1,1), m_TextColor(1,1,1,1)
    , m_PressedColor(0.9f,0.9f,0.9f,1), m_FillColor(0,0,0,0)
    , m_DebugColor(1,1,0,1), m_DisabledColor(1,0,0,1)
    , m_LoopStopFlag(false), m_LoopStarted(false)
    , m_PressTime(std::chrono::steady_clock::now())
    , m_OnPress(nullptr), m_OnRelease(nullptr), m_OnMove(nullptr), m_OnClick(nullptr)
    , m_KeyPressed(false), m_HideWhenKeyBound(false)
    
{}

std::shared_ptr<CircularButton> CircularButton::LoadFromConfig(const json& config) {
    std::string name = config["name"];
    ImVec2 pos; pos.x = config["position"]["x"]; pos.y = config["position"]["y"];
    float radius = config["radius"];
    auto btn = std::make_shared<CircularButton>(name, pos, radius);
    if (config.contains("enabled")) btn->m_Enabled = config["enabled"];
    if (config.contains("movable")) btn->m_Movable = config["movable"];
    if (config.contains("textSize")) btn->m_TextSize = config["textSize"];
    if (config.contains("returnToOrigin")) btn->m_ReturnToOrigin = config["returnToOrigin"];
    if (config.contains("originPosition")) {
        btn->m_OriginalPosition.x = config["originPosition"]["x"];
        btn->m_OriginalPosition.y = config["originPosition"]["y"];
    }
    if (config.contains("borderThickness")) btn->m_BorderThickness = config["borderThickness"];
    if (config.contains("hideWhenKeyBound")) btn->m_HideWhenKeyBound = config["hideWhenKeyBound"];
    auto lc = [](const json& j, const char* k, ImVec4& o) {
        if (j.contains(k)) { o.x=j[k]["r"]; o.y=j[k]["g"]; o.z=j[k]["b"]; o.w=j[k]["a"]; }
    };
    lc(config,"borderColor",btn->m_BorderColor); lc(config,"textColor",btn->m_TextColor);
    lc(config,"pressedColor",btn->m_PressedColor); lc(config,"fillColor",btn->m_FillColor);
    lc(config,"debugColor",btn->m_DebugColor); lc(config,"disabledColor",btn->m_DisabledColor);
    if (config.contains("keyBindings") && config["keyBindings"].is_array()) {
        for (const auto& kb : config["keyBindings"]) {
            int kc = kb["keyCode"];
            int mod = kb.value("modifiers", 0);
            btn->m_KeyBindings.emplace_back(kc, mod);
        }
    }
    return btn;
}

CircularButton::~CircularButton() {}

void CircularButton::SetHideWhenKeyBound(bool hide) { 
    m_HideWhenKeyBound = hide; 
    SaveToConfig(); 
}

bool CircularButton::ShouldRender() const {
    if (g_DebugMode) return true;  // 调试模式下始终显示
    if (m_HideWhenKeyBound && !m_KeyBindings.empty()) return false;
    return true;
}

bool CircularButton::ShouldHandleTouch() const {
    if (g_DebugMode) return true;
    if (m_HideWhenKeyBound && !m_KeyBindings.empty()) return false;
    return true;
}

bool CircularButton::IsPointInside(const ImVec2& p) const {
    float dx=p.x-m_Position.x, dy=p.y-m_Position.y;
    return dx*dx+dy*dy <= m_Radius*m_Radius;
}


float CircularButton::GetDistance(const ImVec2& a, const ImVec2& b) const {
    float dx=b.x-a.x, dy=b.y-a.y; return std::sqrt(dx*dx+dy*dy);
}

ImU32 CircularButton::ColorToU32(const ImVec4& c) const {
    return IM_COL32((int)(c.x*255),(int)(c.y*255),(int)(c.z*255),(int)(c.w*255));
}

long CircularButton::GetPressDuration() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now()-m_PressTime).count();
}

void CircularButton::StopLoopActions() { m_LoopStopFlag.store(true); }
void CircularButton::SetLoopStarted(bool s) { m_LoopStarted = s; }
bool CircularButton::IsReleased() const { return !m_IsPressed; }
void CircularButton::ClearCallbacks() { m_OnPress=nullptr;m_OnRelease=nullptr;m_OnMove=nullptr;m_OnClick=nullptr; }

void CircularButton::TriggerPressCallback(const ImVec2& p, int id) {
    if (m_OnPress) m_OnPress(m_Name,p,id);
    auto& g=ButtonManager::GetInstance().GetGlobalOnPress(); if(g) g(m_Name,p,id);
}
void CircularButton::TriggerReleaseCallback(const ImVec2& p, int id) {
    if (m_OnRelease) m_OnRelease(m_Name,p,id);
    auto& g=ButtonManager::GetInstance().GetGlobalOnRelease(); if(g) g(m_Name,p,id);
}
void CircularButton::TriggerMoveCallback(const ImVec2& p, const ImVec2& d, int id) {
    if (m_OnMove) m_OnMove(m_Name,p,d,id);
    auto& g=ButtonManager::GetInstance().GetGlobalOnMove(); if(g) g(m_Name,p,d,id);
}
void CircularButton::TriggerClickCallback(const ImVec2& p, int id) {
    if (m_OnClick) m_OnClick(m_Name,p,id);
    auto& g=ButtonManager::GetInstance().GetGlobalOnClick(); if(g) g(m_Name,p,id);
}

bool CircularButton::CheckCollisionWithOtherButtons(const ImVec2& pp) const {
    for (auto& o : ButtonManager::GetInstance().GetAllButtons()) {
        if (o.get()==this) continue;
        float dx=pp.x-o->GetPosition().x, dy=pp.y-o->GetPosition().y;
        if (std::sqrt(dx*dx+dy*dy) < m_Radius+o->GetRadius()) return true;
    }
    return false;
}

ImVec2 CircularButton::ResolveCollision(const ImVec2& pp, const ImVec2& op) {
    ImVec2 r=pp;
    for (auto& o : ButtonManager::GetInstance().GetAllButtons()) {
        if (o.get()==this) continue;
        ImVec2 opos=o->GetPosition(); float md=m_Radius+o->GetRadius();
        float dx=r.x-opos.x, dy=r.y-opos.y, d=std::sqrt(dx*dx+dy*dy);
        if (d<md && d>0.001f) { r.x+=(dx/d)*(md-d); r.y+=(dy/d)*(md-d); }
    }
    return r;
}

// ==================== 内部释放 ====================

void CircularButton::DoRelease(const ImVec2& releasePos) {
    if (!m_IsPressed) return;

    if (!m_IsDragging && IsPointInside(m_PressPosition)) {
        m_LastEvent = ButtonEvent(ButtonEventType::Click, m_PressPosition, m_ActiveTouchId);
        TriggerClickCallback(m_PressPosition, m_ActiveTouchId);
    } else {
        m_LastEvent = ButtonEvent(ButtonEventType::Release, releasePos, m_ActiveTouchId);
    }

    TriggerReleaseCallback(releasePos, m_ActiveTouchId);
    m_LoopStopFlag.store(true);

    if (m_Movable && m_ReturnToOrigin) m_Position = m_OriginalPosition;
    else if (m_Movable && m_IsDragging) SaveToConfig();

    ButtonManager::GetInstance().ReleaseTouch(m_ActiveTouchId, this);
    m_IsPressed = false;
    m_IsDragging = false;
    m_ActiveTouchId = -1;
}

// ==================== 事件驱动入口 ====================

bool CircularButton::HandleTouchDown(int touchId, const ImVec2& position, float pressure) {
    if (!ShouldHandleTouch()) return false;  // ← 加这一行
    if (m_IsPressed) return false;
    if (!g_DebugMode && !m_Enabled) return false;
   
    
    if (!g_DebugMode && !m_Enabled) return false;
    if (!IsPointInside(position)) return false;
    if (!ButtonManager::GetInstance().TryClaimTouch(touchId, this)) return false;

    m_IsPressed = true;
    m_ActiveTouchId = touchId;
    m_PressPosition = position;
    m_LastTouchPosition = position;
    m_PressTime = std::chrono::steady_clock::now();
    m_IsDragging = false;
    m_LoopStopFlag.store(false);
    m_LoopStarted = false;

    if (g_DebugMode) {
        m_OriginalPosition = m_Position;
        auto* prev = ButtonManager::GetInstance().GetDebugSelectedButton();
        if (prev && prev!=this) prev->m_IsDebugSelected = false;
        m_IsDebugSelected = true;
        ButtonManager::GetInstance().SetDebugSelectedButton(this);
    } else {
        
    }

    m_LastEvent = ButtonEvent(ButtonEventType::Press, position, touchId);
    if (!g_DebugMode) TriggerPressCallback(position, touchId);

    return true;
}

bool CircularButton::HandleTouchMove(int touchId, const ImVec2& position, const ImVec2& delta, float pressure) {
    if (!m_IsPressed || m_ActiveTouchId != touchId) return false;

    if (g_DebugMode) {
        if (!m_IsDragging && GetDistance(m_PressPosition, position) >= MOVE_THRESHOLD)
            m_IsDragging = true;
        if (m_IsDragging) {
            ImVec2 np(m_OriginalPosition.x + position.x - m_PressPosition.x,
                      m_OriginalPosition.y + position.y - m_PressPosition.y);
            if (CheckCollisionWithOtherButtons(np)) np = ResolveCollision(np, m_OriginalPosition);
            np.x = std::max(m_Radius, std::min(np.x, 屏宽 - m_Radius));
            np.y = std::max(m_Radius, std::min(np.y, 屏高 - m_Radius));
            m_Position = np;
        }
    } else if (m_Movable) {
        if (!m_IsDragging && GetDistance(m_PressPosition, position) >= MOVE_THRESHOLD)
            m_IsDragging = true;
        if (m_IsDragging) {
            m_Position.x += delta.x;
            m_Position.y += delta.y;
            TriggerMoveCallback(position, delta, touchId);
            if (!m_ReturnToOrigin) SaveToConfig();
        }
    } else {
        m_LastEvent = ButtonEvent(ButtonEventType::Move, position, touchId);
        ImVec2 d(position.x - m_LastTouchPosition.x, position.y - m_LastTouchPosition.y);
        m_LastTouchPosition = position;
        TriggerMoveCallback(position, d, touchId);
    }
    return true;
}

bool CircularButton::HandleTouchUp(int touchId, const ImVec2& lastPosition) {
    if (!m_IsPressed || m_ActiveTouchId != touchId) return false;

    if (g_DebugMode) {
        if (m_IsDragging) { m_OriginalPosition = m_Position; SaveToConfig(); }
        ButtonManager::GetInstance().ReleaseTouch(m_ActiveTouchId, this);
        m_IsPressed = false; m_IsDragging = false; m_ActiveTouchId = -1;
    } else {
        DoRelease(lastPosition);
    }
    return true;
}

void CircularButton::HandleTouchCancel() {
    if (!m_IsPressed) return;
    if (!g_DebugMode) TriggerReleaseCallback(m_Position, m_ActiveTouchId);
    m_LoopStopFlag.store(true);
    if (m_Movable && m_ReturnToOrigin) m_Position = m_OriginalPosition;
    ButtonManager::GetInstance().ReleaseTouch(m_ActiveTouchId, this);
    m_IsPressed = false; m_IsDragging = false; m_ActiveTouchId = -1;
}

void CircularButton::HandleTouchStale(int touchId) {
    if (!m_IsPressed || m_ActiveTouchId != touchId) return;
    HandleTouchUp(touchId, m_Position);
}

void CircularButton::ForceReset() {
    if (m_IsPressed) ButtonManager::GetInstance().ReleaseTouch(m_ActiveTouchId, this);
    m_IsPressed=false; m_IsDragging=false; m_ActiveTouchId=-1;
    m_LoopStopFlag.store(true); m_LoopStarted=false;
    m_KeyPressed = false;
}

void CircularButton::Render() {
    if (!ShouldRender()) return;   // ← 加这一行
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (!g_DebugMode && !m_Enabled) return;
    

    ImVec4 bc=m_BorderColor, tc=m_TextColor, fc=m_FillColor;
    if (g_DebugMode) {
        if (m_IsDebugSelected) { bc=m_DebugColor; tc=m_DebugColor; }
        else if (!m_Enabled) { bc=m_DisabledColor; tc=m_DisabledColor; }
    } else if (m_IsPressed) { bc=m_PressedColor; tc=m_PressedColor; }

    if (fc.w>0) dl->AddCircleFilled(m_Position, m_Radius, ColorToU32(fc), 32);
    dl->AddCircle(m_Position, m_Radius, ColorToU32(bc), 32, m_BorderThickness);

    ImFont* f=ImGui::GetFont(); float os=f->Scale; f->Scale=m_TextSize;
    ImGui::PushFont(f);
    const std::string& t = m_DisplayName.empty() ? m_Name : m_DisplayName;
    ImVec2 ts=ImGui::CalcTextSize(t.c_str());
    dl->AddText(ImVec2(m_Position.x-ts.x*0.5f, m_Position.y-ts.y*0.5f), ColorToU32(tc), t.c_str());
    ImGui::PopFont(); f->Scale=os;

    if (g_DebugMode && !m_Enabled) {
        float cs=m_Radius*0.6f; ImU32 dc=ColorToU32(m_DisabledColor);
        dl->AddLine({m_Position.x-cs,m_Position.y-cs},{m_Position.x+cs,m_Position.y+cs},dc,m_BorderThickness);
        dl->AddLine({m_Position.x-cs,m_Position.y+cs},{m_Position.x+cs,m_Position.y-cs},dc,m_BorderThickness);
    }
    
    if (g_DebugMode && !m_KeyBindings.empty()) {
        std::string keyText = GetKeyBindingText();
        ImVec2 keyTextSize = ImGui::CalcTextSize(keyText.c_str());
        ImVec2 keyTextPos(m_Position.x - keyTextSize.x * 0.5f, 
                           m_Position.y + m_Radius + 4.0f);
        dl->AddText(keyTextPos, IM_COL32(200, 200, 0, 255), keyText.c_str());
    }
}

void CircularButton::SetPosition(const ImVec2& p) { m_Position=p; m_OriginalPosition=p; SaveToConfig(); }
void CircularButton::SetRadius(float r) { m_Radius=r; SaveToConfig(); }
void CircularButton::SetEnabled(bool e) { m_Enabled=e; if(!e) ForceReset(); SaveToConfig(); }
void CircularButton::SetMovable(bool m) { m_Movable=m; SaveToConfig(); }
void CircularButton::SetTextSize(float s) { m_TextSize=s; SaveToConfig(); }
void CircularButton::SetReturnToOrigin(bool r) { m_ReturnToOrigin=r; SaveToConfig(); }
void CircularButton::SetOriginPosition(const ImVec2& p) { m_OriginalPosition=p; SaveToConfig(); }
void CircularButton::SetBorderThickness(float t) { m_BorderThickness=t; SaveToConfig(); }
void CircularButton::SetBorderColor(const ImVec4& c) { m_BorderColor=c; SaveToConfig(); }
void CircularButton::SetTextColor(const ImVec4& c) { m_TextColor=c; SaveToConfig(); }
void CircularButton::SetPressedColor(const ImVec4& c) { m_PressedColor=c; SaveToConfig(); }
void CircularButton::SetFillColor(const ImVec4& c) { m_FillColor=c; SaveToConfig(); }
void CircularButton::SetDebugColor(const ImVec4& c) { m_DebugColor=c; SaveToConfig(); }
void CircularButton::SetDisabledColor(const ImVec4& c) { m_DisabledColor=c; SaveToConfig(); }

json CircularButton::ToJson() const {
    json j;
    j["name"]=m_Name;
    j["position"]["x"]=m_Position.x; j["position"]["y"]=m_Position.y;
    j["radius"]=m_Radius; j["enabled"]=m_Enabled; j["movable"]=m_Movable;
    j["textSize"]=m_TextSize; j["returnToOrigin"]=m_ReturnToOrigin;
    j["originPosition"]["x"]=m_OriginalPosition.x; j["originPosition"]["y"]=m_OriginalPosition.y;
    j["borderThickness"]=m_BorderThickness;
    // 在 keyBindings 序列化之前加
    j["hideWhenKeyBound"] = m_HideWhenKeyBound;
    auto sc=[&](const char* k,const ImVec4& c){j[k]["r"]=c.x;j[k]["g"]=c.y;j[k]["b"]=c.z;j[k]["a"]=c.w;};
    sc("borderColor",m_BorderColor); sc("textColor",m_TextColor);
    sc("pressedColor",m_PressedColor); sc("fillColor",m_FillColor);
    sc("debugColor",m_DebugColor); sc("disabledColor",m_DisabledColor);
    
    if (!m_KeyBindings.empty()) {
        j["keyBindings"] = json::array();
        for (const auto& b : m_KeyBindings) {
            json kb;
            kb["keyCode"] = b.keyCode;
            kb["modifiers"] = b.modifiers;
            kb["keyName"] = ImGuiKeyboard::InputManager::GetKeyName(b.keyCode);
            j["keyBindings"].push_back(kb);
        }
    }
    return j;
}

void CircularButton::SaveToConfig() { ButtonManager::GetInstance().SaveAllButtons(); }

// ==================== ButtonManager ====================

ButtonManager::ButtonManager() {}
ButtonManager::~ButtonManager() {
    if (m_Registered) ImGuiMultiTouch::MultiTouchManager::GetInstance().RemoveListener(this);
    if (m_KeyListenerRegistered) {
        ImGuiKeyboard::InputManager::GetInstance().RemoveKeyListener(this);
    }
}

ButtonManager& ButtonManager::GetInstance() { static ButtonManager inst; return inst; }
void ButtonManager::SetConfigPath(const std::string& p) { m_ConfigPath=p; }

void ButtonManager::ClearGlobalCallbacks() {
    m_GlobalOnPress=nullptr; m_GlobalOnRelease=nullptr;
    m_GlobalOnMove=nullptr; m_GlobalOnClick=nullptr;
}

std::shared_ptr<CircularButton> ButtonManager::CreateButton(const std::string& name, const ImVec2& pos, float radius) {
    int i=FindButtonIndex(name); if(i!=-1) return m_Buttons[i];
    auto b=std::make_shared<CircularButton>(name,pos,radius);
    m_Buttons.push_back(b); SaveAllButtons(); return b;
}

void ButtonManager::LoadAllButtons() {
    m_Buttons.clear(); m_DebugSelectedButton=nullptr;
    if (m_ConfigPath.empty()) return;
    std::ifstream f(m_ConfigPath); if(!f.is_open()) return;
    try { json c; f>>c;
        if(c.contains("buttons")&&c["buttons"].is_array())
            for(auto& bc:c["buttons"]){auto b=CircularButton::LoadFromConfig(bc);if(b)m_Buttons.push_back(b);}
    } catch(...){} f.close();
    
    UpdateVolumeKeyIntercept();  // ★ 加载完配置后同步拦截状态
}

void ButtonManager::SaveAllButtons() {
    if(m_ConfigPath.empty()) return;
    json c; c["buttons"]=json::array();
    for(auto& b:m_Buttons) c["buttons"].push_back(b->ToJson());
    std::ofstream f(m_ConfigPath); if(f.is_open()){f<<c.dump(4);f.close();}
}

void ButtonManager::RenderAll() { for(auto& b:m_Buttons) b->Render(); }

std::shared_ptr<CircularButton> ButtonManager::GetButton(const std::string& n) {
    int i=FindButtonIndex(n); return i!=-1?m_Buttons[i]:nullptr;
}

void ButtonManager::RemoveButton(const std::string& n) {
    int i=FindButtonIndex(n); if(i!=-1){m_Buttons.erase(m_Buttons.begin()+i);SaveAllButtons();}
}

void ButtonManager::Clear() { m_Buttons.clear(); SaveAllButtons(); }

int ButtonManager::FindButtonIndex(const std::string& n) const {
    for(size_t i=0;i<m_Buttons.size();i++) if(m_Buttons[i]->GetName()==n) return(int)i;
    return -1;
}

void ButtonManager::SetDebugSelectedButton(CircularButton* b) { m_DebugSelectedButton=b; }

CircularButton* ButtonManager::FindButtonByTouchId(int touchId) {
    auto it=m_TouchOwnership.find(touchId);
    return it!=m_TouchOwnership.end()?it->second:nullptr;
}

// ===== ITouchEventListener =====

bool ButtonManager::OnTouchDown(int touchId, const ImVec2& position, float pressure) {
    if (!m_RenderEnabled) return false;
    for (auto& btn : m_Buttons) {
        if (btn->HandleTouchDown(touchId, position, pressure)) return true;
    }
    return false;
}

bool ButtonManager::OnTouchMove(int touchId, const ImVec2& position, const ImVec2& delta, float pressure) {
    if (!m_RenderEnabled) return false;
    auto* owner = FindButtonByTouchId(touchId);
    return owner ? owner->HandleTouchMove(touchId, position, delta, pressure) : false;
}

bool ButtonManager::OnTouchUp(int touchId, const ImVec2& lastPosition) {
    if (!m_RenderEnabled) return false;
    auto* owner = FindButtonByTouchId(touchId);
    return owner ? owner->HandleTouchUp(touchId, lastPosition) : false;
}

void ButtonManager::OnTouchCancel() {
    for (auto& b : m_Buttons) b->HandleTouchCancel();
    ClearAllTouchOwnership();
}

void ButtonManager::OnTouchStale(int touchId) {
    auto* owner = FindButtonByTouchId(touchId);
    if (owner) owner->HandleTouchStale(touchId);
}

// ===== 触摸所有权 =====

bool ButtonManager::TryClaimTouch(int touchId, CircularButton* button) {
    auto it=m_TouchOwnership.find(touchId);
    if(it==m_TouchOwnership.end()){m_TouchOwnership[touchId]=button;return true;}
    if(it->second==nullptr) return false;
    return it->second==button;
}

void ButtonManager::ReleaseTouch(int touchId, CircularButton* button) {
    auto it=m_TouchOwnership.find(touchId);
    if(it!=m_TouchOwnership.end()&&it->second==button) m_TouchOwnership.erase(it);
}

bool ButtonManager::IsTouchOwnedBy(int touchId, CircularButton* button) const {
    auto it=m_TouchOwnership.find(touchId);
    return it!=m_TouchOwnership.end()&&it->second==button;
}

bool ButtonManager::IsTouchClaimed(int touchId) const {
    return m_TouchOwnership.find(touchId)!=m_TouchOwnership.end();
}

void ButtonManager::ClearAllTouchOwnership() { m_TouchOwnership.clear(); }

void ButtonManager::SetRenderEnabled(bool enabled) {
    if (m_RenderEnabled != enabled) {
        ClearAllTouchOwnership();
        for (auto& b : m_Buttons) if(b) b->ForceReset();

        if (enabled && !m_Registered) {
            ImGuiMultiTouch::MultiTouchManager::GetInstance().AddListener(this);
            m_Registered = true;
        }

        if (enabled && !m_KeyListenerRegistered) {
            ImGuiKeyboard::InputManager::GetInstance().AddKeyListener(this);
            m_KeyListenerRegistered = true;
        }

        // ★ 删除原来的预占用逻辑，不再把已有触摸点标记为 nullptr ownership
        // 这样第一根手指抬起后重新按下就能正常响应
    }
    m_RenderEnabled = enabled;
}



void CircularButton::BindKey(int keyCode, int modifiers) {
    ImGuiKeyboard::KeyBinding binding(keyCode, modifiers);
    for (const auto& b : m_KeyBindings) {
        if (b == binding) return;
    }
    m_KeyBindings.push_back(binding);
    SaveToConfig();
    UpdateVolumeKeyIntercept();  // ★ 加这一行
    LOGI("[Button:%s] 绑定按键: %s", m_Name.c_str(), ImGuiKeyboard::InputManager::GetKeyName(keyCode));
}



void CircularButton::BindKeyByName(const std::string& keyName, int modifiers) {
    int keyCode = ImGuiKeyboard::InputManager::GetKeyCodeByName(keyName.c_str());
    if (keyCode != 0) {
        BindKey(keyCode, modifiers);
    } else {
        LOGW("[Button:%s] 未知按键名称: %s", m_Name.c_str(), keyName.c_str());
    }
}

void CircularButton::UnbindKey(int keyCode) {
    m_KeyBindings.erase(
        std::remove_if(m_KeyBindings.begin(), m_KeyBindings.end(),
            [keyCode](const ImGuiKeyboard::KeyBinding& b) { return b.keyCode == keyCode; }),
        m_KeyBindings.end());
    SaveToConfig();
    UpdateVolumeKeyIntercept();  // ★ 加这一行
}

void CircularButton::UnbindAllKeys() {
    m_KeyBindings.clear();
    SaveToConfig();
    UpdateVolumeKeyIntercept();  // ★ 加这一行
}

std::string CircularButton::GetKeyBindingText() const {
    if (m_KeyBindings.empty()) return "";
    std::string text;
    for (size_t i = 0; i < m_KeyBindings.size(); i++) {
        if (i > 0) text += " / ";
        const auto& b = m_KeyBindings[i];
        if (b.modifiers & ImGuiKeyboard::MOD_CTRL)  text += "Ctrl+";
        if (b.modifiers & ImGuiKeyboard::MOD_SHIFT) text += "Shift+";
        if (b.modifiers & ImGuiKeyboard::MOD_ALT)   text += "Alt+";
        if (b.modifiers & ImGuiKeyboard::MOD_META)  text += "Meta+";
        text += ImGuiKeyboard::InputManager::GetKeyName(b.keyCode);
    }
    return text;
}

bool CircularButton::HandleKeyDown(int keyCode, int modifiers) {
    if (!m_Enabled && !g_DebugMode) return false;
    
    // 检查是否匹配任何绑定
    bool matched = false;
    for (const auto& binding : m_KeyBindings) {
        if (binding.Matches(keyCode, modifiers)) {
            matched = true;
            break;
        }
    }
    if (!matched) return false;
    
    // 防止重复触发（按键重复事件）
    if (m_IsPressed && m_KeyPressed) return true;
    
    // 模拟按下
    m_IsPressed = true;
    m_KeyPressed = true;
    m_PressPosition = m_Position;  // 按键触发时位置就是按钮中心
    m_LastTouchPosition = m_Position;
    m_PressTime = std::chrono::steady_clock::now();
    m_IsDragging = false;
    m_LoopStopFlag.store(false);
    m_LoopStarted = false;
    m_ActiveTouchId = -100 - keyCode;  // 用负数区分按键触发
    
    m_LastEvent = ButtonEvent(ButtonEventType::Press, m_Position, m_ActiveTouchId);
    TriggerPressCallback(m_Position, m_ActiveTouchId);
    
    LOGD("[Button:%s] 按键按下: %s", m_Name.c_str(), 
         ImGuiKeyboard::InputManager::GetKeyName(keyCode));
    
    return true;
}

bool CircularButton::HandleKeyUp(int keyCode, int modifiers) {
    if (!m_KeyPressed) return false;
    
    // 检查是否匹配
    bool matched = false;
    for (const auto& binding : m_KeyBindings) {
        if (binding.Matches(keyCode, modifiers)) {
            matched = true;
            break;
        }
    }
    if (!matched) return false;
    
    // 模拟释放 + 点击
    m_LastEvent = ButtonEvent(ButtonEventType::Click, m_Position, m_ActiveTouchId);
    TriggerClickCallback(m_Position, m_ActiveTouchId);
    TriggerReleaseCallback(m_Position, m_ActiveTouchId);
    
    m_LoopStopFlag.store(true);
    m_IsPressed = false;
    m_KeyPressed = false;
    m_ActiveTouchId = -1;
    
    LOGD("[Button:%s] 按键释放: %s", m_Name.c_str(),
         ImGuiKeyboard::InputManager::GetKeyName(keyCode));
    
    return true;
}

bool ButtonManager::OnKeyDown(int keyCode, int scanCode, int modifiers) {
    if (!m_RenderEnabled) return false;
    for (auto& btn : m_Buttons) {
        if (btn && btn->HasKeyBinding()) {
            if (btn->HandleKeyDown(keyCode, modifiers)) return true;
        }
    }
    return false;
}

bool ButtonManager::OnKeyUp(int keyCode, int scanCode, int modifiers) {
    if (!m_RenderEnabled) return false;
    for (auto& btn : m_Buttons) {
        if (btn && btn->HasKeyBinding()) {
            if (btn->HandleKeyUp(keyCode, modifiers)) return true;
        }
    }
    return false;
}

} // namespace TouchUI