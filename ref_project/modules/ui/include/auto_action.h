#pragma once

#include "imgui.h"
#include "net_packet_hook.h"
#include <atomic>
#include <cstdint>
#include <chrono>

// ===== 设置 =====
struct AutoActionSettings {
    bool auto_reload = false;           // 自动换弹
    bool auto_switch_empty = false;     // 换弹后自动切枪
    bool auto_weapon_switch = false;    // 自动切枪（通用）
    bool auto_mojinfu = false;          // 自动切摸金符到2号位
    bool auto_jetpack_switch = false;   // 自动切喷包
    float jetpack_heat_value = 500.0f;  // 喷包热值阈值
    bool auto_coffin_open = false;      // 秒开棺
};

extern AutoActionSettings g_autoActionSettings;

// ===== 包构造 + 发送 =====
class AutoAction {
private:
    // 冷却时间管理
    std::chrono::steady_clock::time_point last_reload_time;
    std::chrono::steady_clock::time_point last_switch_time;
    std::chrono::steady_clock::time_point last_coffin_time;

    static constexpr int COOLDOWN_MS = 1000;  // 1秒冷却

    bool canAction(std::chrono::steady_clock::time_point& lastTime);

    // 发送包的底层函数
    bool sendPacket(const uint8_t* data, int len);

public:
    AutoAction();

    // ===== 手动触发的操作 =====

    // 切换到指定物品栏位置 (slot: 0~5)
    bool switchToSlot(uint8_t slot);

    // 换弹
    bool reload();

    // 开棺 (sceneId: 棺材的场景ID)
    bool openCoffin(uint32_t coffinSceneId);

    // 取消开棺
    bool cancelCoffin(uint32_t coffinSceneId);

    // ===== 面板 =====
    void drawControlPanel();
};

extern AutoAction g_autoAction;