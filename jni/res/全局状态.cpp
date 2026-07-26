#include "全局状态.h"
#include <cstring>
#include <android/log.h>

int screenWidth = 0;
int screenHeight = 0;
bool g_Initialized = false;
bool g_imgui_backend_active = false;
ANativeWindow* g_native_window = nullptr;
ImGuiWindow* g_window = NULL;
JavaVM* g_ime_jvm = nullptr;
jobject g_ime_activity = nullptr;

sConfig Config{true};

// ==================== T3 网络验证（轻量接入版） ====================
// 说明：只复用旧项目 t3sdk 底层网络验证，不引入旧项目 AppConfig/日志/android_app，降低融合风险。
const char* T3_LOGIN_CODE     = "555F6E97AA87EACA";
const char* T3_NOTICE_CODE    = "0E3C22A329179374";
const char* T3_VERSION_CODE   = "CF78A3C231FBC7E2";
const char* T3_HEARTBEAT_CODE = "87A6E643DAED8CFF";
const char* T3_APPKEY         = "10d0b15afbcf1091ec4b846601f7bd8e";
const char* T3_RSA_PUBLIC_KEY =
    "-----BEGIN PUBLIC KEY-----\n"
    "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDI3/uSjgloeW0X2bfOKrZeZi2w\n"
    "13MCGBkis7Poj3DCPWg7DZuiHWnGW1LN8u+E0kMf2/ORCauZAh0ap45iGzHEzcAD\n"
    "rXxiB72FqVqwo8VEpcWNHU60uZXVwoZ+hxGm3N5VOWIJB2ED06IefDdwEaV22LDu\n"
    "tJwWvxIeZqiqG2IaPwIDAQAB\n"
    "-----END PUBLIC KEY-----";
const char* LOCAL_VERSION = "1000";

T3Verify* g_t3_verify = nullptr;
std::once_flag g_t3_init_once;
std::atomic<bool> g_t3_ready{false};
std::atomic<bool> g_t3_authed{false};
std::atomic<bool> g_t3_verifying{false};
std::atomic<bool> g_t3_heartbeat_running{false};
std::mutex g_t3_mutex;
std::string g_t3_status = "验证系统未初始化";
std::string g_t3_notice;
std::string g_t3_end_time;
std::string g_t3_statecode;
std::string g_t3_kami;
bool g_kami_saved_valid = false;
std::string g_saved_kami;
bool g_t3_auto_login_tried = false;
bool g_t3_auto_login_running = false;
std::string g_t3_machine_code = "android_inject_device";


// ==================== Android 输入法桥（最终稳定版） ====================
// 关键点：当前 GLES3JNIView.step/MotionEventClick 是 static native，JNI 第二参数不是 View。
// 因此不能用 obj.getContext()，必须用 ActivityThread 反射获取当前 Activity。
jobject g_ime_loader = nullptr;
jobject g_main_dex_loader = nullptr;
bool g_main_dex_loaded = false;
jobject g_current_imgui_class_loader = nullptr;
jobject g_secure_loader = nullptr;
bool g_secure_loaded = false;
bool g_pending_secure_switch = false;
bool g_pending_secure_target = false;
bool g_rendering_secure_surface = false;
EGLDisplay g_secure_egl_display = EGL_NO_DISPLAY;
EGLSurface g_secure_egl_surface = EGL_NO_SURFACE;
EGLContext g_secure_egl_context = EGL_NO_CONTEXT;
ANativeWindow* g_secure_native_window = nullptr;
int g_secure_surface_width = 0;
int g_secure_surface_height = 0;
bool g_ime_loaded = false;
bool g_ime_last_want_text = false;
bool g_ime_last_input_active = false;
bool g_secure_skip_screenshot = false;
int g_main_tab_index = 0;
int g_current_theme_index = 3; // 旧项目默认：液态玻璃
bool g_show_another_window = false;
ImGenieParams g_GenieParams;
bool g_show_log_window = false;
ImVec4 g_clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
float g_ui_font_scale = 1.0f;
bool g_volume_key_ui_scale = false;
jobject g_volume_loader = nullptr;
bool g_volume_loaded = false;
char g_logInput[256] = "";


std::string g_config_base_dir;
bool g_app_config_ready = false;
bool g_config_registered = false;

// 吐球加速
bool g_spitball_accelerate = false;
int g_spitball_speed = 3;

// 三角合球
bool g_triangle_running = false;

// 视野修改
bool g_view_enabled = false;
float g_view_scale = 1.5f;
bool g_view_was_enabled = false;

// 粘合修改
bool g_merge_enabled = false;
float g_merge_value = 0.588f;
bool g_merge_was_enabled = false;

// 排名名字修改
bool g_rename_enabled = false;
bool g_rename_was_enabled = false;
bool g_rename_use_color = true;
ImVec4 g_rename_name_color = ImVec4(1.0f, 0.85f, 0.2f, 1.0f); // 默认金黄
bool g_rename_detailed_rank = false; // 默认粗略显示
bool g_rename_show_self = false;     // 默认不改自己

// 球上名字大小（独立）
bool g_name_scale_enabled = false;
float g_name_scale = 1.875f; // 游戏 TargetNameScale2 公式末尾系数

// 日志系统
char g_log_text[262144] = {0};
static int g_log_len = 0;
static int g_log_line_count = 0;
static const int MAX_LOG_LINES = 2000;
static std::string g_log_file_path;
static bool g_log_save_failed = false;

void AppendLog(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    // 已稳定功能的日志一律丢弃（磁盘里也不会再出现）
    if (strstr(buf, "[名字大小]") || strstr(buf, "[改名]") ||
        strstr(buf, "[IL2CPP]")) {
        return;
    }

    int blen = (int)strlen(buf);

    // 如果缓冲区空间不够，从头删掉旧的行
    if (g_log_len + blen + 2 >= (int)sizeof(g_log_text)) {
        int linesToSkip = 500;
        int pos = 0;
        int skipped = 0;
        while (pos < g_log_len && skipped < linesToSkip) {
            if (g_log_text[pos] == '\n') skipped++;
            pos++;
        }
        if (pos < g_log_len) {
            memmove(g_log_text, g_log_text + pos, g_log_len - pos);
            g_log_len -= pos;
        } else {
            g_log_len = 0;
        }
        g_log_line_count -= skipped;
    }

    if (g_log_len > 0 && g_log_text[g_log_len - 1] != '\n') {
        g_log_text[g_log_len++] = '\n';
    }
    memcpy(g_log_text + g_log_len, buf, blen);
    g_log_len += blen;
    g_log_text[g_log_len] = '\0';
    g_log_line_count++;

    // 路径确定
    if (g_log_file_path.empty() && !g_config_base_dir.empty()) {
        g_log_file_path = g_config_base_dir + "/game_log.txt";
    }

    // 只写内存缓冲区的完整快照（不再 "a" 追加，避免磁盘堆满旧的名字大小日志）
    if (!g_log_file_path.empty()) {
        FILE* f = fopen(g_log_file_path.c_str(), "w");
        if (f) {
            fwrite(g_log_text, 1, (size_t)g_log_len, f);
            fputc('\n', f);
            fflush(f);
            fclose(f);
        }
    }

    __android_log_print(ANDROID_LOG_INFO, "IMGUI_GAME", "%s", buf);
}

void ClearLog() {
    g_log_text[0] = '\0';
    g_log_len = 0;
    g_log_line_count = 0;
    if (g_log_file_path.empty() && !g_config_base_dir.empty()) {
        g_log_file_path = g_config_base_dir + "/game_log.txt";
    }
    if (!g_log_file_path.empty()) {
        FILE* f = fopen(g_log_file_path.c_str(), "w");
        if (f) fclose(f);
    }
}
