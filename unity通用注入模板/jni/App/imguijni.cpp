#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <GLES3/gl3.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_android.h"
#include "imgui_impl_opengl3.h"
#include "zt_ttf.h"
#include <string>
#include <vector>
#include <cstring>
#include <random>
#include <ctime>
#include <cfloat>

int screenWidth = 0;
int screenHeight = 0;
bool g_Initialized = false;
ImGuiWindow* g_window = NULL;

// 配置
struct sConfig {
    bool IsWindowVisible;
};
sConfig Config{true};

extern "C" {



static bool get_target_imgui_window_bounds(float outBounds[4]) {
    outBounds[0] = outBounds[1] = outBounds[2] = outBounds[3] = 0.0f;

    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (ctx == nullptr) {
        return false;
    }

    // 优先使用绘制时记录的目标窗口，避免取到 Debug/Toast 等非目标窗口。
    ImGuiWindow* window = g_window;
    if (window == nullptr || !window->Active) {
        window = ImGui::FindWindowByName("悬浮窗口");
    }

    if (window == nullptr || !window->Active || window->Hidden) {
        return false;
    }

    outBounds[0] = window->Pos.x;
    outBounds[1] = window->Pos.y;
    outBounds[2] = window->Pos.x + window->Size.x;
    outBounds[3] = window->Pos.y + window->Size.y;
    return true;
}

JNIEXPORT jboolean JNICALL
Java_com_example_imgui_ImGui_isImGuiComponentTouched(JNIEnv *env, jclass clazz, jfloat x, jfloat y) {
    float windowBounds[4];
    if (!get_target_imgui_window_bounds(windowBounds)) {
        return JNI_FALSE;
    }

    return (x >= windowBounds[0] && x <= windowBounds[2] &&
            y >= windowBounds[1] && y <= windowBounds[3]) ? JNI_TRUE : JNI_FALSE;
}



extern "C"
JNIEXPORT jfloatArray JNICALL
Java_com_example_imgui_ImGui_nativeGetImGuiWindowBounds(JNIEnv *env, jclass clazz) {
    jfloatArray bounds = env->NewFloatArray(4);
    if (bounds == nullptr) return nullptr;

    float windowBounds[4];
    get_target_imgui_window_bounds(windowBounds);
    env->SetFloatArrayRegion(bounds, 0, 4, windowBounds);

    return bounds;
}




JNIEXPORT void JNICALL
Java_com_example_imgui_GLES3JNIView_init(JNIEnv* env, jclass cls, jobject surface) {
    // 获取 ANativeWindow 对象（每次 surface 重建都需要重新获取）
    ANativeWindow* nativeWindow = ANativeWindow_fromSurface(env, surface);
    if (!nativeWindow) {
        __android_log_print(ANDROID_LOG_ERROR, "IMGUI", "Failed to get ANativeWindow from Surface");
        return;
    }

    if (!g_Initialized) {
        // === 首次初始化：创建 ImGui Context + 后端 ===
        __android_log_print(ANDROID_LOG_INFO, "IMGUI", "First time initialization");

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();

        io.IniFilename = NULL;
        ImGui::StyleColorsClassic();

        ImGui_ImplAndroid_Init(nativeWindow);
        ImGui_ImplOpenGL3_Init("#version 300 es");

        if (zt_ttf_len > 0) {
            ImFont* font = io.Fonts->AddFontFromMemoryTTF((void*)zt_ttf, zt_ttf_len, 45.0f, NULL, io.Fonts->GetGlyphRangesChineseFull());
            IM_ASSERT(font != NULL);
        } else {
            __android_log_print(ANDROID_LOG_ERROR, "IMGUI", "Embedded font data is empty");
        }

        ImGui::GetStyle().ScaleAllSizes(3.0f);
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 5.3f;
        style.FrameRounding = 2.3f;
        style.ScrollbarRounding = 0;

        g_Initialized = true;
        __android_log_print(ANDROID_LOG_INFO, "IMGUI", "First init success");
    } else {
        // === 重新初始化：EGL Context 丢失后（切后台再切回）===
        // 保留 ImGui Context（窗口状态不丢），只重建 OpenGL 和 Android 后端
        __android_log_print(ANDROID_LOG_INFO, "IMGUI", "Re-initializing backends after EGL context loss");

        // 销毁旧后端（OpenGL 资源已随 EGL Context 丢失，这里只清理内部状态）
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplAndroid_Shutdown();

        // 重新初始化后端（使用新的 ANativeWindow）
        ImGui_ImplAndroid_Init(nativeWindow);
        ImGui_ImplOpenGL3_Init("#version 300 es");

        // 清除悬空指针，DrawFloatingWindow 会在下一帧重新赋值
        g_window = NULL;

        __android_log_print(ANDROID_LOG_INFO, "IMGUI", "Backends re-initialized, window state preserved");
    }
}

JNIEXPORT void JNICALL
Java_com_example_imgui_GLES3JNIView_resize(JNIEnv* env, jobject obj, jint width, jint height) {
    screenWidth = (int) width;
    screenHeight = (int) height;
    glViewport(0, 0, width, height);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);
}

void DrawFloatingWindow() {
    // 设置悬浮窗口位置和大小，动态适配屏幕
    float windowWidth = screenWidth * 0.8f;
    float windowHeight = screenHeight * 0.6f;
    ImGui::SetNextWindowPos(ImVec2(screenWidth * 0.1f, screenHeight * 0.2f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight), ImGuiCond_FirstUseEver);

    // 关键修复来自已修好版本的逻辑：ScaleAllSizes(3.0f) 会把 style.WindowMinSize 放大，
    // 手动缩放到较小尺寸时 ImGui 会被最小尺寸反复拉回，表现为一卡一卡。
    // 绘制窗口前显式降低最小尺寸，并设置窗口尺寸约束，让 ResizeGrip 连续跟手。
    ImVec2 minSize(58.0f, ImGui::GetFrameHeight());
    ImGui::GetStyle().WindowMinSize = minSize;
    ImGui::SetNextWindowSizeConstraints(minSize, ImVec2(FLT_MAX, FLT_MAX));

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("悬浮窗口", nullptr, windowFlags)) {
        g_window = ImGui::GetCurrentWindow();
    }
    ImGui::End();
}

JNIEXPORT void JNICALL
Java_com_example_imgui_GLES3JNIView_step(JNIEnv* env, jobject obj) {
    if (!g_Initialized) return;
    if (!Config.IsWindowVisible) return;

    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame();
    ImGui::NewFrame();

    // 绘制单一悬浮窗口
    DrawFloatingWindow();

    ImGui::Render();
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

JNIEXPORT void JNICALL Java_com_example_imgui_GLES3JNIView_imgui_1Shutdown(JNIEnv* env, jobject obj) {
    if (!g_Initialized) return;

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplAndroid_Shutdown();
    ImGui::DestroyContext();
    g_Initialized = false;
    g_window = NULL; // 清除悬空指针，避免切后台再切回时访问已释放的窗口
}

JNIEXPORT void JNICALL Java_com_example_imgui_GLES3JNIView_MotionEventClick(JNIEnv* env, jclass clazz, jint pointerId, jboolean down, jfloat PosX, jfloat PosY) {
    (void)env;
    (void)clazz;
    (void)pointerId;

    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (ctx == nullptr) {
        return;
    }

    ImGuiIO &io = ImGui::GetIO();
    // 保持单一输入通道，避免 AddMouse* 事件队列与直接字段写入同时生效造成 resize 坐标抖动。
    io.MousePos = ImVec2(PosX, PosY);
    io.MouseDown[0] = (down == JNI_TRUE);
}

JNIEXPORT jstring JNICALL Java_com_example_imgui_GLES3JNIView_getWindowRect(JNIEnv *env, jobject thiz) {
    char result[256] = "0|0|0|0";
    if (g_window) {
        sprintf(result, "%d|%d|%d|%d", (int)g_window->Pos.x, (int)g_window->Pos.y, (int)g_window->Size.x, (int)g_window->Size.y);
    }
    return env->NewStringUTF(result);
}

JNIEXPORT void JNICALL Java_com_example_imgui_GLES3JNIView_real(JNIEnv* env, jobject obj, jint w, jint h) {
    screenWidth = (int) w;
    screenHeight = (int) h;
}
}