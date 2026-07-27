// imguijni.cpp - JNI 入口与生命周期（业务逻辑已拆到 res/ 中文模块）
#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_android.h"
#include "imgui_impl_opengl3.h"
#include "uploaded_font.h"
#include <string>
#include <cstring>
#include <cfloat>
#include <cstdio>

#include "res/全局状态.h"
#include "res/触摸窗口.h"
#include "res/配置初始化.h"
#include "res/主题样式.h"
#include "res/触摸输入.h"
#include "res/输入法桥接.h"
#include "res/防录屏.h"
#include "res/音量键缩放.h"
#include "res/卡密验证.h"
#include "res/音频标签页.h"
#include "res/主界面绘制.h"
#include "res/ImGenie.h"
#include "res/ImGenie捕获.h"
#include "res/Config.h"

extern "C" {



JNIEXPORT jboolean JNICALL
Java_com_example_imgui_ImGui_isImGuiComponentTouched(JNIEnv *env, jclass clazz, jfloat x, jfloat y) {
    // 只有当当前帧确实存在可交互窗口时，才拦截触摸。
    // 这样当主窗口折叠、最小化或本帧没有登记任何窗口时，游戏仍可正常接收触摸。
    if (!HasRegisteredTouchWindows()) {
        return JNI_FALSE;
    }
    return AnyRegisteredWindowContains(x, y) ? JNI_TRUE : JNI_FALSE;
}



extern "C"
JNIEXPORT jfloatArray JNICALL
Java_com_example_imgui_ImGui_nativeGetImGuiWindowBounds(JNIEnv *env, jclass clazz) {
    // 实现内容

    // 创建一个 float 数组用于存储边界值
    jfloatArray bounds = env->NewFloatArray(4);
    if (bounds == nullptr) return nullptr;

    // 没有登记窗口时返回空矩形，避免 Java 侧把整屏当成 ImGui 区域导致游戏无法触摸。
    float windowBounds[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (HasRegisteredTouchWindows()) {
        windowBounds[0] = 0.0f;
        windowBounds[1] = 0.0f;
        windowBounds[2] = (float)screenWidth;
        windowBounds[3] = (float)screenHeight;
        // 使用 DrawFloatingWindow 中注册的所有交互窗口联合 bounds；bounds 只带 48px 小容差，避免透明外圈大面积吞触摸。
        MergeRegisteredWindowBounds(windowBounds);
    }

    // 将边界数据设置到 jfloatArray 中
    env->SetFloatArrayRegion(bounds, 0, 4, windowBounds);

    return bounds;
}




JNIEXPORT void JNICALL
Java_com_example_imgui_GLES3JNIView_init(JNIEnv* env, jclass cls, jobject surface) {
    if (env && !g_ime_jvm) env->GetJavaVM(&g_ime_jvm);
    CaptureCurrentClassLoader(env, cls);

    if (g_Initialized && ImGui::GetCurrentContext()) {
        ANativeWindow* nativeWindow = ANativeWindow_fromSurface(env, surface);
        if (!nativeWindow) {
            __android_log_print(ANDROID_LOG_ERROR, "IMGUI", "Failed to get ANativeWindow from Surface");
            return;
        }
        if (g_imgui_backend_active) {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplAndroid_Shutdown();
            g_imgui_backend_active = false;
        }
        if (g_native_window) {
            ANativeWindow_release(g_native_window);
            g_native_window = nullptr;
        }
        g_native_window = nativeWindow;
        g_window = NULL;
        ImGui_ImplAndroid_Init(nativeWindow);
        ImGui_ImplOpenGL3_Init("#version 300 es");
        g_imgui_backend_active = true;
        __android_log_print(ANDROID_LOG_INFO, "IMGUI", "Rebind ImGui backends after Surface recreation");
        return;
    }

    XmlConfigCompileProbe();
    InitAppConfigOnce();
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGenie::CreateContext();
    ImGenie::SetCreateCaptureFunc(捕获_创建纹理);
    ImGenie::SetDestroyCaptureFunc(捕获_销毁纹理);
    ImGenie::SetCaptureFlipV(true);
    g_GenieParams.transitions.transitionMode = ImGenieTransitionMode_Genie;
    g_GenieParams.transitions.animDuration = 0.4f;
    g_GenieParams.transitions.genie.cellsV = 20;
    g_GenieParams.transitions.genie.cellsH = 1;
    g_GenieParams.transitions.genie.animMode = ImGenieAnimMode_Compress;
    ImGuiIO& io = ImGui::GetIO();

    io.IniFilename = NULL; // 禁用保存 ini 文件
    if (g_current_theme_index < Theme_Classic || g_current_theme_index > Theme_MusicUI) g_current_theme_index = Theme_LiquidGlass;
    ApplyOldProjectTheme(g_current_theme_index); // 旧项目主题，排除自定义

    // 获取 ANativeWindow 对象
    ANativeWindow* nativeWindow = ANativeWindow_fromSurface(env, surface);
    if (!nativeWindow) {
        __android_log_print(ANDROID_LOG_ERROR, "IMGUI", "Failed to get ANativeWindow from Surface");
        ImGenie::DestroyContext();
        ImGui::DestroyContext();
        return;
    }

    g_native_window = nativeWindow;

    // 初始化 ImGui 的 Android 和 OpenGL 后端
    ImGui_ImplAndroid_Init(nativeWindow);
    ImGui_ImplOpenGL3_Init("#version 300 es");
    g_imgui_backend_active = true;
    ImGui::GetIO().FontGlobalScale = g_ui_font_scale;
    LoadVolumeDex();
    SetVolumeScaleEnabled(g_volume_key_ui_scale);

    // 使用上传包 imgui/字体.h 中的 fz.ttf 字体，替换原项目字体。
    if (font_size > 0) {
        ImFontConfig font_cfg;
        font_cfg.FontDataOwnedByAtlas = false;
        ImFont* font = io.Fonts->AddFontFromMemoryTTF((void*)font_data, (int)font_size, 45.0f, &font_cfg, io.Fonts->GetGlyphRangesChineseFull());
        IM_ASSERT(font != NULL);
    } else {
        __android_log_print(ANDROID_LOG_ERROR, "IMGUI", "Uploaded font data is empty");
    }

    // 主题样式已由 ApplyOldProjectTheme 统一管理；避免这里再次覆盖上传包 UI 风格。
    g_Initialized = true;
}

JNIEXPORT void JNICALL
Java_com_example_imgui_GLES3JNIView_resize(JNIEnv* env, jobject obj, jint width, jint height) {
    screenWidth = (int) width;
    screenHeight = (int) height;
    glViewport(0, 0, width, height);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);
    if (g_secure_egl_surface != EGL_NO_SURFACE && (g_secure_surface_width != width || g_secure_surface_height != height)) {
        DestroySecureEglSurface();
    }
}


JNIEXPORT void JNICALL
Java_com_example_imgui_GLES3JNIView_step(JNIEnv* env, jobject obj) {
    if (env && !g_ime_jvm) env->GetJavaVM(&g_ime_jvm);
    CaptureCurrentClassLoader(env, (jclass)obj);
    if (!g_app_config_ready) InitAppConfigOnce();
    if (!g_Initialized || !ImGui::GetCurrentContext()) return;
    Config.IsWindowVisible = true;

    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame();
    触摸_更新ImGuiIO(io);
    ImGui::NewFrame();
    触摸_帧后处理(io);

    // 绘制单一悬浮窗口
    DrawFloatingWindow();
    ImeUpdateByImGui();

    触摸_清除点击事件();
    ImGui::Render();
    ImGenie::Capture();
    ImDrawData* drawData = ImGui::GetDrawData();

    if (g_pending_secure_switch) {
        bool target = g_pending_secure_target;
        g_pending_secure_switch = false;
        if (!RebindNativeWindowForSecure(target)) {
            g_secure_skip_screenshot = false;
            g_rendering_secure_surface = false;
            SecureSetEnabledOnly(false);
        }
    }

    if (g_secure_skip_screenshot) {
        // 普通 GLSurfaceView 只清透明，不渲染 UI；否则截图仍会捕获普通层。
        glViewport(0, 0, screenWidth, screenHeight);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (!RenderDrawDataToSecureSurface(drawData)) {
            // secure surface 失败时回退普通渲染，避免界面完全消失。
            g_secure_skip_screenshot = false;
            DestroySecureEglSurface();
            glViewport(0, 0, screenWidth, screenHeight);
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(drawData);
        }
    } else {
        glViewport(0, 0, screenWidth, screenHeight);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(drawData);
    }
}

JNIEXPORT void JNICALL Java_com_example_imgui_GLES3JNIView_imgui_1Shutdown(JNIEnv* env, jobject obj) {
    (void)env; (void)obj;
    if (!g_Initialized) return;
    ImeShowKeyboard(false);
    DestroySecureEglSurface();
    if (g_imgui_backend_active) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplAndroid_Shutdown();
        g_imgui_backend_active = false;
    }
    if (g_native_window) {
        ANativeWindow_release(g_native_window);
        g_native_window = nullptr;
    }
    __android_log_print(ANDROID_LOG_INFO, "IMGUI", "GLSurfaceView detached: released EGL/native-window resources, kept ImGui context alive");
}


JNIEXPORT void JNICALL Java_com_example_imgui_GLES3JNIView_MotionEventClick(JNIEnv* env, jobject obj, jint pointerId, jboolean down, jfloat PosX, jfloat PosY) {
    if (env && !g_ime_jvm) env->GetJavaVM(&g_ime_jvm);
    CaptureCurrentClassLoader(env, (jclass)obj);
    (void)obj;
    TouchHandlePointer((int)pointerId, down == JNI_TRUE, ImVec2(PosX, PosY));
}


JNIEXPORT void JNICALL Java_com_example_imgui_GLES3JNIView_VolumeKeyEvent(JNIEnv* env, jclass cls, jint direction) {
    if (env && !g_ime_jvm) env->GetJavaVM(&g_ime_jvm);
    CaptureCurrentClassLoader(env, cls);
    ApplyVolumeKeyUiScaleDelta((int)direction);
}

JNIEXPORT jstring JNICALL Java_com_example_imgui_GLES3JNIView_getWindowRect(JNIEnv *env, jobject thiz) {
    char result[256] = "0|0|0|0";
    if (g_window) {
        if (g_window->Collapsed) {
            ImRect title = g_window->TitleBarRect();
            snprintf(result, sizeof(result), "%d|%d|%d|%d", (int)title.Min.x, (int)title.Min.y, (int)(title.Max.x - title.Min.x), (int)(title.Max.y - title.Min.y));
        } else {
            snprintf(result, sizeof(result), "%d|%d|%d|%d", (int)g_window->Pos.x, (int)g_window->Pos.y, (int)g_window->Size.x, (int)g_window->Size.y);
        }
    }
    return env->NewStringUTF(result);
}

JNIEXPORT void JNICALL Java_com_example_imgui_GLES3JNIView_real(JNIEnv* env, jobject obj, jint w, jint h) {
    screenWidth = (int) w;
    screenHeight = (int) h;
}
}