#include "配置初始化.h"
#include "全局状态.h"
#include "res/Config.h"
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <android/log.h>
#include <jni.h>
#include "输入法桥接.h"

void XmlConfigCompileProbe() {
    // 编译期/链接期验证：确保旧项目 ConfigManager + pugixml 已成功接入。
    // 不设置路径、不读写文件、不改变业务行为。
    if (false) {
        std::string value = ConfigManager::GetString("probe", "");
        (void)value;
    }
}

bool MkdirRecursive(const char* path) {
    if (!path || !*path) return false;

    char tmp[PATH_MAX];
    int written = snprintf(tmp, sizeof(tmp), "%s", path);
    if (written < 0 || written >= (int)sizeof(tmp)) return false;

    size_t len = strlen(tmp);
    if (len == 0) return false;
    if (tmp[len - 1] == '/') tmp[len - 1] = 0;

    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, 0755) && errno != EEXIST) return false;
            *p = '/';
        }
    }
    return !mkdir(tmp, 0755) || errno == EEXIST;
}

bool ParseConfigFloat(const std::string& value, float& out) {
    if (value.empty()) return false;
    char* end = nullptr;
    errno = 0;
    float v = strtof(value.c_str(), &end);
    if (errno != 0 || end == value.c_str() || *end != '\0') return false;
    out = v;
    return true;
}

void InitAppConfigOldStyle(const char* configDir) {
    if (!configDir || !*configDir) return;

    std::string configPath = std::string(configDir) + "/天天开心.xml";
    ConfigManager::SetConfigPath(configPath);

    if (!g_config_registered) {
        g_config_registered = true;
        ConfigManager::RegisterBool("显示另一个窗口", &g_show_another_window);
        ConfigManager::RegisterBool("显示日志窗口", &g_show_log_window);
        ConfigManager::RegisterFloat("界面缩放", &g_ui_font_scale);
        ConfigManager::RegisterBool("音量键调UI大小", &g_volume_key_ui_scale);
        ConfigManager::RegisterInt("当前标签页", &g_main_tab_index);
        ConfigManager::RegisterInt("主题索引", &g_current_theme_index);
        // 旧项目自动登录配置字段
        ConfigManager::RegisterBool("卡密验证通过", &g_kami_saved_valid);
        ConfigManager::RegisterString("保存的卡密", &g_saved_kami);
    }

    // 旧项目逻辑：先 Register，再 Load；首次没有文件时 Save 生成默认配置。
    ConfigManager::Load();
    g_app_config_ready = ConfigManager::Save();
}

bool GetCurrentAppExternalFilesDir(std::string& outDir) {
    outDir.clear();
    if (!ImeEnsureActivity() || !g_ime_activity || !g_ime_jvm) return false;

    bool attached = false;
    JNIEnv* env = ImeGetEnv(&attached);
    if (!env) return false;

    jclass activityClass = env->GetObjectClass(g_ime_activity);
    if (!activityClass) {
        ImeClearException(env);
        if (attached) g_ime_jvm->DetachCurrentThread();
        return false;
    }

    jmethodID getExternalFilesDir = env->GetMethodID(activityClass, "getExternalFilesDir", "(Ljava/lang/String;)Ljava/io/File;");
    if (!getExternalFilesDir) {
        ImeClearException(env);
        env->DeleteLocalRef(activityClass);
        if (attached) g_ime_jvm->DetachCurrentThread();
        return false;
    }

    jobject fileObj = env->CallObjectMethod(g_ime_activity, getExternalFilesDir, nullptr);
    ImeClearException(env);
    if (!fileObj) {
        env->DeleteLocalRef(activityClass);
        if (attached) g_ime_jvm->DetachCurrentThread();
        return false;
    }

    jclass fileClass = env->GetObjectClass(fileObj);
    jmethodID getAbsolutePath = fileClass ? env->GetMethodID(fileClass, "getAbsolutePath", "()Ljava/lang/String;") : nullptr;
    jstring pathString = getAbsolutePath ? (jstring)env->CallObjectMethod(fileObj, getAbsolutePath) : nullptr;
    ImeClearException(env);

    if (pathString) {
        const char* chars = env->GetStringUTFChars(pathString, nullptr);
        if (chars) {
            outDir = chars;
            env->ReleaseStringUTFChars(pathString, chars);
        }
        env->DeleteLocalRef(pathString);
    }

    if (fileClass) env->DeleteLocalRef(fileClass);
    env->DeleteLocalRef(fileObj);
    env->DeleteLocalRef(activityClass);
    if (attached) g_ime_jvm->DetachCurrentThread();
    return !outDir.empty();
}

void InitAppConfigOnce() {
    if (g_app_config_ready) return;

    std::string externalDir;
    if (!GetCurrentAppExternalFilesDir(externalDir)) return;

    g_config_base_dir = externalDir + "/句号的辅助";
    if (!MkdirRecursive(g_config_base_dir.c_str())) return;
    InitAppConfigOldStyle(g_config_base_dir.c_str());
}





void SaveAppConfigNow()
{
    if (!g_app_config_ready) InitAppConfigOnce();
    if (g_app_config_ready) ConfigManager::Save();
}
