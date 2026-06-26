#include "ImeiGenerator.h"
#include "Logger.h"
#include <sys/system_properties.h>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <functional>

/**
 * 获取系统属性值 - 使用NDK提供的API
 */
std::string ImeiGenerator::getSystemProperty(const std::string& prop_name) {
    char prop_value[PROP_VALUE_MAX] = {0};
    
    // 使用NDK提供的系统属性获取函数
    int length = __system_property_get(prop_name.c_str(), prop_value);
    
    if (length > 0) {
        LOGD("System property %s: %s", prop_name.c_str(), prop_value);
        return std::string(prop_value);
    } else {
        LOGW("System property %s not found", prop_name.c_str());
        return "";
    }
}

/**
 * 计算字符串的简单哈希值
 */
unsigned int ImeiGenerator::simpleHash(const std::string& str) {
    unsigned int hash = 5381;
    
    for (char c : str) {
        hash = ((hash << 5) + hash) + static_cast<unsigned char>(c);
    }
    
    return hash;
}

/**
 * 将字符串转换为IMEI格式（15位数字）
 */
std::string ImeiGenerator::toImeiFormat(const std::string& input) {
    if (input.empty()) {
        LOGW("Input string is empty for IMEI format conversion");
        return "000000000000000";  // 默认值
    }
    
    std::stringstream ss;
    
    // 计算哈希值以生成15位数字
    unsigned int hash1 = simpleHash(input);
    unsigned int hash2 = simpleHash(input + "imei");
    unsigned int hash3 = simpleHash(input + "device");
    
    // 组合三个哈希值生成15位数字
    std::string imei;
    
    // 前5位：hash1的模1000000000
    imei += std::to_string((hash1 % 100000));
    
    // 中间5位：hash2的模1000000000
    imei += std::to_string((hash2 % 100000));
    
    // 后5位：hash3的模1000000000
    imei += std::to_string((hash3 % 100000));
    
    // 补齐到15位
    if (imei.length() < 15) {
        imei += std::string(15 - imei.length(), '0');
    } else if (imei.length() > 15) {
        imei = imei.substr(0, 15);
    }
    
    // 确保是纯数字
    for (char& c : imei) {
        if (!std::isdigit(c)) {
            c = '0' + (static_cast<unsigned char>(c) % 10);
        }
    }
    
    LOGD("Generated IMEI: %s", imei.c_str());
    return imei;
}

/**
 * 从系统属性生成IMEI
 */
std::string ImeiGenerator::generateFromSystemProperties() {
    LOGI("Generating IMEI from system properties");
    
    // 获取各种系统属性
    std::string serial = getSystemProperty("ro.serialno");          // 设备序列号
    std::string fingerprint = getSystemProperty("ro.build.fingerprint");  // Build指纹
    std::string model = getSystemProperty("ro.product.model");      // 设备型号
    std::string manufacturer = getSystemProperty("ro.product.manufacturer");  // 制造商
    std::string board = getSystemProperty("ro.board.platform");     // 主板平台
    std::string build_id = getSystemProperty("ro.build.id");        // Build ID
    std::string android_version = getSystemProperty("ro.build.version.release");  // Android版本
    
    // 将属性组合成一个字符串
    std::string combined = serial + fingerprint + model + manufacturer + board + build_id + android_version;
    
    if (combined.empty()) {
        LOGW("No system properties available, using fallback");
        // 如果没有获取到任何属性，使用默认值
        combined = "ANDROID_DEVICE_NO_PROPS";
    }
    
    LOGD("Combined properties string: %s", combined.c_str());
    
    // 转换为IMEI格式
    return toImeiFormat(combined);
}

/**
 * 生成设备IMEI（主函数）
 */
std::string ImeiGenerator::generateImei() {
    try {
        std::string imei = generateFromSystemProperties();
        
        if (imei.empty() || imei == "000000000000000") {
            LOGE("Failed to generate IMEI");
            return "000000000000000";
        }
        
        LOGI("IMEI generated successfully: %s", imei.c_str());
        return imei;
    }
    catch (const std::exception& e) {
        LOGE("Exception while generating IMEI: %s", e.what());
        return "000000000000000";
    }
}