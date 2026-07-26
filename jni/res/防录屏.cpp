#include "防录屏.h"
#include "全局状态.h"
#include "输入法桥接.h"
#include "res/secure/secure_dex_data.h"
#include "imgui_impl_opengl3.h"
#include <android/native_window_jni.h>
#include <GLES3/gl3.h>
#include <vector>
#include <cstring>

bool SetJavaSecureSurfaceMode(bool enable)
{
    // 关键：使用当前 GLES3JNIView native 调用所属 ClassLoader。
    // 这样加载到的是实际创建 display/boundActivity 的同一份 com.example.imgui.ImGui，
    // 避免重新 DexClassLoader/InMemoryDexClassLoader 造成 static 字段不共享。
    if (!g_current_imgui_class_loader || !g_ime_jvm) return false;
    bool attached = false;
    JNIEnv* env = ImeGetEnv(&attached);
    if (!env) return false;

    bool ok = false;
    jclass imguiClass = GetClassFromCurrentLoader(env, "com.example.imgui.ImGui");
    if (imguiClass) {
        jmethodID method = env->GetStaticMethodID(imguiClass, "setSecureMode", "(Z)Z");
        if (method) ok = env->CallStaticBooleanMethod(imguiClass, method, (jboolean)enable);
        ImeClearException(env);
        env->DeleteLocalRef(imguiClass);
    }
    if (attached) g_ime_jvm->DetachCurrentThread();
    return ok;
}


jclass SecureGetClass(JNIEnv* env, const char* className)
{
    if (!env || !g_secure_loader) return nullptr;
    jclass loaderClass = env->FindClass("java/lang/ClassLoader");
    jmethodID loadClass = loaderClass ? env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;") : nullptr;
    jstring name = env->NewStringUTF(className);
    jclass cls = (loadClass && name) ? (jclass)env->CallObjectMethod(g_secure_loader, loadClass, name) : nullptr;
    ImeClearException(env);
    if (name) env->DeleteLocalRef(name);
    if (loaderClass) env->DeleteLocalRef(loaderClass);
    return cls;
}

bool SecureLoadDex()
{
    if (g_secure_loaded && g_secure_loader) return true;
    if (!g_ime_jvm || !ImeEnsureActivity() || !secure_dex || secure_dex_len == 0) return false;
    bool attached = false;
    JNIEnv* env = ImeGetEnv(&attached);
    if (!env) return false;

    jobject activity = g_ime_activity;
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID getClassLoader = activityClass ? env->GetMethodID(activityClass, "getClassLoader", "()Ljava/lang/ClassLoader;") : nullptr;
    jobject parent = getClassLoader ? env->CallObjectMethod(activity, getClassLoader) : nullptr;
    ImeClearException(env);
    if (!parent) { if (activityClass) env->DeleteLocalRef(activityClass); if (attached) g_ime_jvm->DetachCurrentThread(); return false; }

    jbyteArray arr = env->NewByteArray((jsize)secure_dex_len);
    if (!arr) {
        ImeClearException(env);
        if (activityClass) env->DeleteLocalRef(activityClass);
        env->DeleteLocalRef(parent);
        if (attached) g_ime_jvm->DetachCurrentThread();
        return false;
    }
    env->SetByteArrayRegion(arr, 0, (jsize)secure_dex_len, (const jbyte*)secure_dex);
    ImeClearException(env);
    jclass bbClass = env->FindClass("java/nio/ByteBuffer");
    jmethodID wrap = bbClass ? env->GetStaticMethodID(bbClass, "wrap", "([B)Ljava/nio/ByteBuffer;") : nullptr;
    jobject bb = wrap ? env->CallStaticObjectMethod(bbClass, wrap, arr) : nullptr;
    ImeClearException(env);
    jclass clClass = env->FindClass("dalvik/system/InMemoryDexClassLoader");
    jmethodID ctor = clClass ? env->GetMethodID(clClass, "<init>", "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V") : nullptr;
    jobject loader = (ctor && bb) ? env->NewObject(clClass, ctor, bb, parent) : nullptr;
    ImeClearException(env);
    if (loader) g_secure_loader = env->NewGlobalRef(loader);

    bool ok = false;
    if (g_secure_loader) {
        jclass secureClass = SecureGetClass(env, "com.mxp.SecureSurfaceHelper");
        if (secureClass) {
            jmethodID init = env->GetStaticMethodID(secureClass, "init", "(Ljava/lang/Object;)Z");
            if (init) ok = env->CallStaticBooleanMethod(secureClass, init, activity);
            ImeClearException(env);
            env->DeleteLocalRef(secureClass);
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
    g_secure_loaded = ok;
    return ok;
}

ANativeWindow* SecureGetNativeWindow(int width, int height)
{
    if (!SecureLoadDex() || !g_secure_loader || !g_ime_jvm) return nullptr;
    bool attached = false;
    JNIEnv* env = ImeGetEnv(&attached);
    if (!env) return nullptr;
    ANativeWindow* out = nullptr;
    jclass secureClass = SecureGetClass(env, "com.mxp.SecureSurfaceHelper");
    if (secureClass) {
        jmethodID setEnabled = env->GetStaticMethodID(secureClass, "setProtectionEnabled", "(Z)Z");
        if (setEnabled) env->CallStaticBooleanMethod(secureClass, setEnabled, (jboolean)true);
        ImeClearException(env);
        jmethodID getSurface = env->GetStaticMethodID(secureClass, "getSurface", "(II)Ljava/lang/Object;");
        jobject surface = getSurface ? env->CallStaticObjectMethod(secureClass, getSurface, (jint)width, (jint)height) : nullptr;
        ImeClearException(env);
        if (surface) { out = ANativeWindow_fromSurface(env, surface); env->DeleteLocalRef(surface); }
        env->DeleteLocalRef(secureClass);
    }
    if (attached) g_ime_jvm->DetachCurrentThread();
    return out;
}

void SecureSetEnabledOnly(bool enable)
{
    if (!SecureLoadDex() || !g_secure_loader || !g_ime_jvm) return;
    bool attached = false;
    JNIEnv* env = ImeGetEnv(&attached);
    if (!env) return;
    jclass secureClass = SecureGetClass(env, "com.mxp.SecureSurfaceHelper");
    if (secureClass) {
        jmethodID setEnabled = env->GetStaticMethodID(secureClass, "setProtectionEnabled", "(Z)Z");
        if (setEnabled) env->CallStaticBooleanMethod(secureClass, setEnabled, (jboolean)enable);
        ImeClearException(env);
        env->DeleteLocalRef(secureClass);
    }
    if (attached) g_ime_jvm->DetachCurrentThread();
}

void DestroySecureEglSurface()
{
    EGLDisplay currentDisplay = eglGetCurrentDisplay();
    EGLSurface currentDraw = eglGetCurrentSurface(EGL_DRAW);
    EGLSurface currentRead = eglGetCurrentSurface(EGL_READ);
    EGLContext currentContext = eglGetCurrentContext();

    if (g_secure_egl_display != EGL_NO_DISPLAY && g_secure_egl_surface != EGL_NO_SURFACE) {
        if (currentDisplay == g_secure_egl_display && (currentDraw == g_secure_egl_surface || currentRead == g_secure_egl_surface)) {
            eglMakeCurrent(currentDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
        eglDestroySurface(g_secure_egl_display, g_secure_egl_surface);
    }
    g_secure_egl_display = EGL_NO_DISPLAY;
    g_secure_egl_surface = EGL_NO_SURFACE;
    g_secure_egl_context = EGL_NO_CONTEXT;
    g_secure_surface_width = 0;
    g_secure_surface_height = 0;

    if (g_secure_native_window) {
        ANativeWindow_release(g_secure_native_window);
        g_secure_native_window = nullptr;
    }
    SecureSetEnabledOnly(false);

    if (currentDisplay != EGL_NO_DISPLAY && currentContext != EGL_NO_CONTEXT && currentDraw != EGL_NO_SURFACE) {
        eglMakeCurrent(currentDisplay, currentDraw, currentRead, currentContext);
    }
}

EGLConfig FindCurrentEglConfig(EGLDisplay display, EGLContext context)
{
    if (display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT) return nullptr;
    EGLint configId = 0;
    if (!eglQueryContext(display, context, EGL_CONFIG_ID, &configId) || configId == 0) return nullptr;

    EGLint count = 0;
    if (!eglGetConfigs(display, nullptr, 0, &count) || count <= 0) return nullptr;
    std::vector<EGLConfig> configs((size_t)count);
    if (!eglGetConfigs(display, configs.data(), count, &count)) return nullptr;
    for (int i = 0; i < count; ++i) {
        EGLint id = 0;
        eglGetConfigAttrib(display, configs[(size_t)i], EGL_CONFIG_ID, &id);
        if (id == configId) return configs[(size_t)i];
    }
    return nullptr;
}

bool EnsureSecureEglSurface(int width, int height)
{
    if (width <= 0) width = 1;
    if (height <= 0) height = 1;

    EGLDisplay display = eglGetCurrentDisplay();
    EGLContext context = eglGetCurrentContext();
    if (display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT) return false;

    if (g_secure_egl_surface != EGL_NO_SURFACE &&
        g_secure_egl_display == display &&
        g_secure_egl_context == context &&
        g_secure_surface_width == width &&
        g_secure_surface_height == height &&
        g_secure_native_window) {
        return true;
    }

    DestroySecureEglSurface();

    ANativeWindow* win = SecureGetNativeWindow(width, height);
    if (!win) return false;

    EGLConfig config = FindCurrentEglConfig(display, context);
    if (!config) {
        ANativeWindow_release(win);
        SecureSetEnabledOnly(false);
        return false;
    }

    const EGLint attrs[] = { EGL_NONE };
    EGLSurface surface = eglCreateWindowSurface(display, config, (EGLNativeWindowType)win, attrs);
    if (surface == EGL_NO_SURFACE) {
        ANativeWindow_release(win);
        SecureSetEnabledOnly(false);
        return false;
    }

    g_secure_egl_display = display;
    g_secure_egl_context = context;
    g_secure_egl_surface = surface;
    g_secure_native_window = win;
    g_secure_surface_width = width;
    g_secure_surface_height = height;
    g_rendering_secure_surface = true;
    return true;
}

bool RenderDrawDataToSecureSurface(ImDrawData* drawData)
{
    if (!drawData) return false;
    int width = screenWidth > 0 ? screenWidth : (int)drawData->DisplaySize.x;
    int height = screenHeight > 0 ? screenHeight : (int)drawData->DisplaySize.y;
    if (!EnsureSecureEglSurface(width, height)) return false;

    EGLDisplay oldDisplay = eglGetCurrentDisplay();
    EGLSurface oldDraw = eglGetCurrentSurface(EGL_DRAW);
    EGLSurface oldRead = eglGetCurrentSurface(EGL_READ);
    EGLContext oldContext = eglGetCurrentContext();
    if (oldDisplay == EGL_NO_DISPLAY || oldContext == EGL_NO_CONTEXT || oldDraw == EGL_NO_SURFACE) return false;

    if (!eglMakeCurrent(oldDisplay, g_secure_egl_surface, g_secure_egl_surface, oldContext)) return false;
    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(drawData);
    eglSwapBuffers(oldDisplay, g_secure_egl_surface);

    eglMakeCurrent(oldDisplay, oldDraw, oldRead, oldContext);
    glViewport(0, 0, screenWidth, screenHeight);
    return true;
}

bool RebindNativeWindowForSecure(bool enable)
{
    if (enable) {
        return EnsureSecureEglSurface(screenWidth > 0 ? screenWidth : 1, screenHeight > 0 ? screenHeight : 1);
    }
    DestroySecureEglSurface();
    g_rendering_secure_surface = false;
    return true;
}
