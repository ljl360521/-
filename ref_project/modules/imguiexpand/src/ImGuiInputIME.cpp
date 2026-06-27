#include "ImGuiInputIME.h"
#include "imgui.h"
#include "logger.h"
#include <cstring>

namespace ImGuiIME {

// ========== 内部状态 ==========
static JavaVM* g_vm = nullptr;
static jclass g_cls = nullptr;

// 缓存的 Java 方法 ID
static jmethodID g_init = nullptr;
static jmethodID g_showWithText = nullptr;
static jmethodID g_hideInputMethod = nullptr;
static jmethodID g_getInputText = nullptr;
static jmethodID g_clearInput = nullptr;
static jmethodID g_isInputVisible = nullptr;
static jmethodID g_getComposingStart = nullptr;
static jmethodID g_getComposingEnd = nullptr;

// 当前激活的输入框 ID
static ImGuiID g_activeID = 0;
static int g_graceFrames = 0;
static bool g_initialized = false;

// ========== JNI 辅助 ==========
static JNIEnv* GetEnv() {
    JNIEnv* env = nullptr;
    if (g_vm) {
        int status = g_vm->GetEnv((void**)&env, JNI_VERSION_1_6);
        if (status < 0) {
            g_vm->AttachCurrentThread(&env, nullptr);
        }
    }
    return env;
}

static void ClearException(JNIEnv* env) {
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}

/**
 * 通过反射 ActivityThread 获取当前 Application 的 ClassLoader，
 * 再用它加载 IMEHelper 类。
 * 
 * 等价于 Java:
 *   Object at = ActivityThread.currentActivityThread();
 *   Application app = at.getApplication();
 *   ClassLoader cl = app.getClassLoader();
 *   Class<?> cls = cl.loadClass("com.example.imgui.IMEHelper");
 */
static jclass FindIMEHelperClass(JNIEnv* env) {
    // 1. 获取 ActivityThread.currentActivityThread()
    jclass atClass = env->FindClass("android/app/ActivityThread");
    if (!atClass) { ClearException(env); return nullptr; }

    jmethodID currentAT = env->GetStaticMethodID(atClass, "currentActivityThread",
                                                   "()Landroid/app/ActivityThread;");
    if (!currentAT) { ClearException(env); env->DeleteLocalRef(atClass); return nullptr; }

    jobject at = env->CallStaticObjectMethod(atClass, currentAT);
    if (!at) { ClearException(env); env->DeleteLocalRef(atClass); return nullptr; }

    // 2. 获取 Application: at.getApplication()
    jmethodID getApp = env->GetMethodID(atClass, "getApplication",
                                         "()Landroid/app/Application;");
    env->DeleteLocalRef(atClass);
    if (!getApp) { ClearException(env); env->DeleteLocalRef(at); return nullptr; }

    jobject app = env->CallObjectMethod(at, getApp);
    env->DeleteLocalRef(at);
    if (!app) { ClearException(env); return nullptr; }

    // 3. 获取 ClassLoader: app.getClass().getClassLoader()
    jclass objClass = env->GetObjectClass(app);
    jmethodID getClassLoader = env->GetMethodID(objClass, "getClassLoader",
                                                 "()Ljava/lang/ClassLoader;");
    env->DeleteLocalRef(objClass);
    if (!getClassLoader) { ClearException(env); env->DeleteLocalRef(app); return nullptr; }

    jobject classLoader = env->CallObjectMethod(app, getClassLoader);
    env->DeleteLocalRef(app);
    if (!classLoader) { ClearException(env); return nullptr; }

    // 4. classLoader.loadClass("com.example.imgui.IMEHelper")
    jclass clClass = env->FindClass("java/lang/ClassLoader");
    jmethodID loadClass = env->GetMethodID(clClass, "loadClass",
                                            "(Ljava/lang/String;)Ljava/lang/Class;");
    env->DeleteLocalRef(clClass);
    if (!loadClass) { ClearException(env); env->DeleteLocalRef(classLoader); return nullptr; }

    jstring className = env->NewStringUTF("com.example.imgui.IMEHelper");
    jclass result = (jclass)env->CallObjectMethod(classLoader, loadClass, className);
    env->DeleteLocalRef(className);
    env->DeleteLocalRef(classLoader);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }

    return result;
}

// ========== Java 调用封装 ==========
static void CallShowWithText(const std::string& text) {
    JNIEnv* env = GetEnv();
    if (!env || !g_cls || !g_showWithText) return;
    ClearException(env);
    jstring jtext = env->NewStringUTF(text.c_str());
    env->CallStaticVoidMethod(g_cls, g_showWithText, jtext);
    env->DeleteLocalRef(jtext);
}

static void CallHide() {
    JNIEnv* env = GetEnv();
    if (!env || !g_cls || !g_hideInputMethod) return;
    ClearException(env);
    env->CallStaticVoidMethod(g_cls, g_hideInputMethod);
}

static std::string CallGetText() {
    JNIEnv* env = GetEnv();
    if (!env || !g_cls || !g_getInputText) return "";
    ClearException(env);
    jstring result = (jstring)env->CallStaticObjectMethod(g_cls, g_getInputText);
    if (!result) return "";
    const char* str = env->GetStringUTFChars(result, nullptr);
    std::string ret(str);
    env->ReleaseStringUTFChars(result, str);
    env->DeleteLocalRef(result);
    return ret;
}

static void CallClear() {
    JNIEnv* env = GetEnv();
    if (!env || !g_cls || !g_clearInput) return;
    ClearException(env);
    env->CallStaticVoidMethod(g_cls, g_clearInput);
}

static bool CallIsVisible() {
    JNIEnv* env = GetEnv();
    if (!env || !g_cls || !g_isInputVisible) return false;
    ClearException(env);
    return (bool)env->CallStaticBooleanMethod(g_cls, g_isInputVisible);
}

static int CallGetComposingStart() {
    JNIEnv* env = GetEnv();
    if (!env || !g_cls || !g_getComposingStart) return -1;
    ClearException(env);
    return env->CallStaticIntMethod(g_cls, g_getComposingStart);
}

static int CallGetComposingEnd() {
    JNIEnv* env = GetEnv();
    if (!env || !g_cls || !g_getComposingEnd) return -1;
    ClearException(env);
    return env->CallStaticIntMethod(g_cls, g_getComposingEnd);
}

// ========== 公开接口 ==========
void Init(JavaVM* vm) {
    LOGI("开始初始化IMEHelper");
    if (g_initialized) return;
    g_vm = vm;

    JNIEnv* env = GetEnv();
    if (!env) {
        LOGE("Failed to get JNIEnv");
        return;
    }

    // 通过反射 ActivityThread -> Application -> ClassLoader 加载 IMEHelper
    jclass localCls = FindIMEHelperClass(env);
    if (!localCls) {
        LOGE("Failed to find IMEHelper class via reflection");
        return;
    }

    g_cls = (jclass)env->NewGlobalRef(localCls);
    env->DeleteLocalRef(localCls);

    // 缓存方法 ID（包括无参 init）
    g_init           = env->GetStaticMethodID(g_cls, "init", "()V");
    g_showWithText   = env->GetStaticMethodID(g_cls, "showWithText", "(Ljava/lang/String;)V");
    g_hideInputMethod = env->GetStaticMethodID(g_cls, "hideInputMethod", "()V");
    g_getInputText   = env->GetStaticMethodID(g_cls, "getInputText", "()Ljava/lang/String;");
    g_clearInput     = env->GetStaticMethodID(g_cls, "clearInput", "()V");
    g_isInputVisible = env->GetStaticMethodID(g_cls, "isInputVisible", "()Z");
    g_getComposingStart = env->GetStaticMethodID(g_cls, "getComposingStart", "()I");
    g_getComposingEnd   = env->GetStaticMethodID(g_cls, "getComposingEnd", "()I");

    if (!g_showWithText || !g_hideInputMethod || !g_getInputText || !g_clearInput || !g_isInputVisible) {
        LOGE("Failed to find one or more IMEHelper methods");
        return;
    }

    // 调用 Java 侧的 IMEHelper.init()（无参，自动反射获取 Activity）
    if (g_init) {
        ClearException(env);
        env->CallStaticVoidMethod(g_cls, g_init);
        if (env->ExceptionCheck()) {
            LOGE("IMEHelper.init() threw exception");
            env->ExceptionClear();
            return;
        }
    }

    g_initialized = true;
    LOGI("ImGuiIME initialized");
}

bool InputText(const char* label, char* buf, int bufSize, float width) {
    bool changed = false;
    ImGuiID id = ImGui::GetID(label);

    // 同步 IME 文本
    if (g_activeID == id) {
        std::string imeText = CallGetText();
        if (!imeText.empty() || CallIsVisible()) {
            if (strncmp(buf, imeText.c_str(), bufSize) != 0) {
                strncpy(buf, imeText.c_str(), bufSize - 1);
                buf[bufSize - 1] = '\0';
                changed = true;
            }
        }
        if (!CallIsVisible() && g_graceFrames <= 0) {
            g_activeID = 0;
        }
        if (g_graceFrames > 0) g_graceFrames--;
    }

    // ===== 绘制 =====
    if (width > 0) ImGui::PushItemWidth(width);

    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));

    float w = (width > 0) ? width : ImGui::GetContentRegionAvail().x;
    float h = ImGui::GetFrameHeight();
    bool isActive = (g_activeID == id);

    // 获取 composing 范围
    int compStart = -1, compEnd = -1;
    if (isActive) {
        compStart = CallGetComposingStart();
        compEnd   = CallGetComposingEnd();
    }

    // 计算绘制位置
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 padding = ImGui::GetStyle().FramePadding;

    // 绘制背景框
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImU32 bgColor = ImGui::GetColorU32(isActive ? ImGuiCol_FrameBgActive : ImGuiCol_FrameBg);
    ImU32 borderColor = ImGui::GetColorU32(ImGuiCol_Border);
    drawList->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), bgColor);
    drawList->AddRect(pos, ImVec2(pos.x + w, pos.y + h), borderColor);

    // 绘制文本
    ImVec2 textPos = ImVec2(pos.x + padding.x, pos.y + padding.y);
    const char* text = buf;
    bool isEmpty = (buf[0] == '\0');

    if (isEmpty) {
        drawList->AddText(textPos, IM_COL32(128, 128, 128, 255), "点击输入...");
    } else {
        ImFont* font = ImGui::GetFont();
        float fontSize = ImGui::GetFontSize();

        if (compStart >= 0 && compEnd > compStart) {
            // 有 composing 区间：分三段绘制
            std::string fullText(buf);

            // UTF-8 字节偏移转换（composing 索引是字符级别的）
            auto charToByteOffset = [&](int charIdx) -> int {
                int byteOff = 0;
                int ch = 0;
                while (ch < charIdx && byteOff < (int)fullText.size()) {
                    unsigned char c = fullText[byteOff];
                    if (c < 0x80) byteOff += 1;
                    else if (c < 0xE0) byteOff += 2;
                    else if (c < 0xF0) byteOff += 3;
                    else byteOff += 4;
                    ch++;
                }
                return byteOff;
            };

            int byteStart = charToByteOffset(compStart);
            int byteEnd   = charToByteOffset(compEnd);

            std::string before  = fullText.substr(0, byteStart);
            std::string comping = fullText.substr(byteStart, byteEnd - byteStart);
            std::string after   = fullText.substr(byteEnd);

            ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
            ImU32 compColor = IM_COL32(66, 135, 245, 255); // 蓝色 composing 文字

            float xOff = textPos.x;
            if (!before.empty()) {
                drawList->AddText(ImVec2(xOff, textPos.y), textColor,
                                  before.c_str());
                xOff += font->CalcTextSizeA(fontSize, FLT_MAX, 0,
                                            before.c_str()).x;
            }

            if (!comping.empty()) {
                drawList->AddText(ImVec2(xOff, textPos.y), compColor,
                                  comping.c_str());
                ImVec2 compSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0,
                                                       comping.c_str());
                float underlineY = textPos.y + compSize.y + 1.0f;
                drawList->AddLine(
                    ImVec2(xOff, underlineY),
                    ImVec2(xOff + compSize.x, underlineY),
                    compColor, 2.0f);
                xOff += compSize.x;
            }

            if (!after.empty()) {
                drawList->AddText(ImVec2(xOff, textPos.y), textColor,
                                  after.c_str());
            }
        } else {
            drawList->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), text);
        }
    }

    // 光标闪烁（激活状态）
    if (isActive) {
        float cursorX = textPos.x + ImGui::GetFont()->CalcTextSizeA(
            ImGui::GetFontSize(), FLT_MAX, 0, buf).x;
        if (fmod(ImGui::GetTime(), 1.0f) < 0.5f) {
            drawList->AddLine(
                ImVec2(cursorX + 1, pos.y + padding.y),
                ImVec2(cursorX + 1, pos.y + h - padding.y),
                ImGui::GetColorU32(ImGuiCol_Text), 1.0f);
        }
    }

    // 不可见的按钮用于接收点击
    ImGui::InvisibleButton(label, ImVec2(w, h));
    if (ImGui::IsItemClicked()) {
        if (g_activeID != 0 && g_activeID != id) {
            CallHide();
            CallClear();
        }
        CallShowWithText(std::string(buf));
        g_activeID = id;
        g_graceFrames = 30;
    }

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    if (width > 0) ImGui::PopItemWidth();

    return changed;
}

void HideKeyboard() {
    if (g_activeID != 0) {
        CallHide();
        CallClear();
        g_activeID = 0;
        g_graceFrames = 0;
    }
}

void Shutdown() {
    HideKeyboard();
    JNIEnv* env = GetEnv();
    if (env && g_cls) {
        env->DeleteGlobalRef(g_cls);
        g_cls = nullptr;
    }
    g_vm = nullptr;
    g_initialized = false;
    LOGI("ImGuiIME shutdown");
}

} // namespace ImGuiIME