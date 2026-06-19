#include "logger.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>    
#include <chrono> 
#include <sys/stat.h>
  
// 日志存储 
static LogEntry g_logEntries[MAX_LOG_ENTRIES];
static std::atomic<int> g_logCount(0);
static std::atomic<int> g_logWriteIndex(0);
static std::mutex g_logMutex;

// 日志文件路径
const char* LOG_FILE_PATH = "/storage/emulated/0/Android/data/com.pi.czrxdfirst/logs.txt"; 

static FILE* g_logFile = nullptr;
static bool g_fileInitialized = false; 

// LogEntry 构造函数
LogEntry::LogEntry() 
    : timestamp(0), level(LOG_LEVEL_INFO) {
    message[0] = '\0';
}

// 获取当前时间戳（毫秒）
static long long getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

// 确保目录存在
static bool ensureDirectoryExists(const char* filePath) {
    char dirPath[256];
    strncpy(dirPath, filePath, sizeof(dirPath) - 1);
    dirPath[sizeof(dirPath) - 1] = '\0';
    
    // 获取目录部分
    char* lastSlash = strrchr(dirPath, '/');
    if (lastSlash) {
        *lastSlash = '\0';
    }
    
    // 创建目录（如果不存在）
    mkdir(dirPath, 0755);
    return true;
}

// 初始化日志文件
static void initializeLogFile() {
    if (g_fileInitialized) {
        return;
    }
    
    ensureDirectoryExists(LOG_FILE_PATH);
    
    // 以追加模式打开文件
    g_logFile = fopen(LOG_FILE_PATH, "a");
    if (g_logFile) {
        // 写入日志开始标记
        fprintf(g_logFile, "\n========== Log Started at %lld ==========\n", getCurrentTimestamp());
        fflush(g_logFile);
    }
    
    g_fileInitialized = true;
}

// 将日志写入文件
static void writeLogToFile(LogLevel level, const char* message, long long timestamp) {
    if (!g_fileInitialized) {
        initializeLogFile();
    }
    
    if (!g_logFile) {
        return;
    }
    
    // 格式化时间戳
    time_t timeInSeconds = timestamp / 1000;
    int milliseconds = timestamp % 1000;
    struct tm* timeinfo = localtime(&timeInSeconds);
    char timeStr[32];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
    
    // 获取级别名称
    const char* levelName;
    switch (level) {
        case LOG_LEVEL_INFO:  levelName = "INFO"; break;
        case LOG_LEVEL_ERROR: levelName = "ERROR"; break;
        case LOG_LEVEL_WARN:  levelName = "WARN"; break;
        case LOG_LEVEL_DEBUG: levelName = "DEBUG"; break;
        default: levelName = "UNKNOWN"; break;
    }
    
    // 写入文件
    fprintf(g_logFile, "[%s.%03d] [%s] %s\n", timeStr, milliseconds, levelName, message);
    fflush(g_logFile);
}

// 添加日志
void addLog(LogLevel level, const char* format, ...) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    
    // 获取当前写入索引
    int index = g_logWriteIndex.load() % MAX_LOG_ENTRIES;
    
    // 创建日志条目
    LogEntry& entry = g_logEntries[index];
    entry.timestamp = getCurrentTimestamp();
    entry.level = level;
    
    // 格式化消息
    va_list args;
    va_start(args, format);
    vsnprintf(entry.message, MAX_LOG_MESSAGE_SIZE, format, args);
    va_end(args);
    
    // 写入文件 
    writeLogToFile(level, entry.message, entry.timestamp);
    
    // 更新索引和计数
    g_logWriteIndex.fetch_add(1);
    if (g_logCount.load() < MAX_LOG_ENTRIES) {
        g_logCount.fetch_add(1);
    }
}

// 获取日志数量
int getLogCount() {
    int count = g_logCount.load();
    return (count < MAX_LOG_ENTRIES) ? count : MAX_LOG_ENTRIES;
}

// 获取日志条目
LogEntry* getLogEntry(int index) {
    if (index < 0 || index >= getLogCount()) {
        return nullptr;
    }
    
    // 计算实际索引（环形缓冲区）
    int totalWritten = g_logWriteIndex.load();
    int actualIndex;
    
    if (totalWritten < MAX_LOG_ENTRIES) {
        // 缓冲区还没满，直接使用索引
        actualIndex = index;
    } else {
        // 缓冲区已满，计算环形索引
        int startIndex = totalWritten % MAX_LOG_ENTRIES;
        actualIndex = (startIndex + index) % MAX_LOG_ENTRIES;
    }
    
    return &g_logEntries[actualIndex];
}

// 清空日志
void clearLogs() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    
    g_logCount.store(0);
    g_logWriteIndex.store(0);
    
    // 清空所有日志条目
    for (int i = 0; i < MAX_LOG_ENTRIES; i++) {
        g_logEntries[i].timestamp = 0;
        g_logEntries[i].level = LOG_LEVEL_INFO;
        g_logEntries[i].message[0] = '\0';
    }
}

// 关闭日志文件
void closeLogFile() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    
    if (g_logFile) {
        fprintf(g_logFile, "========== Log Ended ==========\n");
        fclose(g_logFile);
        g_logFile = nullptr;
        g_fileInitialized = false;
    }
}

// 获取格式化的日志字符串
char* getFormattedLogs(int maxLines) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    
    int logCount = getLogCount();
    if (maxLines > 0 && maxLines < logCount) {
        logCount = maxLines;
    }
    
    // 计算所需缓冲区大小
    size_t bufferSize = logCount * (MAX_LOG_MESSAGE_SIZE + 100) + 1;
    char* buffer = (char*)malloc(bufferSize);
    if (!buffer) {
        return nullptr;
    }
    
    buffer[0] = '\0';
    char lineBuffer[MAX_LOG_MESSAGE_SIZE + 100];
    
    // 格式化每一行
    for (int i = 0; i < logCount; i++) {
        LogEntry* entry = getLogEntry(i);
        if (!entry) continue;
        
        // 格式化时间戳
        time_t timeInSeconds = entry->timestamp / 1000;
        int milliseconds = entry->timestamp % 1000;
        struct tm* timeinfo = localtime(&timeInSeconds);
        char timeStr[32];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
        
        // 获取级别名称
        const char* levelName;
        switch (entry->level) {
            case LOG_LEVEL_INFO:  levelName = "INFO"; break;
            case LOG_LEVEL_ERROR: levelName = "ERROR"; break;
            case LOG_LEVEL_WARN:  levelName = "WARN"; break;
            case LOG_LEVEL_DEBUG: levelName = "DEBUG"; break;
            default: levelName = "UNKNOWN"; break;
        }
        
        // 格式化日志行
        snprintf(lineBuffer, sizeof(lineBuffer), 
            "[%s.%03d] [%s] %s\n",
            timeStr, milliseconds, levelName, entry->message);
        
        strcat(buffer, lineBuffer);
    }
    
    return buffer;
}

// 获取最近的N条日志
char* getRecentLogs(int lineCount) {
    return getFormattedLogs(lineCount);
}