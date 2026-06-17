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
    if (ImGui::Begin("可一定", nullptr, windowFlags)) {
        g_window = ImGui::GetCurrentWindow();

        // 标签页
        if (ImGui::BeginTabBar("##main_tabs", ImGuiTabBarFlags_None)) {
            // 光环 UI 标签页（Mac 风格毛玻璃 UI）
            if (ImGui::BeginTabItem("\xe5\x85\x89\xe7\x8e\xafUI")) {
                aura_ui::render_aura_tab();
                ImGui::EndTabItem();
            }

            // 设置标签页（UI 大小控制）
            if (ImGui::BeginTabItem("\xe8\xae\xbe\xe7\xbd\xae")) {
                // 基础样式在首次进入时捕获，作为缩放基准
                static float ui_scale = 1.0f;
                static ImGuiStyle base_style;
                static bool base_captured = false;
                if (!base_captured) {
                    base_style = ImGui::GetStyle();
                    base_captured = true;
                }

                ImGui::TextUnformatted("UI \xe8\xae\xbe\xe7\xbd\xae");
                ImGui::Separator();
                ImGui::Spacing();

                // UI 整体大小滑块（同时缩放字体和样式尺寸）
                if (ImGui::SliderFloat("UI \xe5\xa4\xa7\xe5\xb0\x8f", &ui_scale, 0.5f, 2.0f, "%.2fx")) {
                    ImGuiStyle& style = ImGui::GetStyle();
                    style = base_style;
                    style.ScaleAllSizes(ui_scale);
                    ImGui::GetIO().FontGlobalScale = ui_scale;
                }
                ImGui::Spacing();
                ImGui::TextUnformatted("\xe6\x8b\x96\xe5\x8a\xa8\xe6\xbb\x91\xe5\x9d\x97\xe8\xb0\x83\xe6\x95\xb4 UI \xe6\x95\xb4\xe4\xbd\x93\xe5\xa4\xa7\xe5\xb0\x8f");
                ImGui::Text("\xe5\xbd\x93\xe5\x89\x8d\xe7\xbc\xa9\xe6\x94\xbe: %.2f \xe5\x80\x8d", ui_scale);

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
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