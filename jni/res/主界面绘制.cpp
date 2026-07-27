#include "主界面绘制.h"
#include "全局状态.h"
#include "配置初始化.h"
#include "主题样式.h"
#include "触摸输入.h"
#include "触摸窗口.h"
#include "输入法桥接.h"
#include "防录屏.h"
#include "音量键缩放.h"
#include "卡密验证.h"
#include "音频标签页.h"
#include "液态玻璃.h"
#include "游戏函数调用.h"
#include "res/ImGenie.h"
#include "res/ImCoolBar.h"
#include "imgui.h"
#include <cfloat>
#include <cstring>

namespace {

// 双击滑块弹出输入法精确修改数值
// popupId 需要唯一，用于区分不同滑块的弹出窗口
static void SliderFloatWithInput(const char* label, float* v, float v_min, float v_max, const char* format, const char* popupId)
{
    ImGui::SliderFloat(label, v, v_min, v_max, format);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseDoubleClicked(0)) {
        ImGui::OpenPopup(popupId);
    }
    if (ImGui::BeginPopup(popupId)) {
        ImGui::Text("精确输入");
        ImGui::SameLine();
        ImGui::PushItemWidth(120);
        ImGui::InputFloat("##input", v, 0.0f, 0.0f, "%.3f");
        ImGui::PopItemWidth();
        // 限制在范围内
        if (*v < v_min) *v = v_min;
        if (*v > v_max) *v = v_max;
        if (ImGui::Button("确定")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("取消")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

static void DrawMemoryMoveButton(const char* label, float x, float y, float z, const ImVec2& size)
{
    ImGui::Button(label, size);

    // 方向按钮只在按下瞬间把游戏摇杆拨到对应方向一次；松手后不再归零/停止，让游戏按当前方向自然继续。
    if (ImGui::IsItemActivated()) {
        std::string msg;
        GameCall_InvokeMove(x, y, z, msg);
    }
}

static void DrawMemoryMoveButtons()
{
    const ImVec2 btnSize(180, 58);
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float indent = (btnSize.x + gap) * 0.5f;

    ImGui::Indent(indent);
    DrawMemoryMoveButton("摇杆往上", 0.0f, 1.0f, 0.0f, btnSize);
    ImGui::Unindent(indent);

    DrawMemoryMoveButton("摇杆往左", -1.0f, 0.0f, 0.0f, btnSize);
    ImGui::SameLine();
    DrawMemoryMoveButton("摇杆往右", 1.0f, 0.0f, 0.0f, btnSize);

    ImGui::Indent(indent);
    DrawMemoryMoveButton("摇杆往下", 0.0f, -1.0f, 0.0f, btnSize);
    ImGui::Unindent(indent);
}

} // namespace

void DrawOldMainUI()
{
    std::string notice, endTime;
    {
        std::lock_guard<std::mutex> lock(g_t3_mutex);
        notice = g_t3_notice;
        endTime = g_t3_end_time;
    }

    if (ImGui::BeginTabBar("MainTabBar")) {
        // ---------- 说明 ----------
        if (BeginMainTabItem("说明", 0)) {
            ImGui::TextWrapped("%s", notice.empty() ? "暂无公告" : notice.c_str());
            ImGui::Separator();
            ImGui::Text("资源链接：");
            if (ImGui::Button("访问网盘")) {
                OpenUrlByActivity("https://share.feijipan.com/s/uGQ1k9ij");
            }
            ImGui::SameLine(); ImGui::Text("点击打开浏览器");
            ImGui::EndTabItem();
        }

        // ---------- 内存 ----------
        if (BeginMainTabItem("内存", 1)) {
            if (ImGui::Button("调用分身", ImVec2(180, 0))) {
                std::string msg;
                GameCall_InvokeDevide(msg);
            }
            ImGui::SameLine();
            if (ImGui::Button("调用吐球", ImVec2(180, 0))) {
                std::string msg;
                GameCall_InvokeSpitBall(msg);
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("吐球加速");
            ImGui::Checkbox("开启##spitacc", &g_spitball_accelerate);
            ImGui::SameLine();
            ImGui::PushItemWidth(200);
            ImGui::SliderInt("每帧次数##spitspd", &g_spitball_speed, 1, 20, "%d 次/帧");
            ImGui::PopItemWidth();
            ImGui::Separator();
            ImGui::Text("三角合球");
            {
                const char* btnLabel = g_triangle_running ? "停止三角" : "三角合球";
                if (ImGui::Button(btnLabel, ImVec2(180, 0))) {
                    std::string msg;
                    GameCall_TriangleMerge(msg);
                }
            }
            ImGui::Separator();
            ImGui::Text("视野修改");
            ImGui::Checkbox("开启##view", &g_view_enabled);
            ImGui::SameLine();
            ImGui::PushItemWidth(200);
            SliderFloatWithInput("视野倍率##viewscl", &g_view_scale, 1.0f, 5.0f, "%.1fx", "popup_view_scale");
            ImGui::PopItemWidth();
            ImGui::Separator();
            ImGui::Text("粘合修改");
            ImGui::Checkbox("开启##merge", &g_merge_enabled);
            ImGui::SameLine();
            ImGui::PushItemWidth(200);
            SliderFloatWithInput("粘合值##mergeval", &g_merge_value, 0.1f, 1.5f, "%.3f", "popup_merge_value");
            ImGui::PopItemWidth();
            ImGui::Separator();
            ImGui::Text("排名名字修改");
            if (ImGui::Checkbox("开启##rename", &g_rename_enabled)) {
                if (!g_rename_enabled) {
                    // 总开关关闭：立刻恢复所有人原名
                    std::string msg;
                    GameCall_RestoreAllNames(msg);
                    g_rename_was_enabled = false;
                }
            }
            ImGui::Checkbox("详细排名##rename_detail", &g_rename_detailed_rank);
            ImGui::SameLine();
            if (ImGui::Checkbox("显示自我##rename_self", &g_rename_show_self)) {
                // 勾选变化当下立刻跑一轮：开=改自己，关=恢复原名
                if (g_rename_enabled) {
                    std::string msg;
                    GameCall_RenameToRank(msg);
                }
            }
            ImGui::SameLine();
            ImGui::Checkbox("名字上色##rename_color", &g_rename_use_color);
            if (g_rename_use_color) {
                ImGui::SameLine();
                ImGui::ColorEdit3("##rename_col", (float*)&g_rename_name_color,
                    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_DisplayHex);
            }
            ImGui::Separator();
            ImGui::Separator();
            ImGui::Text("名字大小（独立功能）");
            ImGui::Checkbox("开启##namescale", &g_name_scale_enabled);
            if (g_name_scale_enabled) {
                ImGui::SliderFloat("系数##namescalef", &g_name_scale, 0.5f, 6.0f, "%.3f");
            }


            DrawMemoryMoveButtons();
            ImGui::EndTabItem();
        }

        // ---------- 设置 ----------
        if (BeginMainTabItem("设置", 2)) {
            ImGui::Text("主题选择"); ImGui::Separator();
            if (g_current_theme_index < Theme_Classic || g_current_theme_index > Theme_MusicUI) g_current_theme_index = Theme_LiquidGlass;
            const char* themes[] = {"经典色", "纯净白", "芭比粉", "液态玻璃", "音乐UI"};
            if (ImGui::Combo("##th", &g_current_theme_index, themes, 5)) {
                ApplyOldProjectTheme(g_current_theme_index);
                SaveAppConfigNow();
            }

            ImGui::Separator(); ImGui::Text("UI 大小");
            if (ImGui::SliderFloat("##sc", &g_ui_font_scale, 0.5f, 1.0f, "%.2f")) {
                ImGui::GetIO().FontGlobalScale = g_ui_font_scale;
                SaveAppConfigNow();
            }
            ImGui::Separator();

            if (ImGui::Checkbox("无权限防录屏", &g_secure_skip_screenshot)) {
                if (SetJavaSecureSurfaceMode(g_secure_skip_screenshot)) {
                    g_pending_secure_switch = true;
                    g_pending_secure_target = g_secure_skip_screenshot;
                } else {
                    g_secure_skip_screenshot = !g_secure_skip_screenshot;
                }
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("音量键调UI大小", &g_volume_key_ui_scale)) {
                SetVolumeScaleEnabled(g_volume_key_ui_scale);
                SaveAppConfigNow();
            }
            ImGui::Separator();

            ImGui::Text("卡密到期时间");
            if (g_t3_authed && !endTime.empty()) {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%s", endTime.c_str());
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "未激活");
            }
            ImGui::Separator();
            ImGui::EndTabItem();
        }

        // ---------- 液态玻璃 ----------
        // index 必须唯一（说明=0 内存=1 设置=2 日志=3 音频播放=4），与书写顺序无关。
        if (BeginMainTabItem("液态玻璃", 5)) {
            DrawLiquidGlassTab();
            ImGui::EndTabItem();
        }

        // ---------- 音频播放 ----------
        if (BeginMainTabItem("音频播放", 4)) {
            DrawUploadedUIInsideAudioTab();
            ImGui::EndTabItem();
        }

        // ---------- 日志 ----------
        if (BeginMainTabItem("日志", 3)) {
            if (ImGui::Button("弹出日志窗口")) g_show_log_window = true;
            ImGui::SameLine();
            if (ImGui::Button("清空日志")) ClearLog();
            ImGui::SameLine();
            if (ImGui::Button("保存日志")) {
                std::string logPath = g_config_base_dir + "/game_log.txt";
                FILE* f = fopen(logPath.c_str(), "w");
                if (f) {
                    fputs(g_log_text, f);
                    fclose(f);
                    AppendLog("[系统] 日志已保存到 %s", logPath.c_str());
                } else {
                    AppendLog("[错误] 保存日志失败：无法写入 %s", logPath.c_str());
                }
            }
            ImGui::Separator(); ImGui::Text("显示类型:");
            static bool log_flags[6] = {true,true,true,true,true,true};
            const char* lbl[] = {"系统","按键","用户","按钮","错误","其他"};
            for (int i = 0; i < 3; i++) { if (i > 0) ImGui::SameLine(); ImGui::Checkbox(lbl[i], &log_flags[i]); }
            for (int i = 3; i < 6; i++) { if (i > 3) ImGui::SameLine(); ImGui::Checkbox(lbl[i], &log_flags[i]); }
            ImGui::Separator();
            ImGui::InputText("##in", g_logInput, sizeof(g_logInput));
            ImGui::SameLine(); ImGui::Button("发送");
            ImGui::Separator();
            ImGui::BeginChild("Log", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::TextUnformatted(g_log_text);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

void DrawFloatingWindow() {
    ClearTouchWindows();
    // 每帧先清空，只有 Begin 后窗口确实没有折叠时才重新设置/登记。
    // 否则折叠状态可能残留上一帧窗口矩形，导致 Java 层继续拦截整块触摸。
    g_window = nullptr;
    const ImVec2 cen = ImGui::GetMainViewport()->GetCenter();
    // 允许手动缩小到接近左上角折叠按钮大小；同步降低全局最小尺寸，避免主题 WindowMinSize 卡住。
    const ImVec2 minSize(58.0f, ImGui::GetFrameHeight());
    ImGui::GetStyle().WindowMinSize = minSize;

    if (!g_t3_authed) {
        // ==================== 登录窗口：旧版一比一参数 ====================
        ImGui::SetNextWindowSize(ImVec2(788, 420), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(minSize, ImVec2(FLT_MAX, FLT_MAX));
        ImGui::SetNextWindowPos(cen, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

        bool loginWindowVisible = ImGui::Begin("句号的辅助");
        g_window = ImGui::GetCurrentWindow();
        RegisterTouchWindow(g_window);
        if (loginWindowVisible) {
            DrawT3AuthUI();
        }
        ImGui::End();
    } else {
        // ==================== 主窗口：旧版 Phase 1 骨架 ====================
        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(minSize, ImVec2(FLT_MAX, FLT_MAX));
        ImGui::SetNextWindowPos(cen, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

        bool mainWindowVisible = ImGui::Begin("测试", nullptr, ImGuiWindowFlags_None);
        g_window = ImGui::GetCurrentWindow();
        RegisterTouchWindow(g_window);
        if (mainWindowVisible) {
            DrawOldMainUI();
        }
        ImGui::End();

        if (g_show_log_window) {
            ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSizeConstraints(minSize, ImVec2(FLT_MAX, FLT_MAX));
            ImGui::SetNextWindowPos(ImVec2(cen.x - 350, cen.y - 250), ImGuiCond_FirstUseEver, ImVec2(0.0f, 0.0f));
            bool logWindowVisible = ImGui::Begin("详细日志窗口", &g_show_log_window);
            ImGuiWindow* logWindow = ImGui::GetCurrentWindow();
            if (logWindow) RegisterTouchWindow(logWindow);
            if (logWindowVisible) {
                ImGui::Button("清空日志");
                ImGui::SameLine(); ImGui::Text("数量: %d", 0);
                ImGui::BeginChild("LP", ImVec2(0, 0), true);
                ImGui::EndChild();
            }
            ImGui::End();
        }

        if (g_show_another_window) {
            ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSizeConstraints(minSize, ImVec2(FLT_MAX, FLT_MAX));
            ImGui::SetNextWindowPos(ImVec2(cen.x + 50, cen.y - 150), ImGuiCond_FirstUseEver, ImVec2(0.0f, 0.0f));

            if (ImGenie::Begin("另一个窗口", &g_show_another_window, ImGuiWindowFlags_None, &g_GenieParams)) {
                ImGuiWindow* anotherWindow = ImGui::GetCurrentWindow();
                if (anotherWindow) RegisterTouchWindow(anotherWindow);
                ImGui::Text("来自另一个窗口的问候！");
                if (ImGui::Button("关闭我")) g_show_another_window = false;
                ImGui::Separator();

                ImCoolBarConfig coolbar_cfg;
                coolbar_cfg.anchor                  = ImVec2(0.5f, 0.5f);
                coolbar_cfg.normal_size             = 40.0f;
                coolbar_cfg.hovered_size            = 60.0f;
                coolbar_cfg.embed_mode              = true;
                coolbar_cfg.mouse_smoothing_ms      = 80.0f;
                coolbar_cfg.anim_smoothing_ms       = 100.0f;
                coolbar_cfg.effect_strength         = 0.5f;
                coolbar_cfg.local_antialiasing      = true;
                coolbar_cfg.frame_rounding_override = 6.0f;

                if (ImGui::BeginCoolBar("##DockBar", ImCoolBarFlags_Horizontal, coolbar_cfg)) {
                    static const char* happy_chars[] = { "要", "天", "天", "开", "心", "呀" };
                    for (int i = 0; i < 6; i++) {
                        if (ImGui::CoolBarItem()) {
                            float w = ImGui::GetCoolBarItemWidth();
                            ImGui::Button(happy_chars[i], ImVec2(w, w));
                        }
                    }
                    ImGui::EndCoolBar();
                }
                ImGenie::End();
            }
        }

        // 吐球加速：每帧直接调用 NetworkUpdater.ReqFreeType(0,1) N 次
        if (g_spitball_accelerate && g_spitball_speed > 0) {
            std::string msg;
            GameCall_InvokeSpitBallDirect(g_spitball_speed, msg);
        }

        // 视野修改：只在开启时每帧设置，关闭时只 reset 一次
        if (g_view_enabled && g_view_scale > 1.0f) {
            std::string msg;
            GameCall_SetViewScale(g_view_scale, msg);
            g_view_was_enabled = true;
        } else if (g_view_was_enabled) {
            GameCall_ResetViewPullup();
            g_view_was_enabled = false;
        }

        // 粘合修改：只在开启时每帧设置，关闭时只 reset 一次
        if (g_merge_enabled) {
            std::string msg;
            GameCall_SetMerge(g_merge_value, msg);
            g_merge_was_enabled = true;
        } else if (g_merge_was_enabled) {
            GameCall_ResetMerge();
            g_merge_was_enabled = false;
        }

        // 名字大小：独立；每帧改 NameText(LeLable).SelfTF.localScale
        // 系数默认 1.875=游戏公式末尾值；关闭时还原子节点 scale=1
        {
            static bool name_scale_was_on = false;
            if (g_name_scale_enabled) {
                GameCall_TickNameScale(g_name_scale);
                name_scale_was_on = true;
            } else if (name_scale_was_on) {
                GameCall_TickNameScale(1.875f); // 还原
                // 再强制把 NameText.localScale 设回 1
                GameCall_TickNameScale(0.f); // 0 表示 restore mode
                name_scale_was_on = false;
            }
        }

        // 排名名字修改：开启时周期性改名；关闭瞬间恢复全员原名
        if (g_rename_enabled) {
            static double last_rename_time = 0.0;
            double now = ImGui::GetTime();
            if (now - last_rename_time >= 4.0) {
                std::string msg;
                GameCall_RenameToRank(msg);
                last_rename_time = now;
            }
            g_rename_was_enabled = true;
        } else if (g_rename_was_enabled) {
            // 总开关从开→关（含非 checkbox 路径）
            std::string msg;
            GameCall_RestoreAllNames(msg);
            g_rename_was_enabled = false;
        }
    }
}
