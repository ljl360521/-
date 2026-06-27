#include "ImGuiTools.h"
#include "logger.h"
#include <android/log.h>
#include <cstring>

extern jobject g_DexClassLoader;

namespace ImGuiTools {

// ========== 内部状态 ==========
static JavaVM* g_vm = nullptr;
static jclass g_cls = nullptr;
static bool g_initialized = false;

// 缓存的 Java 方法 ID
static jmethodID g_openURL = nullptr;
static jmethodID g_closeWebView = nullptr;
static jmethodID g_isWebViewVisible = nullptr;

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
    if (env && env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}

// ========== 公开接口 ==========
void Init(JavaVM* vm) {
    if (g_initialized) return;
    g_vm = vm;

    JNIEnv* env = GetEnv();
    if (!env) {
        LOGE("Failed to get JNIEnv");
        return;
    }

    if (!g_DexClassLoader) {
        LOGE("DexClassLoader not available");
        return;
    }

    jclass dexCls = env->FindClass("dalvik/system/DexClassLoader");
    if (!dexCls) {
        LOGE("Failed to find DexClassLoader class");
        return;
    }
    jmethodID loadClassMethod = env->GetMethodID(dexCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    env->DeleteLocalRef(dexCls);
    if (!loadClassMethod) {
        LOGE("Failed to find loadClass method");
        return;
    }

    jstring className = env->NewStringUTF("com.example.imgui.ImGuiToolsHelper");
    jclass localCls = (jclass)env->CallObjectMethod(g_DexClassLoader, loadClassMethod, className);
    env->DeleteLocalRef(className);

    if (!localCls) {
        ClearException(env);
        LOGE("Failed to find ImGuiToolsHelper class via DexClassLoader");
        return;
    }

    g_cls = (jclass)env->NewGlobalRef(localCls);
    env->DeleteLocalRef(localCls);

    g_openURL = env->GetStaticMethodID(g_cls, "openURL", "(Ljava/lang/String;Z)V");
    g_closeWebView = env->GetStaticMethodID(g_cls, "closeWebView", "()V");
    g_isWebViewVisible = env->GetStaticMethodID(g_cls, "isWebViewVisible", "()Z");

    if (!g_openURL || !g_closeWebView || !g_isWebViewVisible) {
        LOGE("Failed to find one or more ImGuiToolsHelper methods");
        return;
    }

    g_initialized = true;
    LOGI("ImGuiTools initialized");
}

void OpenURL(const char* url, bool internal) {
    if (!g_initialized || !url) return;
    JNIEnv* env = GetEnv();
    if (!env) return;
    ClearException(env);
    jstring jurl = env->NewStringUTF(url);
    env->CallStaticVoidMethod(g_cls, g_openURL, jurl, (jboolean)internal);
    env->DeleteLocalRef(jurl);
    LOGI("OpenURL: %s (internal=%d)", url, internal);
}

void CloseWebView() {
    if (!g_initialized) return;
    JNIEnv* env = GetEnv();
    if (!env) return;
    ClearException(env);
    env->CallStaticVoidMethod(g_cls, g_closeWebView);
}

bool IsWebViewVisible() {
    if (!g_initialized) return false;
    JNIEnv* env = GetEnv();
    if (!env) return false;
    ClearException(env);
    return (bool)env->CallStaticBooleanMethod(g_cls, g_isWebViewVisible);
}

void Shutdown() {
    CloseWebView();
    JNIEnv* env = GetEnv();
    if (env && g_cls) {
        env->DeleteGlobalRef(g_cls);
        g_cls = nullptr;
    }
    g_vm = nullptr;
    g_initialized = false;
    LOGI("ImGuiTools shutdown");
}

} // namespace ImGuiTools