#include "t3data.h"
#include "MD5.h"
#include "Rsa.h"
#include "Logger.h"
#include <curl/curl.h>
#include <iostream>
#include <cstdio>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <thread>
#include <cstdlib>
#include <cstring>

T3DATA::T3DATA() {
    request_safe_code = getRandomString("", 32);
}

// curl 回调函数，用于处理响应数据
size_t T3DATA::WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    if (!contents || !userp) return 0;
    
    size_t total_size = size * nmemb;
    userp->append(static_cast<char*>(contents), total_size);
    return total_size;
}

// 设置请求接口地址
void T3DATA::setRequestApi(std::string api) {
    requestApi = api;
}

// 添加请求参数
void T3DATA::addRequestParam(std::string key, std::string value) {
    if (!key.empty()) {
        requestParams[key] = value;
    }
}

// 发送请求，若请求成功返回true
bool T3DATA::sendRequest() {
    if (requestApi.empty()) {
        LOGE("Request API is not set");
        return false;
    }
    
    std::string paramsHandleStringResult = paramsHandleString(requestParams);
    if (paramsHandleStringResult.empty()) {
        LOGE("Failed to handle request parameters");
        return false;
    }
    
    std::string strData;
    std::string url = T3Config::Host + "/" + requestApi + "?" + paramsHandleStringResult;
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        LOGE("Failed to initialize CURL");
        return false;
    }
    
    bool success = false;
    
    // 设置请求URL
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    
    // 设置超时时间（单位：秒）
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    // 设置响应数据写入回调函数
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &strData);
    
    // 设置 SSL 验证（如果需要禁用，取消注释下面两行）
     curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
     curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    
    // 执行请求
    CURLcode res = curl_easy_perform(curl);
    
    if (res == CURLE_OK) {
        responseData = strData;
        success = true;
    } else {
        LOGE("curl_easy_perform() failed: %s", curl_easy_strerror(res));
    }
    
    // 清理资源
    curl_easy_cleanup(curl);
    
    return success;
}

// 解密数据
std::string T3DATA::decrypt(std::string &str) {
    if (str.empty()) return "";
    
    char* decrypted = public_decrypt(str.c_str(), T3Config::RsaPublicKey.c_str());
    
    if (!decrypted) {
        LOGE("public_decrypt returned NULL for input: %s", str.c_str());
        return "";
    }
    
    std::string result(decrypted);
    free(decrypted);
    return result;
}

// 加密数据
std::string T3DATA::encrypt(std::string &str) {
    if (str.empty()) return "";
    
    char* encrypted = public_encrypt(str.c_str(), T3Config::RsaPublicKey.c_str());
    if (encrypted) {
        std::string result(encrypted);
        free(encrypted);
        return result;
    }
    return "";
}

// 获取响应数据
std::string T3DATA::getResponseData() {
    if (responseData.empty()) {
        return "";
    }
    
    std::string cleartext = decrypt(responseData);
    return cleartext;
}

// 获取响应数据json对象
json T3DATA::getResponseJsonObject() {
    std::string response = getResponseData();
    if (response.empty()) {
        return json::object();
    }
    
    try {
        return json::parse(response);
    } catch (const json::exception& e) {
        LOGE("JSON parse error: %s", e.what());
        return json::object();
    }
}

// 请求安全码验证-响应数据防劫持验证
bool T3DATA::requestSafeCodeVerify() {
    json responseJson = getResponseJsonObject();
    if (responseJson.is_null() || responseJson.empty()) {
        LOGE("Response JSON is null or empty for safe code verify");
        return false;
    }
    
    try {
        if (responseJson.contains("safe_code")) {
            std::string response_safe_code = responseJson["safe_code"].get<std::string>();
            bool verified = request_safe_code == response_safe_code;
            if (!verified) {
                LOGE("Safe code verification failed");
            }
            return verified;
        }
    } catch (const json::exception& e) {
        LOGE("Safe code verify error: %s", e.what());
    }
    
    return false;
}

// 响应数据签名验证
bool T3DATA::requestDataSignatureVerify() {
    json responseJson = getResponseJsonObject();
    if (responseJson.is_null() || responseJson.empty()) {
        LOGE("Response JSON is null or empty for signature verify");
        return false;
    }
    
    try {
        if (responseJson.contains("token") && responseJson.contains("id") && 
            responseJson.contains("end_time")) {
            std::string token = responseJson["token"].get<std::string>();
            std::string id = responseJson["id"].get<std::string>();
            std::string end_time = responseJson["end_time"].get<std::string>();
            std::string current_time = getCurrentDateTimeString();
            
            std::string verify_str = id + T3Config::AppKey + s + end_time + current_time;
            std::string calculated_token = getStringMd5(verify_str);
            
            bool verified = token == calculated_token;
            if (!verified) {
                LOGE("Data signature verification failed");
            }
            return verified;
        }
    } catch (const json::exception& e) {
        LOGE("Signature verify error: %s", e.what());
    }
    
    return false;
}

// 响应数据时差验证
bool T3DATA::requestDataTimeDifferenceVerify() {
    json responseJson = getResponseJsonObject();
    if (responseJson.is_null() || responseJson.empty()) {
        LOGE("Response JSON is null or empty for time difference verify");
        return false;
    }
    
    try {
        if (responseJson.contains("time")) {
            int server_time = responseJson["time"].get<int>();
            int allow_time_scope_minimum = server_time - 30;
            int allow_time_scope_maximum = server_time + 30;
            int timestamp = getTimestamp();
            
            bool verified = (timestamp >= allow_time_scope_minimum && timestamp <= allow_time_scope_maximum);
            if (!verified) {
                LOGE("Time difference verification failed, server_time: %d, local_time: %d", server_time, timestamp);
            }
            return verified;
        }
    } catch (const json::exception& e) {
        LOGE("Time difference verify error: %s", e.what());
    }
    
    return false;
}

// 统一参数中转处理，返回最终处理完的必要参数、签名、加密的参数map
std::map<std::string, std::string> T3DATA::paramsHandle(std::map<std::string, std::string> params) {
    params["safe_code"] = request_safe_code;  // 增加请求防劫持验证参数
    params["t"] = std::to_string(getTimestamp());  // 增加签名必要参数-时间戳
    
    std::map<std::string, std::string> encryptionRequestParams;
    
    // 开始参数加密处理
    for (auto& pair : params) {
        if (!pair.first.empty() && !pair.second.empty()) {
            std::string encrypted_value = encrypt(pair.second);
            if (!encrypted_value.empty()) {
                encryptionRequestParams[pair.first] = encrypted_value;
            }
        }
    }
    
    return encryptionRequestParams;
}

// 统一参数中转处理，返回最终处理完的必要参数、签名、加密的参数字符串
std::string T3DATA::paramsHandleString(std::map<std::string, std::string> params) {
    std::map<std::string, std::string> encryptionRequestParams = paramsHandle(params);
    std::string paramsStr = "";
    
    for (auto& pair : encryptionRequestParams) {
        if (!paramsStr.empty()) {
            paramsStr.append("&");
        }
        paramsStr.append(pair.first + "=" + pair.second);
    }
    
    s = paramsStr + "&" + T3Config::AppKey;
    std::string ss = getStringMd5(s);
    std::string encrypted_ss = encrypt(ss);
    
    if (!paramsStr.empty() && !encrypted_ss.empty()) {
        paramsStr += "&s=" + encrypted_ss;
    }
    
    return paramsStr;
}

// 获取计算参数签名字符串
std::string T3DATA::getParamsSignString(std::map<std::string, std::string> params) {
    std::string signStr = "";
    
    for (auto& pair : params) {
        if (!pair.first.empty() && !pair.second.empty()) {
            if (!signStr.empty()) {
                signStr.append("&");
            }
            
            std::string encrypted_value = encrypt(pair.second);
            if (!encrypted_value.empty()) {
                signStr.append(pair.first + "=" + encrypted_value);
            }
        }
    }
    
    return signStr;
}

// 获取当前时间戳
int T3DATA::getTimestamp() {
    return static_cast<int>(time(nullptr));
}

// 获取日期格式时间 不带秒
std::string T3DATA::getCurrentDateTimeString() {
    time_t rawTime = time(nullptr);
    struct tm* timeInfo = localtime(&rawTime);
    
    if (!timeInfo) return "";
    
    std::stringstream ss;
    ss << std::setfill('0') 
       << std::setw(4) << (timeInfo->tm_year + 1900)
       << std::setw(2) << (timeInfo->tm_mon + 1)
       << std::setw(2) << timeInfo->tm_mday
       << std::setw(2) << timeInfo->tm_hour
       << std::setw(2) << timeInfo->tm_min;
    
    return ss.str();
}

// 获取字符串md5值
std::string T3DATA::getStringMd5(std::string strPlain) {
    if (strPlain.empty()) return "";
    
    unsigned char decrypt[16];
    MD5_CTX mdContext;
    
    MD5Init(&mdContext);
    MD5Update(&mdContext, reinterpret_cast<unsigned char*>(&strPlain[0]), 
              static_cast<unsigned int>(strPlain.size()));
    MD5Final(&mdContext, decrypt);
    
    std::string md5;
    char buf[3];
    for (int i = 0; i < 16; i++) {
        snprintf(buf, sizeof(buf), "%02x", decrypt[i]);
        md5.append(buf);
    }
    return md5;
}

// 获取随机字符串
std::string T3DATA::getRandomString(std::string strCharElem, int nOutStrLen) {
    if (nOutStrLen <= 0) return "";
    
    if (strCharElem.empty()) {
        strCharElem = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    }
    
    std::string strRet;
    strRet.reserve(nOutStrLen);
    
    srand(static_cast<unsigned int>(time(nullptr)));
    
    for (int i = 0; i < nOutStrLen; ++i) {
        int iRand = rand() % static_cast<int>(strCharElem.length());
        strRet.push_back(strCharElem[iRand]);
    }
    
    return strRet;
}

// 使用nlohmann json库解析json
json T3DATA::parseJSON(const std::string& jsonString) {
    if (jsonString.empty()) {
        return json::object();
    }
    
    try {
        return json::parse(jsonString);
    } catch (const json::exception& e) {
        LOGE("JSON parse error: %s", e.what());
        return json::object();
    }
}

// 十六进制字符串转ASCII字符串
std::string T3DATA::hexStringToAscii(const std::string& hexString) {
    if (hexString.length() % 2 != 0) {
        return "";
    }
    
    std::string asciiString;
    asciiString.reserve(hexString.length() / 2);
    
    for (size_t i = 0; i < hexString.length(); i += 2) {
        std::string hexByte = hexString.substr(i, 2);
        int asciiValue = 0;
        
        if (sscanf(hexByte.c_str(), "%2x", &asciiValue) != 1) {
            LOGE("Failed to parse hex byte: %s", hexByte.c_str());
            return "";
        }
        
        asciiString.push_back(static_cast<char>(asciiValue));
    }
    
    return asciiString;
}

// ASCII字符串转十六进制字符串
std::string T3DATA::asciiToHexString(const std::string& asciiString) {
    std::string hexString;
    hexString.reserve(asciiString.length() * 2);
    
    for (size_t i = 0; i < asciiString.length(); ++i) {
        char hexByte[3];
        snprintf(hexByte, sizeof(hexByte), "%02X", static_cast<unsigned char>(asciiString[i]));
        hexString.append(hexByte);
    }
    
    return hexString;
}