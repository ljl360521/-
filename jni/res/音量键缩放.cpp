#include "音量键缩放.h"
#include "全局状态.h"
#include "配置初始化.h"
#include "输入法桥接.h"
#include "res/volume/volume_key_dex_data.h"
#include "imgui.h"
#include <cstring>


static jclass VolumeGetClass(JNIEnv* env, const char* className)
{
    if (!env || !g_volume_loader) return nullptr;
    jclass loaderClass = env->FindClass("java/lang/ClassLoader");
    jmethodID loadClass = loaderClass ? env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;") : nullptr;
    jstring name = env->NewStringUTF(className);
    jclass cls = (loadClass && name) ? (jclass)env->CallObjectMethod(g_volume_loader, loadClass, name) : nullptr;
    ImeClearException(env);
    if (name) env->DeleteLocalRef(name);
    if (loaderClass) env->DeleteLocalRef(loaderClass);
    return cls;
}


static bool AdjustSystemVolumeByDirection(int direction)
{
    if (direction == 0 || !g_ime_jvm || !ImeEnsureActivity() || !g_ime_activity) return false;

    bool attached = false;
    JNIEnv* env = ImeGetEnv(&attached);
    if (!env) return false;

    bool ok = false;
    jclass activityClass = env->GetObjectClass(g_ime_activity);
    jmethodID getSystemService = activityClass ? env->GetMethodID(activityClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;") : nullptr;
    jstring audioName = env->NewStringUTF("audio");
    jobject audioManager = (getSystemService && audioName) ? env->CallObjectMethod(g_ime_activity, getSystemService, audioName) : nullptr;
    ImeClearException(env);

    jclass audioClass = audioManager ? env->GetObjectClass(audioManager) : nullptr;
    jmethodID adjustStreamVolume = audioClass ? env->GetMethodID(audioClass, "adjustStreamVolume", "(III)V") : nullptr;
    if (adjustStreamVolume) {
        constexpr jint STREAM_MUSIC = 3;
        constexpr jint ADJUST_RAISE = 1;
        constexpr jint ADJUST_LOWER = -1;
        constexpr jint FLAG_SHOW_UI = 1;
        env->CallVoidMethod(audioManager, adjustStreamVolume,
                            STREAM_MUSIC,
                            direction > 0 ? ADJUST_RAISE : ADJUST_LOWER,
                            FLAG_SHOW_UI);
        ok = !env->ExceptionCheck();
        ImeClearException(env);
    } else {
        ImeClearException(env);
    }

    if (audioClass) env->DeleteLocalRef(audioClass);
    if (audioManager) env->DeleteLocalRef(audioManager);
    if (audioName) env->DeleteLocalRef(audioName);
    if (activityClass) env->DeleteLocalRef(activityClass);
    if (attached) g_ime_jvm->DetachCurrentThread();
    return ok;
}


void ApplyVolumeKeyUiScaleDelta(int direction)
{
    if (direction == 0) return;

    // 关闭“音量键调UI大小”后，如果 Java 侧还拦截到了音量键，
    // 这里主动按普通音量键处理一次，避免按键被吃掉导致系统音量没有反应。
    if (!g_volume_key_ui_scale || !ImGui::GetCurrentContext()) {
        AdjustSystemVolumeByDirection(direction);
        return;
    }

    float oldScale = g_ui_font_scale;
    g_ui_font_scale += direction > 0 ? 0.05f : -0.05f;
    if (g_ui_font_scale < 0.50f) g_ui_font_scale = 0.50f;
    if (g_ui_font_scale > 1.00f) g_ui_font_scale = 1.00f;
    if (oldScale != g_ui_font_scale) {
        ImGui::GetIO().FontGlobalScale = g_ui_font_scale;
        SaveAppConfigNow();
    }
}

void NativeVolumeKey(JNIEnv*, jclass, jint direction)
{
    ApplyVolumeKeyUiScaleDelta((int)direction);
}

bool RegisterVolumeNatives(JNIEnv* env)
{
    jclass cls = VolumeGetClass(env, "com.mxp.VolumeKeyHelper");
    if (!cls) return false;
    JNINativeMethod methods[] = {
        { (char*)"nativeVolumeKey", (char*)"(I)V", (void*)NativeVolumeKey },
    };
    jint ret = env->RegisterNatives(cls, methods, 1);
    ImeClearException(env);
    env->DeleteLocalRef(cls);
    return ret == 0;
}

bool LoadVolumeDex()
{
    if (g_volume_loaded && g_volume_loader) return true;
    if (!g_ime_jvm || !ImeEnsureActivity() || !volume_key_dex || volume_key_dex_len == 0) return false;
    bool attached = false;
    JNIEnv* env = ImeGetEnv(&attached);
    if (!env) return false;

    jobject activity = g_ime_activity;
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID getClassLoader = activityClass ? env->GetMethodID(activityClass, "getClassLoader", "()Ljava/lang/ClassLoader;") : nullptr;
    jobject parent = getClassLoader ? env->CallObjectMethod(activity, getClassLoader) : nullptr;
    ImeClearException(env);
    if (!parent) { if (activityClass) env->DeleteLocalRef(activityClass); if (attached) g_ime_jvm->DetachCurrentThread(); return false; }

    jbyteArray arr = env->NewByteArray((jsize)volume_key_dex_len);
    if (!arr) {
        ImeClearException(env);
        if (activityClass) env->DeleteLocalRef(activityClass);
        env->DeleteLocalRef(parent);
        if (attached) g_ime_jvm->DetachCurrentThread();
        return false;
    }
    env->SetByteArrayRegion(arr, 0, (jsize)volume_key_dex_len, (const jbyte*)volume_key_dex);
    ImeClearException(env);
    jclass bbClass = env->FindClass("java/nio/ByteBuffer");
    jmethodID wrap = bbClass ? env->GetStaticMethodID(bbClass, "wrap", "([B)Ljava/nio/ByteBuffer;") : nullptr;
    jobject bb = wrap ? env->CallStaticObjectMethod(bbClass, wrap, arr) : nullptr;
    ImeClearException(env);
    jclass clClass = env->FindClass("dalvik/system/InMemoryDexClassLoader");
    jmethodID ctor = clClass ? env->GetMethodID(clClass, "<init>", "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V") : nullptr;
    jobject loader = (ctor && bb) ? env->NewObject(clClass, ctor, bb, parent) : nullptr;
    ImeClearException(env);
    if (loader) g_volume_loader = env->NewGlobalRef(loader);

    bool ok = false;
    if (g_volume_loader && RegisterVolumeNatives(env)) {
        jclass cls = VolumeGetClass(env, "com.mxp.VolumeKeyHelper");
        if (cls) {
            jmethodID init = env->GetStaticMethodID(cls, "init", "(Landroid/app/Activity;)V");
            if (init) env->CallStaticVoidMethod(cls, init, activity);
            ImeClearException(env);
            env->DeleteLocalRef(cls);
            ok = true;
        }
    }

    if (activityClass) env->DeleteLocalRef(activityClass);
    env->DeleteLocalRef(parent);
    if (arr) env->DeleteLocalRef(arr);
    if (bb) env->DeleteLocalRef(bb);
    if (bbClass) env->DeleteLocalRef(bbClass);
    if (clClass) env->DeleteLocalRef(clClass);
    if (loader) env->DeleteLocalRef(loader);
    if (attached) g_ime_jvm->DetachCurrentThread();
    g_volume_loaded = ok;
    return ok;
}

bool SetVolumeScaleEnabled(bool enable)
{
    if (!LoadVolumeDex() || !g_volume_loader || !g_ime_jvm) return false;
    bool attached = false;
    JNIEnv* env = ImeGetEnv(&attached);
    if (!env) return false;
    bool ok = false;
    jclass cls = VolumeGetClass(env, "com.mxp.VolumeKeyHelper");
    if (cls) {
        jmethodID method = env->GetStaticMethodID(cls, "setEnabled", "(Z)V");
        if (method) { env->CallStaticVoidMethod(cls, method, (jboolean)enable); ok = true; }
        ImeClearException(env);
        env->DeleteLocalRef(cls);
    }
    if (attached) g_ime_jvm->DetachCurrentThread();
    return ok;
}
