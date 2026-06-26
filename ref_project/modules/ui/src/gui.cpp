#include "gui.h"
#include "il2cpp.h"
#include "esp_system.h"
#include "physx_visual.h"
#include "player_esp.h"
#include "item_esp.h"
#include "utility_panel.h"
#include "native_register_vk.h"
#include "ImGuiInputIME.h"
#include "bullet_trail.h"

#include <thread>
#include <atomic>
#include <chrono>
#include <cstring>

// 定义编译配置宏
#ifndef RELEASE_MODE
#define DEBUG_MODE
#endif
static bool s_AntiScreenCapture = true;

// ===== Dump =====

#ifdef DEBUG_MODE
static std::atomic<bool> g_dumping(false);
static std::atomic<bool> g_dumpDone(false);
static std::atomic<bool> g_dumpSuccess(false);
static std::string g_dumpMsg;
static std::mutex g_dumpMsgMutex;
static std::chrono::steady_clock::time_point g_dumpStartTime;

static void dumpThreadFunc(std::string path) {
    bool ok = false; std::string msg;
    if (g_il2cpp_tool && g_il2cpp_tool->isInitialized()) {
        auto t = g_il2cpp_tool->attachCurrentThread();
        ok = g_il2cpp_tool->dumpGameData(path);
        if (t) g_il2cpp_tool->detachThread(t);
        if (ok) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - g_dumpStartTime).count();
            msg = "OK: " + path + " (" + std::to_string(ms) + "ms)";
        } else msg = "Dump 失败";
    } else msg = "未初始化";
    { std::lock_guard<std::mutex> l(g_dumpMsgMutex); g_dumpMsg = msg; }
    g_dumpSuccess.store(ok); g_dumpDone.store(true); g_dumping.store(false);
}


#endif

// ===== 主绘制 =====

void 绘制控件() {
    ImGui::SetNextWindowPos(ImVec2(屏宽/2-750/2, 100), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(750, 550), ImGuiCond_FirstUseEver);
    ImGui::Begin("Menu");
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    g_MainWindowBounds[0] = win->Pos.x;
    g_MainWindowBounds[1] = win->Pos.y;
    g_MainWindowBounds[2] = win->Pos.x + win->Size.x;
    g_MainWindowBounds[3] = win->Pos.y + win->Size.y;

    if (ImGui::BeginTabBar("Tabs")) {

        if (ImGui::BeginTabItem("玩家")) {
            if (!g_player_esp.isInitialized() && g_il2cpp_tool && g_il2cpp_tool->isInitialized())
                g_player_esp.initialize();
            g_player_esp.drawControlPanel();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("物品")) {
            if (!g_item_esp.isInitialized() && g_il2cpp_tool && g_il2cpp_tool->isInitialized())
                g_item_esp.initialize();
            g_item_esp.drawControlPanel();
            ImGui::EndTabItem();
        }
        /*
        if (ImGui::BeginTabItem("自瞄")) {
            if (!g_aimbot.isInitialized() && g_il2cpp_tool && g_il2cpp_tool->isInitialized())
                g_aimbot.initialize();
            if (!g_netPacketHook.isInitialized())
                g_netPacketHook.initialize();
            g_aimbot.drawControlPanel();
            ImGui::EndTabItem();
        }*/

#ifdef DEBUG_MODE
        if (ImGui::BeginTabItem("ESP调试")) {
            if (!g_esp.isInitialized() && g_il2cpp_tool && g_il2cpp_tool->isInitialized())
                g_esp.initialize();
            g_esp.drawControlPanel();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("PhysX")) {
            if (!g_physx_visual.isInitialized() && g_il2cpp_tool && g_il2cpp_tool->isInitialized())
                g_physx_visual.initialize();
            g_physx_visual.drawControlPanel();
            ImGui::EndTabItem();
        }
        
     
#endif

        if (ImGui::BeginTabItem("实用功能")) {
            g_utilityPanel.drawTabContent();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("操作")) {
            //g_autoAction.drawControlPanel();
            ImGui::EndTabItem();
        }

#ifdef DEBUG_MODE
        if (ImGui::BeginTabItem("Dump")) {
            static char dp[512] = "/storage/emulated/0/Android/data/com.pi.czrxdfirst/dump.cs";
            ImGuiIME::InputText("路径", dp, sizeof(dp),400);
            ImGui::SameLine();
            if (g_dumping.load()) {
                auto s = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - g_dumpStartTime).count();
                const char* sp[] = {"|", "/", "-", "\\"};
                ImGui::Text("%s %llds", sp[(int)(ImGui::GetTime() * 8) % 4], (long long)s);
            } else {
                if (ImGui::Button("Dump")) {
                    if (g_il2cpp_tool && g_il2cpp_tool->isInitialized()) {
                        g_dumping.store(true); g_dumpDone.store(false);
                        g_dumpStartTime = std::chrono::steady_clock::now();
                        std::thread(dumpThreadFunc, std::string(dp)).detach();
                    } else {
                        g_dumpDone.store(true); g_dumpSuccess.store(false);
                        std::lock_guard<std::mutex> l(g_dumpMsgMutex); g_dumpMsg = "未初始化";
                    }
                }
            }
            if (g_dumpDone.load()) {
                std::lock_guard<std::mutex> l(g_dumpMsgMutex);
                ImGui::TextColored(g_dumpSuccess.load() ? ImVec4(0,1,0,1) : ImVec4(1,0,0,1),
                    "%s", g_dumpMsg.c_str());
            }
            ImGui::EndTabItem();
        }
        
#endif
        if (ImGui::BeginTabItem("设置")) {
            if (ImGui::Checkbox("防录屏/截图", &s_AntiScreenCapture)) {
                setSurfaceSecurity(s_AntiScreenCapture, s_AntiScreenCapture);
            }
            ImGui::EndTabItem();
        }
        
        ImGui::EndTabBar();
    }

    ImGui::End();

    // ===== 每帧更新 =====
    g_utilityPanel.update(ImGui::GetIO().DeltaTime);

    if (g_player_esp.isInitialized()) {
        g_player_esp.update(ImGui::GetIO().DeltaTime);
        g_player_esp.drawESP();

        // 同步核心实例给 ItemESP
        if (g_player_esp.isCoreFound() && g_player_esp.getCoreInstance()) {
            if (g_item_esp.getCoreInstance() != g_player_esp.getCoreInstance()) {
                g_item_esp.setCoreInstance(g_player_esp.getCoreInstance());
            }
        } else {
            // PlayerESP 核心丢失时同步清除
            if (g_item_esp.getCoreInstance() != nullptr) {
                g_item_esp.setCoreInstance(nullptr);
            }
        }
    }
  /*  if (g_aimbot.isInitialized()) {
        g_aimbot.update(ImGui::GetIO().DeltaTime);
        g_aimbot.drawESP();
    }*/

    if (g_item_esp.isInitialized()) {
        g_item_esp.update(ImGui::GetIO().DeltaTime);
        g_item_esp.drawESP();
    }

#ifdef DEBUG_MODE
    if (g_esp.isInitialized()) {
        g_esp.update(ImGui::GetIO().DeltaTime);
        g_esp.drawESP();
    }
    if (g_physx_visual.isInitialized()) {
        g_physx_visual.update(ImGui::GetIO().DeltaTime);
        g_physx_visual.drawColliders();
    }
#endif
}
