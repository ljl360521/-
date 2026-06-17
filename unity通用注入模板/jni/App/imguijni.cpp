#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <GLES3/gl3.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_android.h"
#include "imgui_impl_opengl3.h"
#include "zt_ttf.h"
#include "aura_ui.hpp"
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
    if (g_Initialized) return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    io.IniFilename = NULL; // 禁用保存 ini 文件
    ImGui::StyleColorsClassic(); // 设置经典风格

    // 获取 ANativeWindow 对象
    ANativeWindow* nativeWindow = ANativeWindow_fromSurface(env, surface);
    if (!nativeWindow) {
        __android_log_print(ANDROID_LOG_ERROR, "IMGUI", "Failed to get ANativeWindow from Surface");
        return;
    }

    // 初始化 ImGui 的 Android 和 OpenGL 后端
    ImGui_ImplAndroid_Init(nativeWindow);
    ImGui_ImplOpenGL3_Init("#version 300 es");

    // 使用嵌入的字体数据
    if (zt_ttf_len > 0) {
        ImFont* font = io.Fonts->AddFontFromMemoryTTF((void*)zt_ttf, zt_ttf_len, 45.0f, NULL, io.Fonts->GetGlyphRangesChineseFull());
        IM_ASSERT(font != NULL); // 确保字体加载成功
    } else {
        __android_log_print(ANDROID_LOG_ERROR, "IMGUI", "Embedded font data is empty");
    }

    // 样式调整
    ImGui::GetStyle().ScaleAllSizes(3.0f); // 放大样式比例
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 5.3f;
    style.FrameRounding = 2.3f;
    style.ScrollbarRounding = 0;

    // 初始化 Aura UI（应用 Mac 风格毛玻璃样式）
    aura_ui::init();

    g_Initialized = true;
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
    // 直接渲染 AuraNexus Mac 风格毛玻璃 UI（完整窗口，包含 Begin/End）
    // 替换掉原项目的整个悬浮窗口结构
    aura_ui::render_window();
}

JNIEXPORT void JNICALL
Java_com_example_imgui_GLES3JNIView_step(JNIEnv* env, jobject obj) {
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