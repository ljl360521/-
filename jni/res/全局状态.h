#pragma once
// 模块共享全局状态与类型

#include <jni.h>
#include <android/native_window.h>
#include <EGL/egl.h>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>
#include "imgui.h"
#include "imgui_internal.h"
#include "res/ImGenie.h"
#include "t3sdk.h"

extern int screenWidth;
extern int screenHeight;
extern bool g_Initialized;
extern bool g_imgui_backend_active;
extern ANativeWindow* g_native_window;
extern ImGuiWindow* g_window;

struct sConfig {
    bool IsWindowVisible;
};
extern sConfig Config;

// T3
extern T3Verify* g_t3_verify;
extern std::once_flag g_t3_init_once;
extern std::atomic<bool> g_t3_ready;
extern std::atomic<bool> g_t3_authed;
extern std::atomic<bool> g_t3_verifying;
extern std::atomic<bool> g_t3_heartbeat_running;
extern std::mutex g_t3_mutex;
extern std::string g_t3_status;
extern std::string g_t3_notice;
extern std::string g_t3_end_time;
extern std::string g_t3_kami;
extern std::string g_t3_statecode;
extern std::string g_t3_machine_code;
extern bool g_kami_saved_valid;
extern std::string g_saved_kami;
extern bool g_t3_auto_login_tried;
extern bool g_t3_auto_login_running;

extern const char* T3_LOGIN_CODE;
extern const char* T3_NOTICE_CODE;
extern const char* T3_VERSION_CODE;
extern const char* T3_HEARTBEAT_CODE;
extern const char* T3_APPKEY;
extern const char* T3_RSA_PUBLIC_KEY;
extern const char* LOCAL_VERSION;

// JVM / DEX / Secure / IME / Volume / UI config
extern JavaVM* g_ime_jvm;
extern jobject g_ime_activity;
extern jobject g_main_dex_loader;
extern bool g_main_dex_loaded;
extern jobject g_current_imgui_class_loader;
extern jobject g_secure_loader;
extern bool g_secure_loaded;
extern bool g_pending_secure_switch;
extern bool g_pending_secure_target;
extern bool g_rendering_secure_surface;
extern ANativeWindow* g_secure_native_window;
extern EGLDisplay g_secure_egl_display;
extern EGLSurface g_secure_egl_surface;
extern EGLContext g_secure_egl_context;
extern int g_secure_surface_width;
extern int g_secure_surface_height;
extern jobject g_ime_loader;
extern bool g_ime_loaded;
extern bool g_ime_last_want_text;
extern bool g_secure_skip_screenshot;
extern char g_logInput[256];
extern bool g_show_another_window;
extern ImGenieParams g_GenieParams;
extern bool g_show_log_window;
extern ImVec4 g_clear_color;
extern float g_ui_font_scale;
extern bool g_volume_key_ui_scale;
extern jobject g_volume_loader;
extern bool g_volume_loaded;
extern int g_main_tab_index;
extern int g_current_theme_index;
extern std::string g_config_base_dir;
extern bool g_app_config_ready;
extern bool g_config_registered;

// 吐球加速
extern bool g_spitball_accelerate;
extern int g_spitball_speed;

// 三角合球
extern bool g_triangle_running;

// 视野修改
extern bool g_view_enabled;
extern float g_view_scale;
extern bool g_view_was_enabled;

// 粘合修改
extern bool g_merge_enabled;
extern float g_merge_value;
extern bool g_merge_was_enabled;

// 排名名字修改
extern bool g_rename_enabled;
extern bool g_rename_was_enabled;
extern bool g_rename_use_color;          // 是否给改后的名字上色
extern ImVec4 g_rename_name_color;       // ImGui 颜色选择器结果 (RGBA 0~1)
extern bool g_rename_detailed_rank;      // true=完整数字，false=粗略(千/万)
extern bool g_rename_show_self;          // true=也改自己的名字，默认false

// 球上名字大小（独立功能，与排名改名无关）
// 对应 TargetNameScale2.Update 中 SelfTF.localScale 公式乘子
extern bool g_name_scale_enabled;
extern float g_name_scale; // 默认 1.875=游戏公式系数，调大/调小即改该系数

// 日志系统
extern char g_log_text[262144];
void AppendLog(const char* fmt, ...);
void ClearLog();

enum ThemeType {
    Theme_Classic = 0,
    Theme_Light = 1,
    Theme_Pink = 2,
    Theme_LiquidGlass = 3,
    Theme_MusicUI = 4
};
