// T3Config.h
#pragma once
#include <string>
namespace T3Config {
    // t3网络验证配置
    static const std::string Host = "https://w.t3yanzheng.com";
    static const std::string AppKey = "bcac5d7df22db9fae912c0ab1510cdba";
    static const std::string RsaPublicKey = "-----BEGIN PUBLIC KEY-----\n"
"MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDg21U/DV/mdHNktPGh/NtbO759\n"
"fRiTkBI97tU5G2htqUkXNyaLPnU+Z32tyhet1Ca6wWHxc8HAgEkS3W1w4/g9qVRF\n"
"YF/EFDOD6LglzCM/lY9lUmYCnm3rViRzsDeusry0fSLekWDsv5GAizhJ3na8lstc\n"
"pNafMrnvUfLxrKCj4wIDAQAB\n"
"-----END PUBLIC KEY-----";

    
    // API路径
    static const std::string Path_SingleLogin = "072BA6950821F4E1";           // 单码卡密登录http://w.t3yanzheng.com/072BA6950821F4E1
    static const std::string Path_IsSingleLoginStatus = "6A3C696717FDD8B9";   // 查询卡密状态http://w.t3yanzheng.com/6A3C696717FDD8B9
    static const std::string Path_SingleHeartbeat = "696B78879B5AC389";       // 单码卡密心跳验证http://w.t3yanzheng.com/696B78879B5AC389
    static const std::string Path_GetProgramNotice = "6FA6B912899E3E65";      // 获取公告http://w.t3yanzheng.com/6FA6B912899E3E65
    static const std::string Path_GetProgramVersionNumber = "F631ADFBAF23EF2A"; // 获取版本号http://w.t3yanzheng.com/F631ADFBAF23EF2A
    static const std::string Path_GetValueContent = "DCFE05F0A89FE1F9";       // 获取变量内容http://w.t3yanzheng.com/DCFE05F0A89FE1F9
    static const std::string Path_CheckVersionUpdate = "42662EE0EE573128";      // 检测版本是否为最新版http://w.t3yanzheng.com/42662EE0EE573128

}
