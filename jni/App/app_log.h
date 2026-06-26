// =============================================================================
// app_log.h — 应用日志系统
//
// 功能:
// 1. 自动识别注入的应用私有目录 (/storage/emulated/0/Android/data/<包名>/files/)
//    无需写死包名, 适配任何被注入的应用
// 2. 内存日志缓冲区 (供 UI 实时显示)
// 3. 同时输出到 logcat + 内存缓冲
// 4. 导出日志到私有目录的文件
//
// 用法:
//   AppLog::Init(env, activity);   // 初始化 (获取私有目录)
//   APP_LOGI("...");               // 记录日志 (自动进内存缓冲 + logcat)
//   AppLog::GetLines();            // 获取所有日志行 (供 UI 显示)
//   AppLog::ExportToFile();        // 导出到文件, 返回文件路径
// =============================================================================

#pragma once

#include <jni.h>
#include <android/log.h>
#include <string>
#include <vector>
#include <mutex>
#include <deque>
#include <ctime>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>

#define APPLOG_TAG "AppLog"
#define APPLOG_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  APPLOG_TAG, __VA_ARGS__)

// 日志最大缓冲行数 (超出后丢弃最旧的)
#define APP_LOG_MAX_LINES 2000

// 日志级别
enum AppLogLevel {
    APP_LOG_DEBUG = 0,
    APP_LOG_INFO  = 1,
    APP_LOG_WARN  = 2,
    APP_LOG_ERROR = 3
};

// 单条日志
struct AppLogEntry {
    AppLogLevel level;
    std::string timestamp;   // HH:MM:SS
    std::string tag;         // 来源标签
    std::string message;     // 内容
};

// =============================================================================
// AppLog 单例
// =============================================================================
class AppLog {
public:
    static AppLog& Instance() {
        static AppLog inst;
        return inst;
    }

    // ---------------------------------------------------------------------
    // 初始化 — 通过 Java 反射获取应用的 filesDir (自动识别包名)
    //   filesDir 形如 /storage/emulated/0/Android/data/com.ztgame.bobbeta/files
    //   或 /data/data/com.ztgame.bobbeta/files (内部存储)
    // ---------------------------------------------------------------------
    void Init(JNIEnv* env, jobject activity) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_initialized) return;

        m_filesDir = GetFilesDir(env, activity);
        if (!m_filesDir.empty()) {
            // 提取包名 (从路径里)
            // 路径形如 .../Android/data/<包名>/files
            size_t pos = m_filesDir.find("Android/data/");
            if (pos != std::string::npos) {
                size_t pkgStart = pos + strlen("Android/data/");
                size_t pkgEnd = m_filesDir.find('/', pkgStart);
                if (pkgEnd != std::string::npos) {
                    m_packageName = m_filesDir.substr(pkgStart, pkgEnd - pkgStart);
                }
            }
            m_exportPath = m_filesDir + "/esp_log.txt";
            m_initialized = true;
            APPLOG_LOGI("AppLog 初始化: filesDir=%s pkg=%s export=%s",
                m_filesDir.c_str(), m_packageName.c_str(), m_exportPath.c_str());
            // 记录一条初始日志
            LogInternal(APP_LOG_INFO, "AppLog", "日志系统初始化, 包名=" + m_packageName + ", 导出路径=" + m_exportPath);
        } else {
            // 兜底: 用 /proc/self/cmdline 读包名
            m_packageName = ReadCmdlinePackage();
            if (!m_packageName.empty()) {
                m_filesDir = "/storage/emulated/0/Android/data/" + m_packageName + "/files";
                EnsureDir(m_filesDir);
                m_exportPath = m_filesDir + "/esp_log.txt";
                m_initialized = true;
                APPLOG_LOGI("AppLog 初始化(cmdline): pkg=%s export=%s",
                    m_packageName.c_str(), m_exportPath.c_str());
                LogInternal(APP_LOG_INFO, "AppLog", "日志系统初始化(cmdline), 包名=" + m_packageName);
            } else {
                APPLOG_LOGI("AppLog 初始化失败: 无法获取 filesDir 和 cmdline");
            }
        }
    }

    // ---------------------------------------------------------------------
    // 记录日志 (线程安全)
    // ---------------------------------------------------------------------
    void Log(AppLogLevel level, const char* tag, const char* fmt, ...) {
        char buf[2048];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        // 同时输出到 logcat
        int prio = (level == APP_LOG_DEBUG ? ANDROID_LOG_DEBUG :
                    level == APP_LOG_INFO  ? ANDROID_LOG_INFO  :
                    level == APP_LOG_WARN  ? ANDROID_LOG_WARN  : ANDROID_LOG_ERROR);
        __android_log_print(prio, tag, "%s", buf);

        std::lock_guard<std::mutex> lock(m_mutex);
        LogInternal(level, tag, buf);
    }

    // ---------------------------------------------------------------------
    // 获取所有日志行 (供 UI 显示) — 返回拷贝
    // ---------------------------------------------------------------------
    std::vector<AppLogEntry> GetLines() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return std::vector<AppLogEntry>(m_lines.begin(), m_lines.end());
    }

    // ---------------------------------------------------------------------
    // 获取日志行数
    // ---------------------------------------------------------------------
    size_t GetLineCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_lines.size();
    }

    // ---------------------------------------------------------------------
    // 导出到文件 — 返回文件路径 (失败返回空串)
    // ---------------------------------------------------------------------
    std::string ExportToFile() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_exportPath.empty()) {
            return "";
        }

        // 确保目录存在
        EnsureDir(m_filesDir);

        std::ofstream ofs(m_exportPath, std::ios::out | std::ios::trunc);
        if (!ofs.is_open()) {
            return "";
        }

        ofs << "========================================\n";
        ofs << "ESP 日志导出\n";
        ofs << "包名: " << m_packageName << "\n";
        ofs << "导出时间: " << NowString() << "\n";
        ofs << "日志条数: " << m_lines.size() << "\n";
        ofs << "========================================\n\n";

        for (const auto& e : m_lines) {
            ofs << "[" << e.timestamp << "] "
                << "[" << LevelStr(e.level) << "] "
                << "[" << e.tag << "] "
                << e.message << "\n";
        }
        ofs.flush();
        ofs.close();
        return m_exportPath;
    }

    // ---------------------------------------------------------------------
    // 清空内存日志
    // ---------------------------------------------------------------------
    void Clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lines.clear();
    }

    // ---------------------------------------------------------------------
    // 获取导出路径
    // ---------------------------------------------------------------------
    const std::string& GetExportPath() const { return m_exportPath; }
    const std::string& GetPackageName() const { return m_packageName; }
    bool IsInitialized() const { return m_initialized; }

private:
    AppLog() : m_initialized(false) {}

    // 内部记录 (调用者需已持锁)
    void LogInternal(AppLogLevel level, const std::string& tag, const std::string& msg) {
        AppLogEntry e;
        e.level = level;
        e.tag = tag;
        e.message = msg;
        e.timestamp = NowTimeStr();
        m_lines.push_back(std::move(e));
        // 超出上限丢弃最旧的
        while (m_lines.size() > APP_LOG_MAX_LINES) {
            m_lines.pop_front();
        }
    }

    // 通过 Java 反射获取 filesDir
    static std::string GetFilesDir(JNIEnv* env, jobject activity) {
        if (!env || !activity) return "";
        jclass ctxClass = env->FindClass("android/content/Context");
        if (!ctxClass) return "";
        jmethodID getFilesDir = env->GetMethodID(ctxClass, "getFilesDir", "()Ljava/io/File;");
        if (!getFilesDir) { env->DeleteLocalRef(ctxClass); return ""; }
        jobject fileObj = env->CallObjectMethod(activity, getFilesDir);
        if (!fileObj) { env->DeleteLocalRef(ctxClass); return ""; }

        jclass fileClass = env->FindClass("java/io/File");
        jmethodID getAbsPath = env->GetMethodID(fileClass, "getAbsolutePath", "()Ljava/lang/String;");
        jstring pathStr = (jstring)env->CallObjectMethod(fileObj, getAbsPath);
        std::string result;
        if (pathStr) {
            const char* cstr = env->GetStringUTFChars(pathStr, nullptr);
            if (cstr) { result = cstr; env->ReleaseStringUTFChars(pathStr, cstr); }
            env->DeleteLocalRef(pathStr);
        }
        env->DeleteLocalRef(fileObj);
        env->DeleteLocalRef(fileClass);
        env->DeleteLocalRef(ctxClass);
        return result;
    }

    // 从 /proc/self/cmdline 读包名 (进程名)
    static std::string ReadCmdlinePackage() {
        FILE* fp = fopen("/proc/self/cmdline", "r");
        if (!fp) return "";
        char buf[256] = {0};
        size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
        fclose(fp);
        if (n == 0) return "";
        // cmdline 以 \0 分隔, 第一个就是进程名(包名)
        return std::string(buf);
    }

    // 确保目录存在 (递归创建)
    static void EnsureDir(const std::string& path) {
        if (path.empty()) return;
        std::string dir;
        size_t pos = 0;
        while ((pos = path.find('/', pos + 1)) != std::string::npos) {
            dir = path.substr(0, pos);
            mkdir(dir.c_str(), 0755);
        }
        mkdir(path.c_str(), 0755);
    }

    static std::string NowTimeStr() {
        time_t now = time(nullptr);
        struct tm* t = localtime(&now);
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
        return buf;
    }

    static std::string NowString() {
        time_t now = time(nullptr);
        struct tm* t = localtime(&now);
        char buf[32];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec);
        return buf;
    }

    static const char* LevelStr(AppLogLevel l) {
        switch (l) {
            case APP_LOG_DEBUG: return "DEBUG";
            case APP_LOG_INFO:  return "INFO";
            case APP_LOG_WARN:  return "WARN";
            case APP_LOG_ERROR: return "ERROR";
        }
        return "?";
    }

    bool m_initialized;
    std::string m_filesDir;      // /storage/emulated/0/Android/data/<包名>/files
    std::string m_packageName;   // 包名
    std::string m_exportPath;    // 导出文件路径

    mutable std::mutex m_mutex;
    std::deque<AppLogEntry> m_lines;  // 环形缓冲 (FIFO, 超限丢最旧)
};

// =============================================================================
// 便捷日志宏 — 自动记录到内存缓冲 + logcat
// =============================================================================
#define APP_LOGD(tag, ...) AppLog::Instance().Log(APP_LOG_DEBUG, tag, __VA_ARGS__)
#define APP_LOGI(tag, ...) AppLog::Instance().Log(APP_LOG_INFO,  tag, __VA_ARGS__)
#define APP_LOGW(tag, ...) AppLog::Instance().Log(APP_LOG_WARN,  tag, __VA_ARGS__)
#define APP_LOGE(tag, ...) AppLog::Instance().Log(APP_LOG_ERROR, tag, __VA_ARGS__)
