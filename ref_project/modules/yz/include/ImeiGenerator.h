#pragma once

#include <string>

/**
 * IMEI生成器 - 通过NDK获取设备信息并生成唯一的IMEI
 * 不使用JNI，直接使用NDK的系统属性API
 */
class ImeiGenerator {
public:
    /**
     * 生成设备IMEI
     * @return 生成的IMEI字符串（15位）
     */
    static std::string generateImei();

private:
    /**
     * 获取系统属性值
     * @param prop_name 属性名称
     * @return 属性值
     */
    static std::string getSystemProperty(const std::string& prop_name);

    /**
     * 从多个系统属性生成IMEI
     * @return 生成的IMEI
     */
    static std::string generateFromSystemProperties();

    /**
     * 将字符串转换为IMEI格式（15位数字）
     * @param input 输入字符串
     * @return IMEI格式的字符串
     */
    static std::string toImeiFormat(const std::string& input);

    /**
     * 计算字符串的简单哈希值
     * @param str 输入字符串
     * @return 哈希值
     */
    static unsigned int simpleHash(const std::string& str);
};