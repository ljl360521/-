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
#include "app_log.h"
#include <string>
#include <vector>
#include <cstring>
#include <random>
#include <ctime>
#include <cfloat>

// 从 Main.cpp 引入 (用于初始化 AppLog 获取应用私有目录)
// 注意: Main.cpp 中这些是普通 C++ 全局, 非 extern "C"
extern jobject g_ActivityInstance;
extern JNIEnv* getJNIEnv();

int screenWidth = 0;
int screenHeight = 0;
bool g_Initialized = false;
ImGuiWindow* g_window = NULL;

// 保存 ANativeWindow 以便每帧兜底获取屏幕尺寸 (修复屏幕尺寸为 0 的根因)
static ANativeWindow* g_NativeWindow = NULL;

// 前向声明 (定义在下方)
static void EnsureAppLogInitialized();
static void DrawESPTab();
static void DrawLogTab();

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

    // 关键修复: init 时立即从 ANativeWindow 获取屏幕尺寸
    // 之前只在 resize() 里设置 io.DisplaySize, 如果 Java 层未调 resize 或调用晚,
    // DisplaySize 一直是 (0,0) → scale=0 → ESP 圆圈极小 + 全在屏幕中心
    g_NativeWindow = nativeWindow;
    int32_t winWidth = ANativeWindow_getWidth(nativeWindow);
    int32_t winHeight = ANativeWindow_getHeight(nativeWindow);
    if (winWidth > 0 && winHeight > 0) {
        screenWidth = winWidth;
        screenHeight = winHeight;
        io.DisplaySize = ImVec2((float)winWidth, (float)winHeight);
        __android_log_print(ANDROID_LOG_INFO, "IMGUI",
            "init: 屏幕尺寸 %dx%d (从 ANativeWindow 获取)", winWidth, winHeight);
    } else {
        __android_log_print(ANDROID_LOG_WARN, "IMGUI",
            "init: ANativeWindow 尺寸异常 %dx%d, 将在 step 兜底", winWidth, winHeight);
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

    // 初始化应用日志系统 (自动识别被注入应用的私有目录)
    // 用于在"日志"标签页显示详细诊断 + 导出到私有目录
    EnsureAppLogInitialized();
}

// 确保 AppLog 已初始化
// 使用 /proc/self/cmdline 识别包名 (不依赖 Activity, 任何时候都可靠)
// 在 GLES3JNIView_init 中调用一次, 也可在绘制日志页时懒加载重试
static void EnsureAppLogInitialized() {
    if (AppLog::Instance().IsInitialized()) return;
    // 无参 Init: 通过 /proc/self/cmdline 读取包名, 构造私有目录路径
    // 不依赖 g_ActivityInstance (它在另一个 native 线程异步设置, 时机不可靠)
    AppLog::Instance().Init();
    if (AppLog::Instance().IsInitialized()) {
        APP_LOGI("UI", "ImGui 初始化完成, 屏幕尺寸: %dx%d", screenWidth, screenHeight);
        APP_LOGI("UI", "应用日志系统已就绪 — 包名=%s 路径=%s",
            AppLog::Instance().GetPackageName().c_str(),
            AppLog::Instance().GetExportPath().c_str());
        APP_LOGI("UI", "详情请见日志标签页, 点击导出按钮可导出到私有目录");
    } else {
        __android_log_print(ANDROID_LOG_WARN, "AppLog",
            "EnsureAppLogInitialized: Init() 失败 (无法读取 /proc/self/cmdline?)");
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

        // 使用标签页: ESP 透视 + 日志
        if (ImGui::BeginTabBar("MainTabs", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
            if (ImGui::BeginTabItem("ESP 透视")) {
                DrawESPTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("日志")) {
                DrawLogTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

// ===== ESP 透视标签页 (原 DrawFloatingWindow 主体) =====
static void DrawESPTab() {
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
            ImGui::SliderFloat("缩放微调", &cfg.zoom_scale, 0.1f, 5.0f, "%.2f");
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("调整圆圈大小贴合球体\n1.0=自动\n>1 放大圆圈\n<1 缩小圆圈");
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

// ===== 日志标签页 =====
// 显示 AppLog 内存缓冲中的详细日志, 支持导出到被注入应用的私有目录
static bool g_logAutoScroll = true;     // 自动滚动到最新
static bool g_logShowDebug = true;      // 显示 DEBUG 级别
static std::string g_lastExportResult;  // 上次导出结果提示

static void DrawLogTab() {
    // 懒加载: 如果 AppLog 还没初始化, 尝试初始化
    if (!AppLog::Instance().IsInitialized()) {
        EnsureAppLogInitialized();
    }

    AppLog& log = AppLog::Instance();

    // --- 顶部: 包名 + 导出路径 + 操作按钮 ---
    if (ImGui::Button("导出日志到文件")) {
        if (log.IsInitialized()) {
            std::string path = log.ExportToFile();
            if (!path.empty()) {
                g_lastExportResult = "✓ 导出成功: " + path;
                APP_LOGI("UI", "用户点击导出日志, 已写入: %s", path.c_str());
            } else {
                g_lastExportResult = "✗ 导出失败 (无法写入文件, 请检查权限)";
                APP_LOGE("UI", "用户点击导出日志, 但导出失败");
            }
        } else {
            g_lastExportResult = "✗ 日志系统未初始化 (无法获取应用私有目录)";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("清空日志")) {
        log.Clear();
        g_lastExportResult = "已清空内存日志";
    }
    ImGui::SameLine();
    ImGui::Checkbox("自动滚动", &g_logAutoScroll);
    ImGui::SameLine();
    ImGui::Checkbox("显示DEBUG", &g_logShowDebug);

    ImGui::Separator();

    // 显示包名和导出路径
    if (log.IsInitialized()) {
        ImGui::TextDisabled("包名: %s", log.GetPackageName().c_str());
        ImGui::TextDisabled("导出路径: %s", log.GetExportPath().c_str());
    } else {
        ImGui::TextDisabled("日志系统未初始化 (等待 Activity 就绪...)");
    }

    // 显示上次导出结果
    if (!g_lastExportResult.empty()) {
        ImGui::TextWrapped("%s", g_lastExportResult.c_str());
    }

    ImGui::Separator();

    // --- 日志列表 (可滚动) ---
    // 使用 ImGuiChildFlags_Border 让日志区有边框, 便于区分
    ImGui::BeginChild("LogList", ImVec2(0, 0), ImGuiChildFlags_Border);

    std::vector<AppLogEntry> lines = log.GetLines();
    for (const auto& e : lines) {
        // DEBUG 级别过滤
        if (e.level == APP_LOG_DEBUG && !g_logShowDebug) continue;

        // 按级别着色
        ImU32 color = IM_COL32(200, 200, 200, 255); // 默认灰白
        const char* levelStr = "?";
        switch (e.level) {
            case APP_LOG_DEBUG: color = IM_COL32(150, 150, 150, 255); levelStr = "D"; break;
            case APP_LOG_INFO:  color = IM_COL32(100, 255, 100, 255); levelStr = "I"; break;
            case APP_LOG_WARN:  color = IM_COL32(255, 200, 0, 255);   levelStr = "W"; break;
            case APP_LOG_ERROR: color = IM_COL32(255, 80, 80, 255);   levelStr = "E"; break;
        }

        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted("[");
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(e.timestamp.c_str());
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted("][");
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(levelStr);
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted("][");
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(e.tag.c_str());
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted("] ");
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(e.message.c_str());
        ImGui::PopStyleColor();
    }

    // 自动滚动到底部
    if (g_logAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
}

JNIEXPORT void JNICALL
Java_com_example_imgui_GLES3JNIView_step(JNIEnv* env, jobject obj) {
    if (!Config.IsWindowVisible) return;

    // 关键修复: 每帧兜底确保屏幕尺寸不为 0
    // 如果 DisplaySize 为 0 (resize 未调用或 ANativeWindow 尺寸异常),
    // 从保存的 ANativeWindow 重新获取, 或用 screenWidth/screenHeight 全局变量兜底
    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) {
        if (g_NativeWindow) {
            int32_t w = ANativeWindow_getWidth(g_NativeWindow);
            int32_t h = ANativeWindow_getHeight(g_NativeWindow);
            if (w > 0 && h > 0) {
                screenWidth = w;
                screenHeight = h;
                io.DisplaySize = ImVec2((float)w, (float)h);
            }
        }
        if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) {
            // 最终兜底: 用全局变量
            if (screenWidth > 0 && screenHeight > 0) {
                io.DisplaySize = ImVec2((float)screenWidth, (float)screenHeight);
            }
        }
    }

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