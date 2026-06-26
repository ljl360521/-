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
#include <fcntl.h>

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
    // 初始化 (无参版本) — 通过 /proc/self/cmdline 读取包名, 构造私有目录路径
    // 这是最可靠的方式, 不依赖 JNI/Activity/类加载器, 任何时候都能用
    //
    // 进程名 (cmdline 第一项) 就是 Android 应用包名, 例如 com.ztgame.bobbeta
    // 然后构造外部存储私有目录: /storage/emulated/0/Android/data/<包名>/files/
    // ---------------------------------------------------------------------
    void Init() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_initialized) return;

        APPLOG_LOGI("AppLog::Init() — 通过 /proc/self/cmdline 识别包名");

        // 步骤 1: 从 /proc/self/cmdline 读包名 (进程名 = 包名)
        m_packageName = ReadCmdlinePackage();
        if (m_packageName.empty()) {
            APPLOG_LOGI("AppLog: /proc/self/cmdline 读取失败, 初始化失败");
            LogInternal(APP_LOG_ERROR, "AppLog", "初始化失败: 无法读取 /proc/self/cmdline");
            return;
        }
        APPLOG_LOGI("AppLog: 识别到包名 = %s", m_packageName.c_str());

        // 步骤 2: 尝试外部存储私有目录 (用户可通过文件管理器访问)
        //   /storage/emulated/0/Android/data/<包名>/files/
        std::string extDir = "/storage/emulated/0/Android/data/" + m_packageName + "/files";
        bool extOk = EnsureDir(extDir);
        if (extOk && TestWritable(extDir)) {
            m_filesDir = extDir;
            m_exportPath = extDir + "/esp_log.txt";
            m_initialized = true;
            APPLOG_LOGI("AppLog: 使用外部存储私有目录: %s", m_filesDir.c_str());
            LogInternal(APP_LOG_INFO, "AppLog",
                "日志系统初始化成功 [cmdline], 包名=" + m_packageName +
                ", 路径=" + m_exportPath);
            return;
        }

        // 步骤 3: 外部存储不可用, 回退到内部存储私有目录 (应用总有权限)
        //   /data/data/<包名>/files/  或  /data/user/0/<包名>/files/
        std::string intDir = "/data/data/" + m_packageName + "/files";
        bool intOk = EnsureDir(intDir);
        if (!intOk) {
            // 某些设备用 /data/user/0/
            intDir = "/data/user/0/" + m_packageName + "/files";
            intOk = EnsureDir(intDir);
        }
        if (intOk && TestWritable(intDir)) {
            m_filesDir = intDir;
            m_exportPath = intDir + "/esp_log.txt";
            m_initialized = true;
            APPLOG_LOGI("AppLog: 使用内部存储私有目录: %s", m_filesDir.c_str());
            LogInternal(APP_LOG_INFO, "AppLog",
                "日志系统初始化成功 [内部存储], 包名=" + m_packageName +
                ", 路径=" + m_exportPath);
            return;
        }

        // 步骤 4: 都失败 — 记录错误, 但仍标记为已初始化避免反复重试
        // 用 /tmp 作为最后兜底 (至少能导出)
        m_filesDir = "/data/local/tmp";
        m_exportPath = m_filesDir + "/esp_log_" + m_packageName + ".txt";
        m_initialized = true;
        APPLOG_LOGI("AppLog: 私有目录均不可写, 兜底使用: %s", m_exportPath.c_str());
        LogInternal(APP_LOG_ERROR, "AppLog",
            "警告: 外部/内部私有目录均不可写, 兜底路径=" + m_exportPath +
            ", 包名=" + m_packageName);
    }

    // ---------------------------------------------------------------------
    // 初始化 (带 Activity 版本) — 兼容旧接口, 内部调用无参版本
    // 反射方式作为补充, 如果无参版本已成功则跳过
    // ---------------------------------------------------------------------
    void Init(JNIEnv* env, jobject activity) {
        // 先尝试无参版本 (最可靠)
        if (!m_initialized) {
            Init();
        }
        // 如果无参版本已成功, 不再重复 (反射方式可能因 Activity 时机问题失败)
        // 保留此接口仅为兼容已有调用代码
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

    // 从 /proc/self/cmdline 读包名 (进程名 = 包名)
    // cmdline 是 \0 分隔的参数列表, 第一个参数就是进程名
    // 对于 Android 应用, 进程名默认等于包名 (除非 AndroidManifest.xml 配置了 android:process)
    static std::string ReadCmdlinePackage() {
        // 方式 1: /proc/self/cmdline
        int fd = open("/proc/self/cmdline", O_RDONLY);
        if (fd >= 0) {
            char buf[256] = {0};
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            close(fd);
            if (n > 0 && buf[0] != '\0') {
                std::string pkg(buf);  // 遇到第一个 \0 截断
                // 过滤掉包含 ':' 的进程名 (如 com.pkg:remote 这种子进程)
                // 取冒号前的部分作为包名
                size_t colon = pkg.find(':');
                if (colon != std::string::npos) {
                    pkg = pkg.substr(0, colon);
                }
                if (!pkg.empty()) return pkg;
            }
        }
        // 方式 2: /proc/self/comm (进程名, 可能有 15 字符截断)
        fd = open("/proc/self/comm", O_RDONLY);
        if (fd >= 0) {
            char buf[256] = {0};
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            close(fd);
            if (n > 0) {
                // 去掉末尾换行
                while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) {
                    buf[--n] = '\0';
                }
                if (n > 0) return std::string(buf);
            }
        }
        return "";
    }

    // 确保目录存在 (递归创建), 返回目录是否最终存在且可写
    static bool EnsureDir(const std::string& path) {
        if (path.empty()) return false;
        std::string dir;
        size_t pos = 0;
        // 逐级创建 (忽略已存在的错误)
        while ((pos = path.find('/', pos + 1)) != std::string::npos) {
            dir = path.substr(0, pos);
            mkdir(dir.c_str(), 0755);  // 已存在会返回 EEXIST, 忽略
        }
        mkdir(path.c_str(), 0755);
        // 检查最终目录是否真的存在
        struct stat st;
        return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }

    // 测试目录是否可写 (创建临时文件测试)
    static bool TestWritable(const std::string& dir) {
        if (dir.empty()) return false;
        std::string testFile = dir + "/.esp_log_wtest";
        FILE* fp = fopen(testFile.c_str(), "w");
        if (!fp) return false;
        fputs("t", fp);
        fclose(fp);
        unlink(testFile.c_str());  // 删除测试文件
        return true;
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
