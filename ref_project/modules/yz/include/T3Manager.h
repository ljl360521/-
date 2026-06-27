#pragma once

#include <string>
#include <functional>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <fstream>
#include <sys/stat.h>
#include "t3data.h"

using json = nlohmann::json;

/**
 * T3验证事件类型枚举
 */
enum class T3EventType {
    LOGIN_SUCCESS,
    LOGIN_FAILED,
    VERSION_CHECK_SUCCESS,
    VERSION_CHECK_FAILED,
    VERSION_OUTDATED,
    NOTICE_FETCH_SUCCESS,
    NOTICE_FETCH_FAILED,
    VALUE_FETCH_SUCCESS,
    VALUE_FETCH_FAILED,
    HEARTBEAT_SUCCESS,
    HEARTBEAT_FAILED,
    ERROR_OCCURRED,
    AUTO_LOGIN_STARTED,
    AUTO_LOGIN_NO_KAMI
};

/**
 * 版本检查结果结构体
 */
struct VersionCheckResult {
    bool success;
    std::string code;              // 状态码
    std::string ver;               // 最新版本字符串 (如 "1.0.0")
    int version;                   // 最新版本号 (如 1000)
    std::string uplog;             // 更新公告
    std::string upurl;             // 更新地址
    long long time;                // 服务器时间戳
    std::string message;           // 错误信息
    
    // 是否需要更新
    bool isNewerThan(int localVersion) const {
        return success && version > localVersion;
    }
    
    // 是否为最新版本
    bool isLatest(int localVersion) const {
        return success && version == localVersion;
    }
    
    // 是否为较旧版本
    bool isOlder(int localVersion) const {
        return success && version < localVersion;
    }
    
    VersionCheckResult() 
        : success(false), version(0), time(0), message("") {}
};

/**
 * T3验证结果结构体
 */
struct T3Result {
    bool success;
    int code;
    std::string message;
    json data;
    T3Result() : success(false), code(-1) {}
};

/**
 * 卡密状态结构体
 */
struct T3KamiStatus {
    bool success;
    std::string code;              // 状态码
    std::string state;             // 卡密状态
    std::string use;               // 激活状态
    std::string id;                // 卡密ID
    std::string use_time;          // 激活时间
    std::string end_time;          // 到期时间
    std::string line_time;         // 最后在线时间
    std::string line;              // 在线状态
    std::string amount;            // 卡密时长
    int available;                 // 剩余时间（秒）
    long long time;                // 服务器时间戳
    std::string date;              // 服务器时间
    std::string message;           // 错误信息

    T3KamiStatus() 
        : success(false), available(0), time(0), message("") {}
};

/**
 * T3验证管理器类 - 单例模式
 */
class T3Manager {
public:
    using EventCallback = std::function<void(T3EventType, const T3Result&)>;
    using StatusCallback = std::function<void(const std::string&)>;
    
    static constexpr const char* STORAGE_DIR = "/storage/emulated/0/Android/data/com.ztgame.bob";
    static constexpr const char* KAMI_FILE = "kami.txt";
    static constexpr const char* IMEI_FILE = "imei.txt";

    // 获取单例实例
    static T3Manager& getInstance();

    ~T3Manager();

    void setEventCallback(EventCallback callback);
    void setStatusCallback(StatusCallback callback);
    void setCardKami(const std::string& kami);
    void setMachineCode(const std::string& code);
    void setLocalVersion(const std::string& version);
    void setHeartbeatInterval(int seconds);

    T3Result login();
    void loginAsync();
    bool autoLogin();
    void autoLoginAsync();

    T3Result getVersion();
    void getVersionAsync();
    T3Result getNotice();
    void getNoticeAsync();
    
    int getHeartbeatRetryCount() const;
    int getHeartbeatMaxRetries() const;
    
    // 获取变量值 - 支持动态参数和返回文本内容
    std::string getValue(const std::string& valueId, const std::string& valueName);
    void getValueAsync(const std::string& valueId, const std::string& valueName);
    
    /**
     * 获取卡密状态 (同步)
     * @param kami 卡密（可选，默认使用内部保存的卡密）
     * @return T3KamiStatus 卡密状态信息
     */
    T3KamiStatus getKamiStatus(const std::string& kami = "");
    
    /**
     * 获取卡密状态 (异步)
     * @param kami 卡密（可选，默认使用内部保存的卡密）
     */
    void getKamiStatusAsync(const std::string& kami = "");
        /**
     * 检查版本更新 (同步)
     * @param localVersion 本地版本号 (如 1000)
     * @return VersionCheckResult 版本检查结果
     */
    VersionCheckResult checkVersionUpdate(int localVersion);
    
    /**
     * 检查版本更新 (异步)
     * @param localVersion 本地版本号
     */
    void checkVersionUpdateAsync(int localVersion);
    
    /**
     * 获取缓存的版本检查结果
     * @return VersionCheckResult 最后一次检查的结果
     */
    VersionCheckResult getLatestVersionCheckResult();
    
    /**
     * 清除缓存的版本信息
     */
    void clearVersionCache();
    
    bool startHeartbeat();
    void stopHeartbeat();

    bool isLoggedIn() const;
    bool isHeartbeatRunning() const;
    std::string getCardKami() const;
    std::string getMachineCode() const;
    std::string getExpireTime() const;
    std::string getLatestVersion() const;
    bool hasSavedKami() const;
    void clearSavedKami();

private:
    std::string card_kami;
    std::string heartbeat_code;
    std::string machine_code;
    std::string local_version;
    std::string latest_version;
    std::string expire_time;
    // 心跳超时重试配置
    static constexpr int HEARTBEAT_MAX_RETRIES = 5;           // 最大重试次数
    static constexpr int HEARTBEAT_RETRY_INTERVAL = 5;        // 重试间隔（秒）
    static constexpr int HEARTBEAT_TIMEOUT = 10;              // 单次请求超时时间（秒）
    
    int heartbeat_retry_count;                                 // 当前重试次数
    bool is_logged_in;
    bool is_running_heartbeat;
    std::atomic<bool> stop_heartbeat;
    int heartbeat_interval_seconds;
    std::unique_ptr<std::thread> heartbeat_thread;

    EventCallback event_callback;
    StatusCallback status_callback;
    
    VersionCheckResult cached_version_result;  // 缓存的版本检查结果
    std::mutex version_result_mutex;           // 版本检查结果互斥锁
    
    // 私有构造函数
    T3Manager();
    
    // 禁止拷贝和移动
    T3Manager(const T3Manager&) = delete;
    T3Manager& operator=(const T3Manager&) = delete;
    T3Manager(T3Manager&&) = delete;
    T3Manager& operator=(T3Manager&&) = delete;

    bool initStorageDir();
    std::string loadKamiFromFile();
    bool saveKamiToFile(const std::string& kami);
    std::string loadImeiFromFile();
    bool saveImeiToFile(const std::string& imei);
    void initMachineCode();
    void heartbeatThread();
    T3Result executeRequestInternal(const std::string& apiPath, 
                                    const std::map<std::string, std::string>& params,
                                    T3DATA& outT3Data);
    void fireEvent(T3EventType eventType, const T3Result& result);
    void updateStatus(const std::string& status);
    T3Result executeRequest(const std::string& apiPath, const std::map<std::string, std::string>& params);
    bool verifyResponse(T3DATA& t3data);
    T3Result heartbeat();
};