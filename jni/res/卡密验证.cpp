#include "卡密验证.h"
#include "全局状态.h"
#include "配置初始化.h"
#include "输入法桥接.h"
#include "imgui.h"
#include <cstring>
#include <cstdio>

void T3_SetStatus(const std::string& s)
{
    std::lock_guard<std::mutex> lock(g_t3_mutex);
    g_t3_status = s;
}

std::string T3_GetStatus()
{
    std::lock_guard<std::mutex> lock(g_t3_mutex);
    return g_t3_status;
}

void T3_InitOnce()
{
    std::call_once(g_t3_init_once, [](){
        g_t3_verify = new T3Verify();
        if (!g_t3_verify->initRSA(T3_LOGIN_CODE, T3_NOTICE_CODE, T3_VERSION_CODE,
                                  T3_HEARTBEAT_CODE, T3_APPKEY, T3_RSA_PUBLIC_KEY))
        {
            T3_SetStatus("验证系统初始化失败");
            g_t3_ready = false;
            return;
        }
        g_t3_ready = true;
        T3_SetStatus("验证系统已就绪，请输入卡密");

        // 公告/版本检查放在线程中，避免首次绘制卡顿。
        std::thread([](){
            if (!g_t3_verify) return;
            auto vr = g_t3_verify->getLatestVersion();
            auto nr = g_t3_verify->getNotice();
            std::lock_guard<std::mutex> lock(g_t3_mutex);
            if (vr.success && !vr.version.empty()) {
                if (vr.version > LOCAL_VERSION) g_t3_status = "版本过旧，请更新后使用";
            }
            if (nr.success && !nr.notice.empty()) g_t3_notice = nr.notice;
        }).detach();
    });
}

void T3_StartHeartbeat()
{
    if (g_t3_heartbeat_running.exchange(true)) return;
    std::thread([](){
        while (g_t3_heartbeat_running) {
            for (int i = 0; i < 60 && g_t3_heartbeat_running; ++i)
                std::this_thread::sleep_for(std::chrono::seconds(1));
            if (!g_t3_heartbeat_running || !g_t3_verify || !g_t3_authed) break;

            std::string kami, statecode;
            {
                std::lock_guard<std::mutex> lock(g_t3_mutex);
                kami = g_t3_kami;
                statecode = g_t3_statecode;
            }
            T3Result hb = g_t3_verify->heartbeat(kami, statecode);
            if (!hb.success) {
                g_t3_authed = false;
                g_t3_heartbeat_running = false;
                T3_SetStatus(hb.error.empty() ? "心跳失败，请重新验证" : ("心跳失败：" + hb.error));
                break;
            }
        }
    }).detach();
}

void T3_LoginAsync(const char* kami)
{
    T3_InitOnce();
    if (!g_t3_ready || !g_t3_verify) {
        T3_SetStatus("验证系统未就绪");
        return;
    }
    if (!kami || !kami[0]) {
        T3_SetStatus("请输入卡密");
        return;
    }
    if (g_t3_verifying.exchange(true)) return;

    std::string kamiStr(kami);
    T3_SetStatus("验证中...");
    std::thread([kamiStr](){
        T3LoginResult r = g_t3_verify->login(kamiStr, g_t3_machine_code);
        if (r.success) {
            {
                std::lock_guard<std::mutex> lock(g_t3_mutex);
                g_t3_kami = kamiStr;
                g_t3_statecode = r.statecode;
                g_t3_end_time = r.end_time;
                g_t3_status = "验证成功";
                // 旧项目逻辑：登录成功后保存卡密，供下次自动登录。
                g_kami_saved_valid = true;
                g_saved_kami = kamiStr;
            }
            SaveAppConfigNow();
            g_t3_authed = true;
            T3_StartHeartbeat();
        } else {
            g_t3_authed = false;
            if (g_t3_auto_login_running && kamiStr == g_saved_kami) {
                g_kami_saved_valid = false;
                SaveAppConfigNow();
            }
            T3_SetStatus(r.error.empty() ? "验证失败" : ("验证失败：" + r.error));
        }
        g_t3_auto_login_running = false;
        g_t3_verifying = false;
    }).detach();
}

void T3_TryAutoLoginFromConfig()
{
    if (g_t3_auto_login_tried || g_t3_authed || g_t3_verifying) return;
    if (!g_app_config_ready) InitAppConfigOnce();
    if (!g_app_config_ready) return;
    if (!g_kami_saved_valid || g_saved_kami.empty()) return;

    g_t3_auto_login_tried = true;
    g_t3_auto_login_running = true;
    T3_SetStatus("发现已保存卡密，尝试自动登录...");
    T3_LoginAsync(g_saved_kami.c_str());
}


void DrawT3AuthUI()
{
    T3_InitOnce();
    T3_TryAutoLoginFromConfig();
    static char kami[128] = "";
    static bool g_show_err = false;
    static char g_err_msg[256] = "";

    // 一比一复刻旧版登录 UI：只保留旧代码中的文字、输入框、按钮和提示。
    ImGui::Text("请输入卡密：");
    if (ImGui::InputText("##pwd", kami, sizeof(kami))) {}
    bool inputActive = ImGui::IsItemActive();
    bool inputClicked = ImGui::IsItemClicked();
    if ((inputClicked || (inputActive && !g_ime_last_input_active)) && !g_ime_last_want_text) {
        ImeShowKeyboard(true);
        g_ime_last_want_text = true;
    }
    g_ime_last_input_active = inputActive;

    bool btn = ImGui::Button("验证");
    if (btn && !g_t3_verifying) {
        if (!*kami) {
            g_show_err = true;
            snprintf(g_err_msg, sizeof(g_err_msg), "%s", "请输入卡密。");
        } else {
            g_show_err = false;
            T3_LoginAsync(kami);
        }
    }

    std::string status, endTime;
    {
        std::lock_guard<std::mutex> lock(g_t3_mutex);
        status = g_t3_status;
        endTime = g_t3_end_time;
    }
    if (!status.empty() && status.rfind("验证失败", 0) == 0) {
        g_show_err = true;
        snprintf(g_err_msg, sizeof(g_err_msg), "%s", status.c_str());
    }

    if (g_show_err)  ImGui::TextColored(ImVec4(1, 0, 0, 1), "%s", g_err_msg);
    if (g_t3_verifying) ImGui::TextColored(ImVec4(1, 1, 0, 1), "验证中...");
    if (g_t3_authed && !endTime.empty()) ImGui::Text("到期：%s", endTime.c_str());
}
