#include "T3Manager.h"
#include "ImeiGenerator.h"
#include "Logger.h"
#include <iostream>
std::string 配置路径 = "/storage/emulated/0/Android/data/com.pi.czrxdfirst";
// 私有构造函数
T3Manager::T3Manager()
    : is_logged_in(false),
      is_running_heartbeat(false),
      heartbeat_interval_seconds(10),
      local_version("1000"),
      heartbeat_retry_count(0)
{
    stop_heartbeat = true;
    initStorageDir();
    initMachineCode();
    LOGI("T3Manager initialized with machine code: %s", machine_code.c_str());
}

// 单例获取方法
T3Manager& T3Manager::getInstance()
{
    static T3Manager instance;
    return instance;
}

T3Manager::~T3Manager()
{
    stopHeartbeat();
    // 不在析构函数中join，因为stopHeartbeat已经处理了
    LOGI("T3Manager destroyed");
}


bool T3Manager::initStorageDir()
{
    if (配置路径.empty()) {
        LOGE("配置路径为空，未设置存储目录");
        return false;
    }
    
    struct stat st;
    if (stat(配置路径.c_str(), &st) != 0) {
        if (mkdir(配置路径.c_str(), 0755) != 0) {
            LOGE("创建存储目录失败: %s", 配置路径.c_str());
            return false;
        }
        LOGI("已创建存储目录: %s", 配置路径.c_str());
    }
    return true;
}


std::string T3Manager::loadKamiFromFile()
{
    std::ifstream file(配置路径 + "/" + KAMI_FILE);
    if (file.is_open()) {
        std::string kami;
        std::getline(file, kami);
        file.close();
        kami.erase(0, kami.find_first_not_of(" \t\n\r"));
        kami.erase(kami.find_last_not_of(" \t\n\r") + 1);
        if (!kami.empty()) {
            LOGI("Loaded kami from file");
            return kami;
        }
    }
    LOGD("No kami file found or empty");
    return "";
}

bool T3Manager::saveKamiToFile(const std::string& kami)
{
    std::ofstream file(配置路径 + "/" + KAMI_FILE);
    if (file.is_open()) {
        file << kami;
        file.close();
        LOGI("Saved kami to file");
        return true;
    }
    LOGE("Failed to save kami to file");
    return false;
}

std::string T3Manager::loadImeiFromFile()
{
    std::ifstream file(配置路径 + "/" + IMEI_FILE);
    if (file.is_open()) {
        std::string imei;
        std::getline(file, imei);
        file.close();
        imei.erase(0, imei.find_first_not_of(" \t\n\r"));
        imei.erase(imei.find_last_not_of(" \t\n\r") + 1);
        if (!imei.empty()) {
            LOGI("Loaded IMEI from file: %s", imei.c_str());
            return imei;
        }
    }
    LOGD("No IMEI file found or empty");
    return "";
}

bool T3Manager::saveImeiToFile(const std::string& imei)
{
    std::ofstream file(配置路径 + "/" + IMEI_FILE);
    if (file.is_open()) {
        file << imei;
        file.close();
        LOGI("Saved IMEI to file: %s", imei.c_str());
        return true;
    }
    LOGE("Failed to save IMEI to file");
    return false;
}

void T3Manager::initMachineCode()
{
    // 始终先生成当前设备的真实机器码
    std::string currentDeviceImei = ImeiGenerator::generateImei();
    
    // 加载已保存的机器码
    std::string savedImei = loadImeiFromFile();
    
    if (savedImei.empty()) {
        // 首次运行
        machine_code = currentDeviceImei;
        saveImeiToFile(machine_code);
        LOGI("Generated new IMEI: %s", machine_code.c_str());
    }
    else if (savedImei != currentDeviceImei) {
        // 检测到 IMEI 不匹配 - 可能是复制的文件
        LOGW("Security: IMEI file mismatch detected!");
        LOGW("  Saved IMEI:  %s", savedImei.c_str());
        LOGW("  Device IMEI: %s", currentDeviceImei.c_str());
        
        // 强制使用当前设备的真实 IMEI
        machine_code = currentDeviceImei;
        saveImeiToFile(machine_code);
        
        // 清除卡密，防止使用复制的凭据
        clearSavedKami();
        
        LOGI("Reset to device IMEI and cleared kami");
    }
    else {
        // 验证通过
        machine_code = savedImei;
        LOGI("IMEI verified: %s", machine_code.c_str());
    }
}

void T3Manager::setEventCallback(EventCallback callback)
{
    event_callback = callback;
}

void T3Manager::setStatusCallback(StatusCallback callback)
{
    status_callback = callback;
}

void T3Manager::setCardKami(const std::string& kami)
{
    card_kami = kami;
    saveKamiToFile(kami);
    LOGD("Card kami set and saved");
}

void T3Manager::setMachineCode(const std::string& code)
{
    machine_code = code;
    saveImeiToFile(code);
    LOGD("Machine code set and saved: %s", code.c_str());
}

void T3Manager::setLocalVersion(const std::string& version)
{
    local_version = version;
    LOGD("Local version set: %s", version.c_str());
}

void T3Manager::setHeartbeatInterval(int seconds)
{
    heartbeat_interval_seconds = seconds;
    LOGD("Heartbeat interval set: %d seconds", seconds);
}

bool T3Manager::autoLogin()
{
    updateStatus("正在检查登录信息...");
    std::string savedKami = loadKamiFromFile();
    
    if (savedKami.empty()) {
        T3Result result;
        result.success = false;
        result.message = "请输入卡密";
        fireEvent(T3EventType::AUTO_LOGIN_NO_KAMI, result);
        return false;
    }
    
    card_kami = savedKami;
    T3Result result;
    result.message = "正在自动登录...";
    fireEvent(T3EventType::AUTO_LOGIN_STARTED, result);
    
    result = login();
    if (!result.success) {
        clearSavedKami();
        T3Result noKamiResult;
        noKamiResult.success = false;
        noKamiResult.message = "自动登录失败，请重新输入卡密";
        fireEvent(T3EventType::AUTO_LOGIN_NO_KAMI, noKamiResult);
        return false;
    }
    return true;
}

void T3Manager::autoLoginAsync()
{
    std::thread t([this]() { autoLogin(); });
    t.detach();
    LOGD("Async auto login started");
}

T3Result T3Manager::login()
{
    T3Result result;
    updateStatus("正在登录...");

    try {
        std::map<std::string, std::string> params;
        params["kami"] = card_kami;
        params["imei"] = machine_code;

        T3DATA t3data;
        result = executeRequestInternal(T3Config::Path_SingleLogin, params, t3data);

        if (!result.success) {
            updateStatus("登录失败: " + result.message);
            fireEvent(T3EventType::LOGIN_FAILED, result);
            return result;
        }

        if (!verifyResponse(t3data)) {
            result.success = false;
            result.message = "数据验证失败";
            updateStatus("登录验证失败");
            fireEvent(T3EventType::LOGIN_FAILED, result);
            return result;
        }

        json responseJson = result.data;
        expire_time = responseJson["end_time"].get<std::string>();
        heartbeat_code = responseJson["statecode"].get<std::string>();
        is_logged_in = true;
        saveKamiToFile(card_kami);

        updateStatus("登录成功，过期时间: " + expire_time);
        fireEvent(T3EventType::LOGIN_SUCCESS, result);
        return result;
    }
    catch (const std::exception& e) {
        result.success = false;
        result.message = std::string("异常错误: ") + e.what();
        updateStatus("登录异常: " + result.message);
        fireEvent(T3EventType::ERROR_OCCURRED, result);
        return result;
    }
}

void T3Manager::loginAsync()
{
    std::thread t([this]() { login(); });
    t.detach();
    LOGD("Async login started");
}

T3Result T3Manager::getVersion()
{
    T3Result result;
    updateStatus("正在检查版本...");

    try {
        result = executeRequest(T3Config::Path_GetProgramVersionNumber, {});

        if (!result.success) {
            updateStatus("版本检查失败: " + result.message);
            fireEvent(T3EventType::VERSION_CHECK_FAILED, result);
            return result;
        }

        json responseJson = result.data;
        latest_version = responseJson["msg"].get<std::string>();

        if (local_version < latest_version) {
            result.message = "版本过低，建议更新";
            updateStatus("检测到新版本: " + latest_version);
            fireEvent(T3EventType::VERSION_OUTDATED, result);
        } else {
            updateStatus("版本正常");
            fireEvent(T3EventType::VERSION_CHECK_SUCCESS, result);
        }
        return result;
    }
    catch (const std::exception& e) {
        result.success = false;
        result.message = std::string("异常错误: ") + e.what();
        updateStatus("版本检查异常: " + result.message);
        fireEvent(T3EventType::ERROR_OCCURRED, result);
        return result;
    }
}

void T3Manager::getVersionAsync()
{
    std::thread t([this]() { getVersion(); });
    t.detach();
    LOGD("Async version check started");
}

T3Result T3Manager::getNotice()
{
    T3Result result;
    updateStatus("正在获取公告...");

    try {
        result = executeRequest(T3Config::Path_GetProgramNotice, {});

        if (!result.success) {
            updateStatus("公告获取失败: " + result.message);
            fireEvent(T3EventType::NOTICE_FETCH_FAILED, result);
            return result;
        }

        json responseJson = result.data;
        std::string notice = responseJson["msg"].get<std::string>();
        updateStatus("公告: " + notice);
        fireEvent(T3EventType::NOTICE_FETCH_SUCCESS, result);
        return result;
    }
    catch (const std::exception& e) {
        result.success = false;
        result.message = std::string("异常错误: ") + e.what();
        updateStatus("公告获取异常: " + result.message);
        fireEvent(T3EventType::ERROR_OCCURRED, result);
        return result;
    }
}

void T3Manager::getNoticeAsync()
{
    std::thread t([this]() { getNotice(); });
    t.detach();
    LOGD("Async notice fetch started");
}

std::string T3Manager::getValue(const std::string& valueId, const std::string& valueName)
{
    updateStatus("正在获取变量值...");

    try {
        if (card_kami.empty()) {
            updateStatus("未登录");
            T3Result result;
            result.success = false;
            result.message = "未登录，请先登录";
            fireEvent(T3EventType::VALUE_FETCH_FAILED, result);
            return "";
        }

        std::map<std::string, std::string> params;
        params["kami"] = card_kami;
        params["valueid"] = valueId;
        params["valuename"] = valueName;

        T3Result result = executeRequest(T3Config::Path_GetValueContent, params);

        if (!result.success) {
            updateStatus("变量获取失败: " + result.message);
            fireEvent(T3EventType::VALUE_FETCH_FAILED, result);
            return "";
        }

        json responseJson = result.data;
        std::string value = responseJson["msg"].get<std::string>();
        updateStatus("变量值获取成功");
        fireEvent(T3EventType::VALUE_FETCH_SUCCESS, result);
        
        return value;
    }
    catch (const std::exception& e) {
        std::string errorMsg = std::string("异常错误: ") + e.what();
        updateStatus("变量获取异常: " + errorMsg);
        T3Result result;
        result.success = false;
        result.message = errorMsg;
        fireEvent(T3EventType::ERROR_OCCURRED, result);
        return "";
    }
}

void T3Manager::getValueAsync(const std::string& valueId, const std::string& valueName)
{
    std::thread t([this, valueId, valueName]() {
        getValue(valueId, valueName);
    });
    t.detach();
    LOGD("Async value fetch started");
}

bool T3Manager::startHeartbeat()
{
    // ========== 修复：更严格的状态检查 ==========
    if (is_running_heartbeat) {
        LOGW("心跳已在运行中，跳过");
        return false;
    }

    if (!is_logged_in) {
        updateStatus("未登录，无法启动心跳");
        return false;
    }

    // ========== 修复：确保旧线程已清理 ==========
    if (heartbeat_thread) {
        LOGW("发现旧的心跳线程对象，正在清理...");
        stop_heartbeat = true;
        
        // 等待一小段时间让旧线程退出
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 不管是否 joinable，都重置指针
        heartbeat_thread.reset();
    }

    stop_heartbeat = false;
    is_running_heartbeat = true;
    heartbeat_retry_count = 0;
    
    heartbeat_thread = std::make_unique<std::thread>([this]() { 
        heartbeatThread(); 
        // ========== 修复：线程结束时清理状态 ==========
        is_running_heartbeat = false;
        LOGI("心跳线程已退出");
    });
    
    // ========== 修复：detach 而不是保持 joinable ==========
    // 这样避免了后续的 join/detach 混乱
    if (heartbeat_thread->joinable()) {
        heartbeat_thread->detach();
    }
    
    updateStatus("心跳已启动");
    LOGI("Heartbeat started");
    return true;
}

void T3Manager::stopHeartbeat()
{
    LOGI("stopHeartbeat 被调用, is_running_heartbeat=%d", is_running_heartbeat);
    
    // 设置停止标志（即使 is_running_heartbeat 为 false 也设置，以防万一）
    stop_heartbeat = true;
    is_running_heartbeat = false;
    
    // ========== 修复：不再尝试 join，因为线程已经 detach ==========
    // 线程会自己检测 stop_heartbeat 并退出
    
    // 重置线程指针
    heartbeat_thread.reset();

    updateStatus("心跳已停止");
    LOGI("Heartbeat stopped");
}



bool T3Manager::isLoggedIn() const { return is_logged_in; }
bool T3Manager::isHeartbeatRunning() const { return is_running_heartbeat; }
std::string T3Manager::getCardKami() const { return card_kami; }
std::string T3Manager::getMachineCode() const { return machine_code; }
std::string T3Manager::getExpireTime() const { return expire_time; }
std::string T3Manager::getLatestVersion() const { return latest_version; }

bool T3Manager::hasSavedKami() const
{
    std::ifstream file(配置路径 + "/" + KAMI_FILE);
    if (file.is_open()) {
        std::string kami;
        std::getline(file, kami);
        file.close();
        kami.erase(0, kami.find_first_not_of(" \t\n\r"));
        kami.erase(kami.find_last_not_of(" \t\n\r") + 1);
        return !kami.empty();
    }
    return false;
}

void T3Manager::clearSavedKami()
{
    std::string filepath = 配置路径 + "/" + KAMI_FILE;
    std::remove(filepath.c_str());
    card_kami.clear();
    LOGI("Cleared saved kami");
}

// T3Manager.cpp - 修改 heartbeatThread()

void T3Manager::heartbeatThread()
{
    LOGD("Heartbeat thread started");
    while (!stop_heartbeat) {
        heartbeat_retry_count = 0;
        T3Result result;
        
        for (int attempt = 0; attempt < HEARTBEAT_MAX_RETRIES; ++attempt) {
            if (stop_heartbeat) break;
            
            LOGD("Heartbeat attempt %d/%d", attempt + 1, HEARTBEAT_MAX_RETRIES);
            result = heartbeat();
            
            if (result.success) {
                fireEvent(T3EventType::HEARTBEAT_SUCCESS, result);
                heartbeat_retry_count = 0;
                break;
            } else {
                heartbeat_retry_count = attempt + 1;
                
                if (attempt == HEARTBEAT_MAX_RETRIES - 1) {
                    result.message = std::string("心跳验证失败，重试") + 
                                   std::to_string(HEARTBEAT_MAX_RETRIES) + 
                                   "次后仍未成功: " + result.message;
                    LOGE("Heartbeat failed after %d retries: %s", 
                         HEARTBEAT_MAX_RETRIES, result.message.c_str());
                    
                    fireEvent(T3EventType::HEARTBEAT_FAILED, result);
                    
                    // ========== 修复：失败后自己设置停止标志 ==========
                    stop_heartbeat = true;
                    is_running_heartbeat = false;
                    return;  // 直接退出线程
                } else {
                    std::string retryMsg = std::string("心跳验证失败，") + 
                                         std::to_string(HEARTBEAT_MAX_RETRIES - attempt - 1) + 
                                         "秒后进行第" + 
                                         std::to_string(attempt + 2) + 
                                         "次尝试...";
                    LOGD("%s", retryMsg.c_str());
                    updateStatus(retryMsg);
                    
                    for (int i = 0; i < HEARTBEAT_RETRY_INTERVAL && !stop_heartbeat; ++i) {
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                }
            }
        }
        
        if (!stop_heartbeat) {
            for (int i = 0; i < heartbeat_interval_seconds && !stop_heartbeat; ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }
    LOGD("Heartbeat thread ended");
}

void T3Manager::fireEvent(T3EventType eventType, const T3Result& result)
{
    if (event_callback) {
        event_callback(eventType, result);
    }
}

void T3Manager::updateStatus(const std::string& status)
{
    if (status_callback) {
        status_callback(status);
    }
}

T3Result T3Manager::executeRequestInternal(const std::string& apiPath, 
                                           const std::map<std::string, std::string>& params,
                                           T3DATA& outT3Data)
{
    T3Result result;

    try {
        outT3Data.setRequestApi(apiPath);

        for (const auto& param : params) {
            outT3Data.addRequestParam(param.first, param.second);
        }

        if (!outT3Data.sendRequest()) {
            result.success = false;
            result.message = "网络请求失败";
            return result;
        }

        json responseJson = outT3Data.getResponseJsonObject();
        
        // code 字段: API可能返回 string "200" 或 number 200，统一处理
        if (responseJson.contains("code")) {
            if (responseJson["code"].is_number()) {
                result.code = responseJson["code"].get<int>();
            } else if (responseJson["code"].is_string()) {
                try {
                    result.code = std::stoi(responseJson["code"].get<std::string>());
                } catch (...) {
                    result.code = -1;
                }
            } else {
                result.code = -1;
            }
        } else {
            result.code = -1;
        }
        
        // msg 字段: 成功响应可能没有 msg 字段，或者 msg 可能不是 string
        if (responseJson.contains("msg")) {
            if (responseJson["msg"].is_string()) {
                result.message = responseJson["msg"].get<std::string>();
            } else {
                result.message = "未知错误";
            }
        } else {
            result.message = "";
        }
        result.data = responseJson;

        if (result.code != 200) {
            result.success = false;
            return result;
        }

        result.success = true;
        return result;
    }
    catch (const std::exception& e) {
        result.success = false;
        result.message = std::string("请求异常: ") + e.what();
        return result;
    }
}

T3Result T3Manager::executeRequest(const std::string& apiPath, const std::map<std::string, std::string>& params)
{
    T3DATA t3data;
    return executeRequestInternal(apiPath, params, t3data);
}

bool T3Manager::verifyResponse(T3DATA& t3data)
{
    try {
        if (!t3data.requestSafeCodeVerify() || 
            !t3data.requestDataSignatureVerify() ||
            !t3data.requestDataTimeDifferenceVerify()) {
            updateStatus("数据验证失败");
            return false;
        }
        return true;
    }
    catch (const std::exception& e) {
        updateStatus(std::string("验证异常: ") + e.what());
        return false;
    }
}

T3Result T3Manager::heartbeat()
{
    T3Result result;

    try {
        std::map<std::string, std::string> params;
        params["kami"] = card_kami;
        params["statecode"] = heartbeat_code;

        LOGD("发送心跳请求: kami=%s..., statecode=%s...", 
             card_kami.substr(0, 8).c_str(),
             heartbeat_code.substr(0, 8).c_str());

        // ========== 修复：使用正确的心跳 API ==========
        result = executeRequest(T3Config::Path_SingleHeartbeat, params);

        LOGD("心跳响应: code=%d, msg=%s", result.code, result.message.c_str());

        if (!result.success) {
            return result;
        }

        // 根据 API 文档，心跳成功返回 code=200
        // executeRequest 已经处理了 code 和 success
        if (result.code == 200) {
            result.success = true;
            result.message = "心跳验证成功";
        } else {
            result.success = false;
            // msg 字段包含失败原因
            std::string msg = result.data.contains("msg") ? 
                              result.data["msg"].get<std::string>() : "心跳验证失败";
            result.message = msg;
        }

        return result;
    }
    catch (const std::exception& e) {
        result.success = false;
        result.message = std::string("心跳异常: ") + e.what();
        LOGE("心跳异常: %s", e.what());
        return result;
    }
}

T3KamiStatus T3Manager::getKamiStatus(const std::string& kami)
{
    T3KamiStatus status;
    updateStatus("正在查询卡密状态...");

    try {
        // 使用传入的卡密或内部保存的卡密
        std::string kamiToUse = kami.empty() ? card_kami : kami;
        
        if (kamiToUse.empty()) {
            status.success = false;
            status.message = "未提供卡密，请先登录或传入卡密";
            updateStatus("查询卡密状态失败: 未提供卡密");
            
            T3Result result;
            result.success = false;
            result.message = status.message;
            fireEvent(T3EventType::VALUE_FETCH_FAILED, result);
            return status;
        }

        std::map<std::string, std::string> params;
        params["kami"] = kamiToUse;

        // 使用 T3DATA 发送请求（自动处理时间戳和签名）
        T3DATA t3data;
        T3Result result = executeRequestInternal(T3Config::Path_IsSingleLoginStatus, params, t3data);

        if (!result.success) {
            status.success = false;
            status.message = result.message;
            status.code = std::to_string(result.code);
            updateStatus("查询卡密状态失败: " + result.message);
            fireEvent(T3EventType::VALUE_FETCH_FAILED, result);
            return status;
        }

        // 解析响应数据 - 注意: code 在 executeRequestInternal 中已解析为 int
        // API 返回的 code 是字符串 "200"，但 responseJson 中可能是 string 或 number
        json responseJson = result.data;
        status.success = true;
        
        // code: 可能是 string "200" 或 number 200
        if (responseJson.contains("code")) {
            if (responseJson["code"].is_string()) {
                status.code = responseJson["code"].get<std::string>();
            } else {
                status.code = std::to_string(responseJson["code"].get<int>());
            }
        }
        
        status.state = responseJson.contains("state") ? 
                      responseJson["state"].get<std::string>() : "";
        status.use = responseJson.contains("use") ? 
                    responseJson["use"].get<std::string>() : "";
        
        // id: API 文档是 string，但可能返回 number
        if (responseJson.contains("id")) {
            if (responseJson["id"].is_string()) {
                status.id = responseJson["id"].get<std::string>();
            } else {
                status.id = std::to_string(responseJson["id"].get<long long>());
            }
        }
        
        status.use_time = responseJson.contains("use_time") ? 
                         responseJson["use_time"].get<std::string>() : "";
        status.end_time = responseJson.contains("end_time") ? 
                         responseJson["end_time"].get<std::string>() : "";
        status.line_time = responseJson.contains("line_time") ? 
                          responseJson["line_time"].get<std::string>() : "";
        status.line = responseJson.contains("line") ? 
                     responseJson["line"].get<std::string>() : "";
        status.amount = responseJson.contains("amount") ? 
                       responseJson["amount"].get<std::string>() : "";
        
        // available: integer
        if (responseJson.contains("available")) {
            if (responseJson["available"].is_number()) {
                status.available = responseJson["available"].get<int>();
            } else {
                status.available = std::stoi(responseJson["available"].get<std::string>());
            }
        }
        
        // time: integer
        if (responseJson.contains("time")) {
            if (responseJson["time"].is_number()) {
                status.time = responseJson["time"].get<long long>();
            } else {
                status.time = std::stoll(responseJson["time"].get<std::string>());
            }
        }
        
        status.date = responseJson.contains("date") ? 
                     responseJson["date"].get<std::string>() : "";

        std::string statusMsg = "卡密状态 - 激活状态: " + status.use + 
                               ", 到期时间: " + status.end_time + 
                               ", 剩余: " + std::to_string(status.available) + "秒";
        updateStatus(statusMsg);
        fireEvent(T3EventType::VALUE_FETCH_SUCCESS, result);

        LOGI("卡密状态查询成功: state=%s, use=%s, available=%d", 
             status.state.c_str(), status.use.c_str(), status.available);
        
        return status;
    }
    catch (const std::exception& e) {
        status.success = false;
        status.message = std::string("异常错误: ") + e.what();
        updateStatus("查询卡密状态异常: " + status.message);
        
        T3Result errorResult;
        errorResult.success = false;
        errorResult.message = status.message;
        fireEvent(T3EventType::ERROR_OCCURRED, errorResult);
        
        LOGE("查询卡密状态异常: %s", e.what());
        return status;
    }
}

void T3Manager::getKamiStatusAsync(const std::string& kami)
{
    std::thread t([this, kami]() {
        getKamiStatus(kami);
    });
    t.detach();
    LOGD("异步查询卡密状态已启动");
}

int T3Manager::getHeartbeatRetryCount() const
{
    return heartbeat_retry_count;
}

int T3Manager::getHeartbeatMaxRetries() const
{
    return HEARTBEAT_MAX_RETRIES;
}


/**
 * 检查版本更新 (同步)
 */
VersionCheckResult T3Manager::checkVersionUpdate(int localVersion)
{
    VersionCheckResult result;
    updateStatus("正在检查版本更新...");

    try {
        // 构建请求参数
        std::map<std::string, std::string> params;
        params["ver"] = std::to_string(localVersion);

        // 使用 T3DATA 发送请求（自动处理时间戳和签名）
        T3DATA t3data;
        T3Result apiResult = executeRequestInternal(T3Config::Path_CheckVersionUpdate, params, t3data);
    
    // 201 = 已是最新版，不是真正的失败
    if (!apiResult.success && apiResult.code == 201) {
        result.success = true;
        result.code = "201";
        result.message = apiResult.message;  // "已是最新版"
        result.version = localVersion;       // 版本号等于本地，表示已是最新
        result.ver = std::to_string(localVersion);
        
        {
            std::lock_guard<std::mutex> lock(version_result_mutex);
            cached_version_result = result;
        }
        
        updateStatus("已是最新版本");
        LOGI("版本检查: 已是最新版本 (本地: %d)", localVersion);
        fireEvent(T3EventType::VERSION_CHECK_SUCCESS, apiResult);
        return result;
    }
    
        if (!apiResult.success) {
            result.success = false;
            result.message = apiResult.message;
            result.code = std::to_string(apiResult.code);
            updateStatus("版本检查失败: " + apiResult.message);
            fireEvent(T3EventType::VERSION_CHECK_FAILED, apiResult);
            LOGW("版本检查失败: %s", apiResult.message.c_str());
            return result;
        }

        // 解析响应数据
        json responseJson = apiResult.data;
        
        result.success = true;
        
        // code: 可能是 string "200" 或 number 200
        if (responseJson.contains("code")) {
            if (responseJson["code"].is_string()) {
                result.code = responseJson["code"].get<std::string>();
            } else {
                result.code = std::to_string(responseJson["code"].get<int>());
            }
        }
        
        result.ver = responseJson.contains("ver") ? 
                    responseJson["ver"].get<std::string>() : "";
        
        // version: integer
        if (responseJson.contains("version")) {
            if (responseJson["version"].is_number()) {
                result.version = responseJson["version"].get<int>();
            } else {
                result.version = std::stoi(responseJson["version"].get<std::string>());
            }
        }
        
        result.uplog = responseJson.contains("uplog") ? 
                      responseJson["uplog"].get<std::string>() : "";
        result.upurl = responseJson.contains("upurl") ? 
                      responseJson["upurl"].get<std::string>() : "";
        
        // time: integer
        if (responseJson.contains("time")) {
            if (responseJson["time"].is_number()) {
                result.time = responseJson["time"].get<long long>();
            } else {
                result.time = std::stoll(responseJson["time"].get<std::string>());
            }
        }

        // 缓存版本检查结果
        {
            std::lock_guard<std::mutex> lock(version_result_mutex);
            cached_version_result = result;
        }

        // 生成状态消息
        std::string statusMsg;
        if (result.isNewerThan(localVersion)) {
            statusMsg = "检测到新版本: " + result.ver + 
                       " (本地: " + std::to_string(localVersion) + 
                       ", 最新: " + std::to_string(result.version) + ")";
            updateStatus(statusMsg);
            LOGI("检测到新版本: %s", result.ver.c_str());
        } else if (result.isLatest(localVersion)) {
            statusMsg = "已是最新版本: " + result.ver;
            updateStatus(statusMsg);
            LOGI("已是最新版本: %s", result.ver.c_str());
        } else {
            statusMsg = "警告: 本地版本 (" + std::to_string(localVersion) + 
                       ") 大于服务器版本 (" + std::to_string(result.version) + ")";
            updateStatus(statusMsg);
            LOGW("%s", statusMsg.c_str());
        }
        
        // 详细日志
        LOGI("版本检查完成:");
        LOGI("  本地版本: %d", localVersion);
        LOGI("  最新版本: %s (%d)", result.ver.c_str(), result.version);
        LOGI("  更新公告: %s", result.uplog.c_str());
        LOGI("  更新地址: %s", result.upurl.c_str());
        
        fireEvent(T3EventType::VERSION_CHECK_SUCCESS, apiResult);
        return result;
    }
    catch (const std::exception& e) {
        result.success = false;
        result.message = std::string("异常错误: ") + e.what();
        updateStatus("版本检查异常: " + result.message);
        
        T3Result errorResult;
        errorResult.success = false;
        errorResult.message = result.message;
        fireEvent(T3EventType::ERROR_OCCURRED, errorResult);
        
        LOGE("版本检查异常: %s", e.what());
        return result;
    }
}

/**
 * 检查版本更新 (异步)
 */
void T3Manager::checkVersionUpdateAsync(int localVersion)
{
    std::thread t([this, localVersion]() {
        checkVersionUpdate(localVersion);
    });
    t.detach();
    LOGD("异步版本检查已启动 (本地版本: %d)", localVersion);
}

/**
 * 获取缓存的版本检查结果
 */
VersionCheckResult T3Manager::getLatestVersionCheckResult() 
{
    std::lock_guard<std::mutex> lock(version_result_mutex);
    return cached_version_result;
}

/**
 * 清除缓存的版本信息
 */
void T3Manager::clearVersionCache()
{
    std::lock_guard<std::mutex> lock(version_result_mutex);
    cached_version_result = VersionCheckResult();
    LOGI("版本缓存已清除");
}