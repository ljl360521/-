#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <GLES3/gl3.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_android.h"
#include "imgui_impl_opengl3.h"
#include "zt_ttf.h"
#include "ESP.h"
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
    if (ImGui::Begin("悬浮窗口", nullptr, windowFlags)) {
        g_window = ImGui::GetCurrentWindow();

        // ===== ESP 透视控制面板 =====
        ESPSystem& esp = ESPSystem::Instance();
        ESPConfig& cfg = esp.config;

        if (ImGui::CollapsingHeader("ESP 透视")) {
            ImGui::Checkbox("总开关", &cfg.draw_enabled);
            ImGui::SameLine();
            ImGui::TextDisabled("(游戏就绪: %s)", esp.IsGameReady() ? "是" : "否");
            ImGui::SameLine();
            ImGui::Text("对象数: %zu", esp.GetObjectCount());

            // 诊断信息 (帮助排查"勾选没用"的问题)
            if (ImGui::TreeNode("诊断信息")) {
                ImGui::TextWrapped("状态: %s", esp.GetDiagStatus().c_str());
                ImGui::Separator();
                ImGui::TextDisabled("字段偏移 (供对照 logcat):");
                ImGui::TextWrapped("%s", esp.GetDiagOffsets().c_str());
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("绘制项")) {
                ImGui::Checkbox("圆圈",   &cfg.show_circle);
                ImGui::Checkbox("名称",   &cfg.show_name);
                ImGui::Checkbox("追踪线", &cfg.show_tracer);
                ImGui::Checkbox("ID",     &cfg.show_id);
                ImGui::Checkbox("分数",   &cfg.show_score);
                ImGui::Checkbox("半径",   &cfg.show_radius);
                ImGui::Checkbox("距离",   &cfg.show_distance);
                ImGui::Checkbox("自身标记", &cfg.show_self_marker);
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("颜色")) {
                ImGui::ColorEdit4("圆圈##cc",  (float*)&cfg.circle_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreview);
                ImGui::SameLine();
                ImGui::ColorEdit4("名称##nc",  (float*)&cfg.name_color,   ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreview);
                ImGui::ColorEdit4("追踪线##tc",(float*)&cfg.tracer_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreview);
                ImGui::SameLine();
                ImGui::ColorEdit4("自身##sc",  (float*)&cfg.self_color,   ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreview);
                ImGui::ColorEdit4("敌人##ec",  (float*)&cfg.enemy_color,  ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreview);
                ImGui::SameLine();
                ImGui::ColorEdit4("死亡##dc",  (float*)&cfg.dead_color,   ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreview);
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("参数")) {
                ImGui::SliderFloat("圆圈线宽", &cfg.circle_thickness, 0.5f, 8.0f);
                ImGui::SliderInt("圆圈分段",   &cfg.circle_segments, 6, 64);
                ImGui::SliderFloat("名称字号", &cfg.name_font_size, 10.0f, 40.0f);
                ImGui::SliderFloat("名称偏移", &cfg.name_offset_y, 5.0f, 60.0f);
                ImGui::SliderFloat("追踪线宽", &cfg.tracer_thickness, 0.5f, 5.0f);
                ImGui::SliderInt("自身 rankId", &cfg.self_rank_id, -1, 200);
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("改名 (Rename)")) {
                ImGui::Checkbox("启用改名", &cfg.rename_enabled);
                char buf[128];
                snprintf(buf, sizeof(buf), "%s", cfg.name_prefix.c_str());
                if (ImGui::InputText("前缀##rp", buf, sizeof(buf))) {
                    cfg.name_prefix = buf;
                }
                ImGui::SameLine();
                if (ImGui::Button("应用改名")) {
                    esp.ApplyRename(cfg.name_prefix);
                }
                ImGui::TreePop();
            }
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

    // ESP: 每帧读取游戏数据 (IL2CPP), 须在渲染前完成
    ESP_Tick();

    // 绘制单一悬浮窗口 (含 ESP 控制面板)
    DrawFloatingWindow();

    // ESP: 渲染透视绘制 (使用 BackgroundDrawList, 须在 NewFrame 后、Render 前)
    ESP_Render();

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