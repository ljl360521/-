// Config.h
#pragma once

#include <string>
#include <functional>
#include <unordered_map>

namespace pugi { class xml_document; }

class ConfigManager {
public:
    // 设置配置文件路径（应在初始化时调用）
    static void SetConfigPath(const std::string& path);

    // 加载配置文件
    static bool Load();

    // 保存配置文件
    static bool Save();

    // 注册一个配置项（用于自动加载/保存）
    using Getter = std::function<std::string()>;
    using Setter = std::function<void(const std::string&)>;
    static void Register(const std::string& key, Getter getter, Setter setter);

    // 便捷方法：注册基本类型
    static void RegisterBool(const std::string& key, bool* value);
    static void RegisterInt(const std::string& key, int* value);
    static void RegisterFloat(const std::string& key, float* value);
    static void RegisterString(const std::string& key, std::string* value);

    // 手动读写（不注册）
    static std::string GetString(const std::string& key, const std::string& defaultValue = "");
    static void SetString(const std::string& key, const std::string& value);

    // 释放配置文档缓存（应用退出或配置路径切换时调用）
    static void Cleanup();

private:
    static std::string s_configPath;
    static pugi::xml_document* s_doc;
    static std::unordered_map<std::string, std::pair<Getter, Setter>> s_registry;

    static void EnsureDocument();
};