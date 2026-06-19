#ifndef LOGGER_H
#define LOGGER_H

#include <chrono>
#include <atomic>
#include <pthread.h>
#include <android/log.h>
#include <cstring>

// 日志系统相关常量
#define MAX_LOG_ENTRIES 1000
#define MAX_LOG_MESSAGE_SIZE 512
#define TAG "超自然"

// 日志级别 
enum LogLevel {
    LOG_LEVEL_INFO = 0,
    LOG_LEVEL_ERROR = 1,
    LOG_LEVEL_WARN = 2, 
    LOG_LEVEL_DEBUG = 3
};

// 日志条目结构体
struct LogEntry {
    long long timestamp;
    int level;
    char message[MAX_LOG_MESSAGE_SIZE];
    
    LogEntry();
};

// 日志系统函数声明
void addLog(LogLevel level, const char* format, ...);
int getLogCount();
LogEntry* getLogEntry(int index);
void clearLogs();
char* getFormattedLogs(int maxLines);

// 日志宏定义
#define LOGI(...) do { \
    __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__); \
    addLog(LOG_LEVEL_INFO, __VA_ARGS__); \
} while(0)

#define LOGE(...) do { \
    __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__); \
    addLog(LOG_LEVEL_ERROR, __VA_ARGS__); \
} while(0)

#define LOGW(...) do { \
    __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__); \
    addLog(LOG_LEVEL_WARN, __VA_ARGS__); \
} while(0)

#define LOGD(...) do { \
    __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__); \
    addLog(LOG_LEVEL_DEBUG, __VA_ARGS__); \
} while(0)

#endif // LOGGER_H