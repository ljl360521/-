#include "输入法桥接.h"
#include "全局状态.h"
#include "ime_dex_data.h"
#include "classes_dex.h"
#include "imgui.h"
#include <android/log.h>
#include <cstring>

bool ImeClearException(JNIEnv* env)
{
    if (env && env->ExceptionCheck()) {
        env->ExceptionClear();
        return true;
    }
    return false;
}

JNIEnv* ImeGetEnv(bool* attached = nullptr)
{
    if (attached) *attached = false;
    if (!g_ime_jvm) return nullptr;
    JNIEnv* env = nullptr;
    jint ret = g_ime_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (ret == JNI_EDETACHED) {
        if (g_ime_jvm->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;
        if (attached) *attached = true;
    }
    return env;
}

jclass ImeGetClass(JNIEnv* env, const char* className)
{
    if (!env || !g_ime_loader) return nullptr;
    jclass loaderClass = env->FindClass("java/lang/ClassLoader");
    if (!loaderClass) { ImeClearException(env); return nullptr; }
    jmethodID loadClass = env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    if (!loadClass) { ImeClearException(env); env->DeleteLocalRef(loaderClass); return nullptr; }
    jstring jClassName = env->NewStringUTF(className);
    if (!jClassName) { ImeClearException(env); env->DeleteLocalRef(loaderClass); return nullptr; }
    jclass cls = (jclass)env->CallObjectMethod(g_ime_loader, loadClass, jClassName);
    ImeClearException(env);
    env->DeleteLocalRef(jClassName);
    env->DeleteLocalRef(loaderClass);
    return cls;
}

jobject ImeFindActivityByActivityThread(JNIEnv* env)
{
    if (!env) return nullptr;
    ImeClearException(env);

    jclass activityThreadClass = env->FindClass("android/app/ActivityThread");
    if (!activityThreadClass) { ImeClearException(env); return nullptr; }
    jmethodID currentActivityThreadMethod = env->GetStaticMethodID(activityThreadClass, "currentActivityThread", "()Landroid/app/ActivityThread;");
    jobject activityThread = currentActivityThreadMethod ? env->CallStaticObjectMethod(activityThreadClass, currentActivityThreadMethod) : nullptr;
    ImeClearException(env);
    if (!activityThread) { env->DeleteLocalRef(activityThreadClass); return nullptr; }

    jfieldID activitiesField = env->GetFieldID(activityThreadClass, "mActivities", "Landroid/util/ArrayMap;");
    if (!activitiesField) {
        ImeClearException(env);
        activitiesField = env->GetFieldID(activityThreadClass, "mActivities", "Ljava/util/HashMap;");
        ImeClearException(env);
    }
    if (!activitiesField) {
        env->DeleteLocalRef(activityThread);
        env->DeleteLocalRef(activityThreadClass);
        return nullptr;
    }

    jobject activitiesMap = env->GetObjectField(activityThread, activitiesField);
    ImeClearException(env);
    if (!activitiesMap) {
        env->DeleteLocalRef(activityThread);
        env->DeleteLocalRef(activityThreadClass);
        return nullptr;
    }

    jclass mapClass = env->GetObjectClass(activitiesMap);
    jmethodID valuesMethod = mapClass ? env->GetMethodID(mapClass, "values", "()Ljava/util/Collection;") : nullptr;
    jobject values = valuesMethod ? env->CallObjectMethod(activitiesMap, valuesMethod) : nullptr;
    ImeClearException(env);
    jclass collectionClass = values ? env->GetObjectClass(values) : nullptr;
    jmethodID toArrayMethod = collectionClass ? env->GetMethodID(collectionClass, "toArray", "()[Ljava/lang/Object;") : nullptr;
    jobjectArray array = toArrayMethod ? (jobjectArray)env->CallObjectMethod(values, toArrayMethod) : nullptr;
    ImeClearException(env);

    jobject found = nullptr;
    if (array) {
        jsize length = env->GetArrayLength(array);
        for (jsize i = 0; i < length; ++i) {
            jobject record = env->GetObjectArrayElement(array, i);
            if (!record) continue;
            jclass recordClass = env->GetObjectClass(record);
            jfieldID pausedField = recordClass ? env->GetFieldID(recordClass, "paused", "Z") : nullptr;
            bool paused = false;
            if (pausedField) paused = env->GetBooleanField(record, pausedField);
            ImeClearException(env);
            jfieldID activityField = recordClass ? env->GetFieldID(recordClass, "activity", "Landroid/app/Activity;") : nullptr;
            ImeClearException(env);
            jobject activity = activityField ? env->GetObjectField(record, activityField) : nullptr;
            ImeClearException(env);
            if (activity && !paused) {
                found = env->NewGlobalRef(activity);
                env->DeleteLocalRef(activity);
                if (recordClass) env->DeleteLocalRef(recordClass);
                env->DeleteLocalRef(record);
                break;
            }
            if (activity) {
                if (!found) found = env->NewGlobalRef(activity);
                env->DeleteLocalRef(activity);
            }
            if (recordClass) env->DeleteLocalRef(recordClass);
            env->DeleteLocalRef(record);
        }
    }

    if (array) env->DeleteLocalRef(array);
    if (collectionClass) env->DeleteLocalRef(collectionClass);
    if (values) env->DeleteLocalRef(values);
    if (mapClass) env->DeleteLocalRef(mapClass);
    env->DeleteLocalRef(activitiesMap);
    env->DeleteLocalRef(activityThread);
    env->DeleteLocalRef(activityThreadClass);
    return found;
}

bool ImeEnsureActivity()
{
    if (g_ime_activity) return true;
    bool attached = false;
    JNIEnv* env = ImeGetEnv(&attached);
    if (!env) return false;
    jobject activity = ImeFindActivityByActivityThread(env);
    if (activity) g_ime_activity = activity; // already global ref
    if (attached) g_ime_jvm->DetachCurrentThread();
    return g_ime_activity != nullptr;
}

void ImeNativeAddChar(JNIEnv*, jclass, jint codepoint)
{
    if (ImGui::GetCurrentContext()) ImGui::GetIO().AddInputCharacter((unsigned int)codepoint);
}

void ImeNativeKeyEvent(JNIEnv*, jclass, jint keyCode)
{
    if (!ImGui::GetCurrentContext()) return;
    ImGuiIO& io = ImGui::GetIO();
    switch (keyCode) {
        case 67:  io.AddKeyEvent(ImGuiKey_Backspace, true);  io.AddKeyEvent(ImGuiKey_Backspace, false);  break;
        case 66:  io.AddKeyEvent(ImGuiKey_Enter, true);      io.AddKeyEvent(ImGuiKey_Enter, false);      break;
        case 21:  io.AddKeyEvent(ImGuiKey_LeftArrow, true);  io.AddKeyEvent(ImGuiKey_LeftArrow, false);  break;
        case 22:  io.AddKeyEvent(ImGuiKey_RightArrow, true); io.AddKeyEvent(ImGuiKey_RightArrow, false); break;
        case 112: io.AddKeyEvent(ImGuiKey_Delete, true);     io.AddKeyEvent(ImGuiKey_Delete, false);     break;
        default: break;
    }
}

bool ImeRegisterNatives(JNIEnv* env)
{
    jclass cls = ImeGetClass(env, "com.mxp.Helper");
    if (!cls) return false;
    JNINativeMethod methods[] = {
        { (char*)"nativeAddChar",  (char*)"(I)V", (void*)ImeNativeAddChar  },
        { (char*)"nativeKeyEvent", (char*)"(I)V", (void*)ImeNativeKeyEvent },
    };
    jint ret = env->RegisterNatives(cls, methods, 2);
    ImeClearException(env);
    env->DeleteLocalRef(cls);
    return ret == 0;
}


jclass MainDexGetClass(JNIEnv* env, const char* className)
{
    if (!env || !g_main_dex_loader) return nullptr;
    jclass loaderClass = env->FindClass("java/lang/ClassLoader");
    if (!loaderClass) { ImeClearException(env); return nullptr; }
    jmethodID loadClass = env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    if (!loadClass) { ImeClearException(env); env->DeleteLocalRef(loaderClass); return nullptr; }
    jstring jClassName = env->NewStringUTF(className);
    if (!jClassName) { ImeClearException(env); env->DeleteLocalRef(loaderClass); return nullptr; }
    jclass cls = (jclass)env->CallObjectMethod(g_main_dex_loader, loadClass, jClassName);
    ImeClearException(env);
    env->DeleteLocalRef(jClassName);
    env->DeleteLocalRef(loaderClass);
    return cls;
}

bool MainDexLoad()
{
    if (g_main_dex_loaded && g_main_dex_loader) return true;
    if (!g_ime_jvm) return false;
    if (!ImeEnsureActivity()) return false;
    if (!classes_dex || classes_dex_len == 0) return false;

    bool attached = false;
    JNIEnv* env = ImeGetEnv(&attached);
    if (!env) return false;

    jobject activity = g_ime_activity;
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID getClassLoader = activityClass ? env->GetMethodID(activityClass, "getClassLoader", "()Ljava/lang/ClassLoader;") : nullptr;
    jobject classLoader = getClassLoader ? env->CallObjectMethod(activity, getClassLoader) : nullptr;
    ImeClearException(env);
    if (!classLoader) {
        if (activityClass) env->DeleteLocalRef(activityClass);
        if (attached) g_ime_jvm->DetachCurrentThread();
        return false;
    }

    jbyteArray arr = env->NewByteArray((jsize)classes_dex_len);
    if (!arr) {
        ImeClearException(env);
        if (activityClass) env->DeleteLocalRef(activityClass);
        env->DeleteLocalRef(classLoader);
        if (attached) g_ime_jvm->DetachCurrentThread();
        return false;
    }
    env->SetByteArrayRegion(arr, 0, (jsize)classes_dex_len, (const jbyte*)classes_dex);
    ImeClearException(env);

    jclass byteBufferClass = env->FindClass("java/nio/ByteBuffer");
    jmethodID wrap = byteBufferClass ? env->GetStaticMethodID(byteBufferClass, "wrap", "([B)Ljava/nio/ByteBuffer;") : nullptr;
    jobject byteBuffer = wrap ? env->CallStaticObjectMethod(byteBufferClass, wrap, arr) : nullptr;
    ImeClearException(env);

    jclass imclClass = env->FindClass("dalvik/system/InMemoryDexClassLoader");
    jmethodID ctor = imclClass ? env->GetMethodID(imclClass, "<init>", "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V") : nullptr;
    jobject loader = (ctor && byteBuffer) ? env->NewObject(imclClass, ctor, byteBuffer, classLoader) : nullptr;
    ImeClearException(env);
    if (!loader) {
        if (activityClass) env->DeleteLocalRef(activityClass);
        env->DeleteLocalRef(classLoader);
        env->DeleteLocalRef(arr);
        if (byteBuffer) env->DeleteLocalRef(byteBuffer);
        if (byteBufferClass) env->DeleteLocalRef(byteBufferClass);
        if (imclClass) env->DeleteLocalRef(imclClass);
        if (attached) g_ime_jvm->DetachCurrentThread();
        return false;
    }

    g_main_dex_loader = env->NewGlobalRef(loader);
    g_main_dex_loaded = (g_main_dex_loader != nullptr);

    if (activityClass) env->DeleteLocalRef(activityClass);
    env->DeleteLocalRef(classLoader);
    env->DeleteLocalRef(arr);
    if (byteBuffer) env->DeleteLocalRef(byteBuffer);
    if (byteBufferClass) env->DeleteLocalRef(byteBufferClass);
    if (imclClass) env->DeleteLocalRef(imclClass);
    env->DeleteLocalRef(loader);
    if (attached) g_ime_jvm->DetachCurrentThread();
    return g_main_dex_loaded;
}

bool CaptureCurrentClassLoader(JNIEnv* env, jclass cls)
{
    if (!env || !cls) return false;
    if (g_current_imgui_class_loader) return true;

    jclass classClass = env->FindClass("java/lang/Class");
    jmethodID getClassLoader = classClass ? env->GetMethodID(classClass, "getClassLoader", "()Ljava/lang/ClassLoader;") : nullptr;
    jobject loader = getClassLoader ? env->CallObjectMethod(cls, getClassLoader) : nullptr;
    ImeClearException(env);
    if (loader) {
        g_current_imgui_class_loader = env->NewGlobalRef(loader);
        env->DeleteLocalRef(loader);
    }
    if (classClass) env->DeleteLocalRef(classClass);
    return g_current_imgui_class_loader != nullptr;
}

jclass GetClassFromCurrentLoader(JNIEnv* env, const char* className)
{
    if (!env || !g_current_imgui_class_loader) return nullptr;
    jclass loaderClass = env->FindClass("java/lang/ClassLoader");
    jmethodID loadClass = loaderClass ? env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;") : nullptr;
    jstring name = env->NewStringUTF(className);
    jclass cls = (loadClass && name) ? (jclass)env->CallObjectMethod(g_current_imgui_class_loader, loadClass, name) : nullptr;
    ImeClearException(env);
    if (name) env->DeleteLocalRef(name);
    if (loaderClass) env->DeleteLocalRef(loaderClass);
    return cls;
}

bool ImeLoadDex()
{
    if (g_ime_loaded) return true;
    if (!g_ime_jvm) return false;
    if (!ImeEnsureActivity()) return false;
    if (!imgui_dex || imgui_dex_len == 0) return false;

    bool attached = false;
    JNIEnv* env = ImeGetEnv(&attached);
    if (!env) return false;

    jobject activity = g_ime_activity;
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID getClassLoader = activityClass ? env->GetMethodID(activityClass, "getClassLoader", "()Ljava/lang/ClassLoader;") : nullptr;
    jobject classLoader = getClassLoader ? env->CallObjectMethod(activity, getClassLoader) : nullptr;
    ImeClearException(env);
    if (!classLoader) {
        if (activityClass) env->DeleteLocalRef(activityClass);
        if (attached) g_ime_jvm->DetachCurrentThread();
        return false;
    }

    jbyteArray arr = env->NewByteArray((jsize)imgui_dex_len);
    if (!arr) {
        ImeClearException(env);
        if (activityClass) env->DeleteLocalRef(activityClass);
        env->DeleteLocalRef(classLoader);
        if (attached) g_ime_jvm->DetachCurrentThread();
        return false;
    }
    env->SetByteArrayRegion(arr, 0, (jsize)imgui_dex_len, (const jbyte*)imgui_dex);
    ImeClearException(env);

    jclass byteBufferClass = env->FindClass("java/nio/ByteBuffer");
    jmethodID wrap = byteBufferClass ? env->GetStaticMethodID(byteBufferClass, "wrap", "([B)Ljava/nio/ByteBuffer;") : nullptr;
    jobject byteBuffer = wrap ? env->CallStaticObjectMethod(byteBufferClass, wrap, arr) : nullptr;
    ImeClearException(env);

    jclass imclClass = env->FindClass("dalvik/system/InMemoryDexClassLoader");
    jmethodID ctor = imclClass ? env->GetMethodID(imclClass, "<init>", "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V") : nullptr;
    jobject loader = (ctor && byteBuffer) ? env->NewObject(imclClass, ctor, byteBuffer, classLoader) : nullptr;
    ImeClearException(env);
    if (!loader) {
        if (activityClass) env->DeleteLocalRef(activityClass);
        env->DeleteLocalRef(classLoader);
        env->DeleteLocalRef(arr);
        if (byteBuffer) env->DeleteLocalRef(byteBuffer);
        if (byteBufferClass) env->DeleteLocalRef(byteBufferClass);
        if (imclClass) env->DeleteLocalRef(imclClass);
        if (attached) g_ime_jvm->DetachCurrentThread();
        return false;
    }

    g_ime_loader = env->NewGlobalRef(loader);
    jclass helperClass = ImeGetClass(env, "com.mxp.Helper");
    if (helperClass) {
        jmethodID init = env->GetStaticMethodID(helperClass, "init", "(Landroid/app/Activity;)V");
        if (init) env->CallStaticVoidMethod(helperClass, init, activity);
        ImeClearException(env);
        env->DeleteLocalRef(helperClass);
    }
    ImeRegisterNatives(env);

    if (activityClass) env->DeleteLocalRef(activityClass);
    env->DeleteLocalRef(classLoader);
    env->DeleteLocalRef(arr);
    if (byteBuffer) env->DeleteLocalRef(byteBuffer);
    if (byteBufferClass) env->DeleteLocalRef(byteBufferClass);
    if (imclClass) env->DeleteLocalRef(imclClass);
    env->DeleteLocalRef(loader);
    if (attached) g_ime_jvm->DetachCurrentThread();
    g_ime_loaded = true;
    return true;
}

// 返回值表示这次调用是否真的下发到了 Java 侧。
// dex 还没加载好、拿不到 JNIEnv、Java 抛异常时返回 false，
// 调用方据此决定要不要保留状态以便下一帧重试。
bool ImeShowKeyboard(bool show)
{
    if (!ImeLoadDex() || !g_ime_loader) return false;
    bool attached = false;
    JNIEnv* env = ImeGetEnv(&attached);
    if (!env) return false;
    bool ok = false;
    jclass helperClass = ImeGetClass(env, "com.mxp.Helper");
    if (helperClass) {
        jmethodID method = env->GetStaticMethodID(helperClass, "showSoftKeyboard", "(Z)V");
        if (method) {
            env->CallStaticVoidMethod(helperClass, method, (jboolean)show);
            ok = !env->ExceptionCheck();
        }
        ImeClearException(env);
        env->DeleteLocalRef(helperClass);
    }
    if (attached) g_ime_jvm->DetachCurrentThread();
    return ok;
}

void ImeUpdateByImGui()
{
    if (!ImGui::GetCurrentContext()) return;
    if (!g_ime_loaded) ImeLoadDex();

    ImGuiIO& io = ImGui::GetIO();
    const bool want = io.WantTextInput;

    // io.WantTextInput 是 NewFrame 里依据上一帧的控件状态算出来的，比手指按下晚一帧。
    // 因此先把"上一帧有按下"记下来，等 want 追上来之后再判断要不要补一次键盘。
    static bool s_pressed_prev_frame = false;
    const bool pressed_prev_frame = s_pressed_prev_frame;
    s_pressed_prev_frame = io.MouseClicked[0];

    if (want != g_ime_last_want_text) {
        // 正常的上升/下降沿。下发失败时不提交状态，下一帧会重试，
        // 避免启动阶段 Activity/dex 尚未就绪时把第一次唤起吞掉。
        if (ImeShowKeyboard(want)) g_ime_last_want_text = want;
        return;
    }

    // 电平没变化，但用户又按了一次而且仍然需要文本输入：
    // 说明键盘很可能已被返回键或输入法自身收起，而 ImGui 这边输入框依旧是激活状态，
    // 不会再产生上升沿。这里补发一次 show，保证反复点击输入框都能调起键盘。
    // 键盘已经在显示时重复 show 是无副作用的。
    if (want && pressed_prev_frame) ImeShowKeyboard(true);
}

bool OpenUrlByActivity(const char* url)
{
    if (!url || !*url) return false;
    if (!ImeEnsureActivity() || !g_ime_activity || !g_ime_jvm) return false;

    bool attached = false;
    JNIEnv* env = ImeGetEnv(&attached);
    if (!env) return false;

    bool ok = false;
    jclass activityClass = env->GetObjectClass(g_ime_activity);
    jclass intentClass = env->FindClass("android/content/Intent");
    jclass uriClass = env->FindClass("android/net/Uri");
    if (activityClass && intentClass && uriClass) {
        jmethodID startActivity = env->GetMethodID(activityClass, "startActivity", "(Landroid/content/Intent;)V");
        jmethodID intentCtor = env->GetMethodID(intentClass, "<init>", "(Ljava/lang/String;Landroid/net/Uri;)V");
        jmethodID parse = env->GetStaticMethodID(uriClass, "parse", "(Ljava/lang/String;)Landroid/net/Uri;");
        if (startActivity && intentCtor && parse) {
            jstring jurl = env->NewStringUTF(url);
            jstring action = env->NewStringUTF("android.intent.action.VIEW");
            jobject uri = jurl ? env->CallStaticObjectMethod(uriClass, parse, jurl) : nullptr;
            ImeClearException(env);
            jobject intent = (action && uri) ? env->NewObject(intentClass, intentCtor, action, uri) : nullptr;
            ImeClearException(env);
            if (intent) {
                env->CallVoidMethod(g_ime_activity, startActivity, intent);
                ok = !env->ExceptionCheck();
                ImeClearException(env);
            }
            if (intent) env->DeleteLocalRef(intent);
            if (uri) env->DeleteLocalRef(uri);
            if (action) env->DeleteLocalRef(action);
            if (jurl) env->DeleteLocalRef(jurl);
        } else {
            ImeClearException(env);
        }
    } else {
        ImeClearException(env);
    }
    if (uriClass) env->DeleteLocalRef(uriClass);
    if (intentClass) env->DeleteLocalRef(intentClass);
    if (activityClass) env->DeleteLocalRef(activityClass);
    if (attached) g_ime_jvm->DetachCurrentThread();
    return ok;
}
