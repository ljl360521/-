#pragma once

#include "imgui.h"
#include "il2cpp.h"
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <chrono>
#include <deque>
#include <functional>

// ===== 静默自瞄数据 =====
struct SilentAimData {
    std::atomic<bool> enabled;
    std::atomic<bool> has_target;
    std::atomic<bool> penetrate;      // 新增：穿墙开关

    std::atomic<float> self_x, self_y, self_z;
    std::atomic<float> aim_dir_x, aim_dir_y, aim_dir_z;
    std::atomic<float> target_x, target_y, target_z;  // 新增：目标坐标

    SilentAimData() : enabled(false), has_target(false), penetrate(false),
        self_x(0), self_y(0), self_z(0),
        aim_dir_x(0), aim_dir_y(0), aim_dir_z(0),
        target_x(0), target_y(0), target_z(0) {}
};

extern SilentAimData g_silentAimData;

// ===== 抓包数据 =====
struct CapturedPacket {
    uint32_t index;
    std::chrono::steady_clock::time_point timestamp;
    uintptr_t udpPtr;
    int16_t  module;
    uint16_t cmd;
    bool     reliable;
    bool     modified;          // 是否被静默自瞄修改过
    std::vector<uint8_t> payload;
    std::string summary;
};

class NetPacketHook {
private:
    std::mutex mtx;
    bool initialized;
    bool hooked;

    void* hook_addr;

    std::deque<CapturedPacket> packets;
    uint32_t packet_counter;
    int max_packets;

    bool capture_enabled;
    bool auto_scroll;
    bool show_payload_hex;
    bool pause_capture;

    bool filter_enabled;
    int  filter_module;
    int  filter_cmd;
    bool filter_reliable_only;
    bool filter_unreliable_only;
    bool filter_modified_only;      // 只显示被修改的包

    uint64_t total_packets;
    uint64_t total_bytes;
    uint64_t modified_packets;      // 被修改的射击包计数
    std::chrono::steady_clock::time_point start_time;

    int selected_packet_idx;
    char offset_input[32];

    bool hookViaIl2cpp();
    bool hookAtAddr(void* addr);

public:
    NetPacketHook();
    ~NetPacketHook();

    bool initialize();
    void cleanup();

    void onPacketCaptured(uintptr_t udpPtr,
                          int16_t module, uint16_t cmd,
                          const uint8_t* buff, uint32_t len,
                          bool reliable, bool modified);

    void drawControlPanel();

    bool isInitialized() const { return initialized; }
    bool isHooked() const { return hooked; }
};

extern NetPacketHook g_netPacketHook;
// net_packet_hook.h 末尾加:
typedef int32_t (*libUdpSend_t)(void* udpptr, int16_t module, uint16_t cmd,
                                void* buff, uint32_t len, uint16_t reliable,
                                const MethodInfo* method);
extern libUdpSend_t g_original_libUdpSend;