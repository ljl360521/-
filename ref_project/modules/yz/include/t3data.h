#ifndef T3DATA_H
#define T3DATA_H

#include <string>
#include <map>
#include <json.hpp>
#include "t3config.h"

using json = nlohmann::json;

class T3DATA {
private:
    std::string requestApi;                        // 请求接口地址
    std::map<std::string, std::string> requestParams; // 请求参数
    std::string request_safe_code;                 // 请求随机安全码-防劫持验证用
    std::string responseData;                      // 响应数据(发送请求后这里则会被赋值)
    std::string s;                                 // 数据签名

public:
    T3DATA();

    // 设置请求接口地址
    void setRequestApi(std::string api);

    // 添加请求参数
    void addRequestParam(std::string key, std::string value);

    // 发送请求，若请求成功返回true
    bool sendRequest();

    // 获取响应数据
    std::string getResponseData();

    // 获取响应数据json对象
    json getResponseJsonObject();

    // 解密数据
    std::string decrypt(std::string &str);

    // 加密数据
    std::string encrypt(std::string &str);

    // 请求安全码验证-响应数据防劫持验证
    bool requestSafeCodeVerify();

    // 响应数据签名验证
    bool requestDataSignatureVerify();

    // 响应数据时差验证
    bool requestDataTimeDifferenceVerify();

    // 统一参数中转处理，返回最终处理完的必要参数、签名、加密的参数map
    std::map<std::string, std::string> paramsHandle(std::map<std::string, std::string> params);

    // 统一参数中转处理，返回最终处理完的必要参数、签名、加密的参数字符串
    std::string paramsHandleString(std::map<std::string, std::string> params);

    // 获取计算参数签名字符串
    std::string getParamsSignString(std::map<std::string, std::string> params);

    // 获取当前时间戳
    int getTimestamp();

    // 获取日期格式时间，不带秒
    std::string getCurrentDateTimeString();

    // 获取字符串md5值
    std::string getStringMd5(std::string strPlain);

    // 获取随机字符串
    std::string getRandomString(std::string strCharElem, int nOutStrLen);

    // 解析json字符串
    json parseJSON(const std::string& jsonString);

    // 十六进制转ASCII字符串
    std::string hexStringToAscii(const std::string& hexString);

    // ASCII字符串转十六进制
    std::string asciiToHexString(const std::string& asciiString);

private:
    // curl 回调函数，用于处理响应数据
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
};

#endif // T3DATA_H
