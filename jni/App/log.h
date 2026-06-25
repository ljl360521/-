//
// Created by Administrator on 2021-06-28.
//

#ifndef NDK_LOG_H
#define NDK_LOG_H

#include <android/log.h>
#include <string>
#include <fstream>
#include <iostream>
#include <sstream>
#include <ctime>
#include <iomanip>
#include <sys/stat.h>

using namespace std;

#define NDK_LOG true
#define LOG_TO_FILE true  // 是否输出到文件
#define LOG_FILE_PATH "/storage/emulated/0/Android/data/com.ztgame.bob/ndk.log"  // 日志文件路径

#define TAG "NDK" // 这个是自定义的LOG的标识

// 创建目录
static bool createDirectory(const std::string& path) {
    size_t pos = 0;
    std::string dir;
    
    if (path[path.length() - 1] != '/') {
        // 如果路径不以'/'结尾，找到最后一个'/'的位置
        pos = path.find_last_of('/');
        dir = path.substr(0, pos);
    } else {
        dir = path;
    }
    
    // 逐级创建目录
    pos = 0;
    while ((pos = dir.find_first_of('/', pos + 1)) != std::string::npos) {
        std::string subdir = dir.substr(0, pos);
        if (mkdir(subdir.c_str(), 0755) != 0 && errno != EEXIST) {
            return false;
        }
    }
    
    // 创建最后一级目录
    if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
        return false;
    }
    
    return true;
}

// 获取当前时间字符串
static std::string getCurrentTime() {
    std::time_t now = std::time(nullptr);
    std::tm* localTime = std::localtime(&now);
    
    std::ostringstream oss;
    oss << std::put_time(localTime, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// 获取日志级别名称
static std::string getLogLevelName(int level) {
    switch (level) {
        case ANDROID_LOG_DEBUG: return "DEBUG";
        case ANDROID_LOG_INFO: return "INFO";
        case ANDROID_LOG_WARN: return "WARN";
        case ANDROID_LOG_ERROR: return "ERROR";
        case ANDROID_LOG_FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

// 写入日志到文件
static void writeToLogFile(int level, const char* tag, const char* message) {
    if (!LOG_TO_FILE) return;
    
    static bool initDone = false;
    static std::ofstream logFile;
    static pthread_mutex_t logMutex = PTHREAD_MUTEX_INITIALIZER;
    
    pthread_mutex_lock(&logMutex);
    
    try {
        // 第一次使用时初始化
        if (!initDone) {
            // 创建日志目录
            std::string logPath = LOG_FILE_PATH;
            size_t lastSlash = logPath.find_last_of('/');
            if (lastSlash != std::string::npos) {
                std::string dirPath = logPath.substr(0, lastSlash);
                createDirectory(dirPath);
            }
            
            // 打开日志文件（追加模式）
            logFile.open(logPath, std::ios::app | std::ios::out);
            if (logFile.is_open()) {
                initDone = true;
                logFile << "=== Log Started at " << getCurrentTime() << " ===" << std::endl;
                logFile.flush();
            }
        }
        
        // 写入日志
        if (logFile.is_open()) {
            logFile << "[" << getCurrentTime() << "] "
                    << "[" << getLogLevelName(level) << "] "
                    << "[" << tag << "] "
                    << message << std::endl;
            logFile.flush();  // 立即刷新缓冲区
        }
    } catch (...) {
        // 忽略文件写入异常，避免影响程序正常运行
    }
    
    pthread_mutex_unlock(&logMutex);
}

// 带文件输出的日志宏
#if NDK_LOG
#define LOGD(...) do { \
    __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__); \
    if (LOG_TO_FILE) { \
        char buffer[1024]; \
        snprintf(buffer, sizeof(buffer), __VA_ARGS__); \
        writeToLogFile(ANDROID_LOG_DEBUG, TAG, buffer); \
    } \
} while(0)

#define LOGI(...) do { \
    __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__); \
    if (LOG_TO_FILE) { \
        char buffer[1024]; \
        snprintf(buffer, sizeof(buffer), __VA_ARGS__); \
        writeToLogFile(ANDROID_LOG_INFO, TAG, buffer); \
    } \
} while(0)

#define LOGW(...) do { \
    __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__); \
    if (LOG_TO_FILE) { \
        char buffer[1024]; \
        snprintf(buffer, sizeof(buffer), __VA_ARGS__); \
        writeToLogFile(ANDROID_LOG_WARN, TAG, buffer); \
    } \
} while(0)

#define LOGE(...) do { \
    __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__); \
    if (LOG_TO_FILE) { \
        char buffer[1024]; \
        snprintf(buffer, sizeof(buffer), __VA_ARGS__); \
        writeToLogFile(ANDROID_LOG_ERROR, TAG, buffer); \
    } \
} while(0)

#define LOGF(...) do { \
    __android_log_print(ANDROID_LOG_FATAL, TAG, __VA_ARGS__); \
    if (LOG_TO_FILE) { \
        char buffer[1024]; \
        snprintf(buffer, sizeof(buffer), __VA_ARGS__); \
        writeToLogFile(ANDROID_LOG_FATAL, TAG, buffer); \
    } \
} while(0)

#else
#define LOGD(...)
#define LOGI(...)
#define LOGW(...)
#define LOGE(...)
#define LOGF(...)
#endif

// 额外的工具函数
namespace LogUtils {
    
    // 设置自定义日志路径
    static void setLogFilePath(const std::string& path) {
        // 注意：这个函数需要在程序开始时调用
        // 实际实现需要修改 LOG_FILE_PATH，这里只是示例
    }
    
    // 手动刷新日志缓冲区
    static void flushLog() {
        if (LOG_TO_FILE) {
            // 强制刷新文件缓冲区
            std::ofstream logFile(LOG_FILE_PATH, std::ios::app);
            if (logFile.is_open()) {
                logFile.flush();
            }
        }
    }
    
    // 获取日志文件大小
    static long getLogFileSize() {
        struct stat stat_buf;
        if (stat(LOG_FILE_PATH, &stat_buf) == 0) {
            return stat_buf.st_size;
        }
        return -1;
    }
    
    // 清理过期的日志文件（按大小）
    static void cleanupLogFile(long maxSize = 10 * 1024 * 1024) { // 默认10MB
        long currentSize = getLogFileSize();
        if (currentSize > maxSize) {
            // 简单的清理策略：重命名当前日志文件，创建新的
            std::string oldPath = std::string(LOG_FILE_PATH) + ".old";
            rename(LOG_FILE_PATH, oldPath.c_str());
        }
    }
}

#endif //NDK_LOG_H
