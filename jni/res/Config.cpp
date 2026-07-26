// Config.cpp
#include "res/Config.h"
#include "pugixml.hpp"
#include <fstream>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <climits>
#include <cstdio>
#include <mutex>

std::string ConfigManager::s_configPath;
pugi::xml_document* ConfigManager::s_doc = nullptr;
std::unordered_map<std::string, std::pair<ConfigManager::Getter, ConfigManager::Setter>> ConfigManager::s_registry;
static std::recursive_mutex g_config_mutex;

namespace {

bool TryParseInt(const std::string& s, int& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    errno = 0;
    long v = strtol(s.c_str(), &end, 10);
    if (errno != 0 || end == s.c_str() || *end != '\0' || v < INT_MIN || v > INT_MAX)
        return false;
    out = static_cast<int>(v);
    return true;
}

bool TryParseFloat(const std::string& s, float& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    errno = 0;
    float v = strtof(s.c_str(), &end);
    if (errno != 0 || end == s.c_str() || *end != '\0')
        return false;
    out = v;
    return true;
}


const char* SimpleTagForKey(const std::string& key) {
    if (key == "卡密验证通过") return "自动登录";
    if (key == "保存的卡密") return "卡密";
    if (key == "界面缩放") return "界面缩放";
    if (key == "音量键调UI大小") return "音量键调UI大小";
    if (key == "当前标签页") return "当前标签页";
    if (key == "主题索引") return "主题索引";
    if (key == "显示日志窗口") return "显示日志窗口";
    if (key == "显示另一个窗口") return "显示另一个窗口";
    return nullptr;
}

}

void ConfigManager::SetConfigPath(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lock(g_config_mutex);
    if (s_configPath != path) {
        Cleanup();
        s_configPath = path;
    }
}

void ConfigManager::EnsureDocument() {
    std::lock_guard<std::recursive_mutex> lock(g_config_mutex);
    if (!s_doc) {
        s_doc = new pugi::xml_document();
        std::ifstream file(s_configPath);
        if (file.good()) {
            s_doc->load_file(s_configPath.c_str());
        }
        if (!s_doc->child("天天开心")) {
            s_doc->reset();
            s_doc->append_child("天天开心");
        }
    }
}

bool ConfigManager::Load() {
    std::lock_guard<std::recursive_mutex> lock(g_config_mutex);
    EnsureDocument();
    if (!s_doc) return false;

    pugi::xml_node root = s_doc->child("天天开心");
    if (!root) return false;

    for (const auto& pair : s_registry) {
        const char* tag = SimpleTagForKey(pair.first);
        if (!tag) continue;
        pugi::xml_node node = root.child(tag);
        if (node) pair.second.second(node.text().as_string());
    }
    return true;
}

bool ConfigManager::Save() {
    std::lock_guard<std::recursive_mutex> lock(g_config_mutex);
    if (s_configPath.empty()) return false;
    std::string tmpPath = s_configPath + ".tmp";

    pugi::xml_document doc;
    pugi::xml_node root = doc.append_child("天天开心");

    // 简单格式：保留非颜色字段；颜色字段不写入配置。
    const char* saveOrder[] = {
        "卡密验证通过",
        "保存的卡密",
        "界面缩放",
        "音量键调UI大小",
        "当前标签页",
        "主题索引",
        "显示日志窗口",
        "显示另一个窗口"
    };
    for (const char* key : saveOrder) {
        auto it = s_registry.find(key);
        if (it == s_registry.end()) continue;
        const char* tag = SimpleTagForKey(key);
        if (!tag) continue;
        root.append_child(tag).text().set(it->second.first().c_str());
    }

    // 先写临时文件再 rename，降低配置写到一半时进程退出造成 XML 损坏的概率。
    if (!doc.save_file(tmpPath.c_str(), "  ", pugi::format_default, pugi::encoding_utf8)) return false;
    if (rename(tmpPath.c_str(), s_configPath.c_str()) != 0) {
        remove(tmpPath.c_str());
        return false;
    }
    Cleanup(); // 下次读取时从原子写入后的文件重新加载，避免缓存旧文档。
    return true;
}

void ConfigManager::Register(const std::string& key, Getter getter, Setter setter) {
    std::lock_guard<std::recursive_mutex> lock(g_config_mutex);
    s_registry[key] = { getter, setter };
}

void ConfigManager::RegisterBool(const std::string& key, bool* value) {
    Register(key,
        [value]() -> std::string { return *value ? "true" : "false"; },
        [value](const std::string& s) { *value = (s == "true"); });
}

void ConfigManager::RegisterInt(const std::string& key, int* value) {
    Register(key,
        [value]() -> std::string { return std::to_string(*value); },
        [value](const std::string& s) { int v; if (TryParseInt(s, v)) *value = v; });
}

void ConfigManager::RegisterFloat(const std::string& key, float* value) {
    Register(key,
        [value]() -> std::string { return std::to_string(*value); },
        [value](const std::string& s) { float v; if (TryParseFloat(s, v)) *value = v; });
}

void ConfigManager::RegisterString(const std::string& key, std::string* value) {
    Register(key,
        [value]() -> std::string { return *value; },
        [value](const std::string& s) { *value = s; });
}

std::string ConfigManager::GetString(const std::string& key, const std::string& defaultValue) {
    std::lock_guard<std::recursive_mutex> lock(g_config_mutex);
    EnsureDocument();
    if (!s_doc) return defaultValue;
    pugi::xml_node root = s_doc->child("天天开心");
    if (!root) return defaultValue;
    const char* tag = SimpleTagForKey(key);
    if (!tag) tag = key.c_str();
    pugi::xml_node node = root.child(tag);
    return node ? node.text().as_string(defaultValue.c_str()) : defaultValue;
}

void ConfigManager::SetString(const std::string& key, const std::string& value) {
    std::lock_guard<std::recursive_mutex> lock(g_config_mutex);
    EnsureDocument();
    pugi::xml_node root = s_doc->child("天天开心");
    if (!root) root = s_doc->append_child("天天开心");
    const char* tag = SimpleTagForKey(key);
    if (!tag) tag = key.c_str();
    pugi::xml_node node = root.child(tag);
    if (!node) node = root.append_child(tag);
    node.text().set(value.c_str());
}

void ConfigManager::Cleanup() {
    std::lock_guard<std::recursive_mutex> lock(g_config_mutex);
    delete s_doc;
    s_doc = nullptr;
}