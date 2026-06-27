#pragma once

#include "imgui.h"

static float size = 28.0f;

namespace ImGui
{ 
    // 抹茶绿(亮色)
    inline void ApplyMatchaGreenTheme()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style = ImGuiStyle();

        style.ScaleAllSizes(size / 10);
        ImVec4* colors = style.Colors;

        // 基础颜色
        colors[ImGuiCol_Text]                   = ImVec4(0.00f, 0.20f, 0.10f, 1.00f);  // 深绿文字
        colors[ImGuiCol_TextDisabled]           = ImVec4(0.40f, 0.55f, 0.45f, 1.00f);  // 灰绿文字
        colors[ImGuiCol_WindowBg]               = ImVec4(0.95f, 0.98f, 0.93f, 1.00f);  // 浅抹茶背景
        colors[ImGuiCol_ChildBg]                = ImVec4(0.96f, 0.98f, 0.94f, 0.50f);  // 半透子窗口
        colors[ImGuiCol_PopupBg]                = ImVec4(0.96f, 0.98f, 0.95f, 0.95f);  // 弹窗背景

        // 边框颜色
        colors[ImGuiCol_Border]                 = ImVec4(0.60f, 0.70f, 0.55f, 0.50f);  // 浅绿边框
        colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);  // 无阴影

        // 框架颜色（按钮、输入框等）
        colors[ImGuiCol_FrameBg]                = ImVec4(0.85f, 0.92f, 0.82f, 0.50f);  // 框架背景
        colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.75f, 0.88f, 0.72f, 0.50f);  // 悬停
        colors[ImGuiCol_FrameBgActive]          = ImVec4(0.65f, 0.82f, 0.62f, 0.50f);  // 激活

        // 标题栏
        colors[ImGuiCol_TitleBg]                = ImVec4(0.65f, 0.82f, 0.62f, 0.70f);  // 标题背景
        colors[ImGuiCol_TitleBgActive]          = ImVec4(0.55f, 0.75f, 0.52f, 1.00f);  // 激活标题
        colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.85f, 0.92f, 0.82f, 0.70f);  // 折叠标题

        // 菜单栏
        colors[ImGuiCol_MenuBarBg]              = ImVec4(0.90f, 0.95f, 0.88f, 0.90f);  // 菜单栏

        // 滚动条
        colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.92f, 0.96f, 0.90f, 0.50f);  // 滚动条背景
        colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.70f, 0.85f, 0.68f, 0.80f);  // 滚动条抓取
        colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.60f, 0.80f, 0.58f, 0.80f);  // 悬停
        colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.50f, 0.75f, 0.48f, 0.80f);  // 激活

        // 复选框、单选按钮
        colors[ImGuiCol_CheckMark]              = ImVec4(0.20f, 0.50f, 0.30f, 1.00f);  // 勾选标记

        // 滑块
        colors[ImGuiCol_SliderGrab]             = ImVec4(0.60f, 0.80f, 0.58f, 0.80f);  // 滑块抓取
        colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.50f, 0.75f, 0.48f, 1.00f);  // 激活

        // 按钮
        colors[ImGuiCol_Button]                 = ImVec4(0.70f, 0.85f, 0.68f, 0.70f);  // 普通按钮
        colors[ImGuiCol_ButtonHovered]          = ImVec4(0.60f, 0.80f, 0.58f, 0.80f);  // 悬停
        colors[ImGuiCol_ButtonActive]           = ImVec4(0.50f, 0.75f, 0.48f, 1.00f);  // 激活

        // 标题头（用于表格等）
        colors[ImGuiCol_Header]                 = ImVec4(0.70f, 0.85f, 0.68f, 0.50f);  // 标题头
        colors[ImGuiCol_HeaderHovered]          = ImVec4(0.60f, 0.80f, 0.58f, 0.80f);  // 悬停
        colors[ImGuiCol_HeaderActive]           = ImVec4(0.50f, 0.75f, 0.48f, 1.00f);  // 激活

        // 分隔线
        colors[ImGuiCol_Separator]              = ImVec4(0.60f, 0.70f, 0.55f, 0.50f);  // 分隔线
        colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.50f, 0.65f, 0.45f, 0.78f);  // 悬停
        colors[ImGuiCol_SeparatorActive]        = ImVec4(0.40f, 0.60f, 0.35f, 1.00f);  // 激活

        // 调整大小手柄
        colors[ImGuiCol_ResizeGrip]             = ImVec4(0.70f, 0.85f, 0.68f, 0.50f);  // 调整手柄
        colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.60f, 0.80f, 0.58f, 0.80f);  // 悬停
        colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.50f, 0.75f, 0.48f, 1.00f);  // 激活

        // 标签
        colors[ImGuiCol_Tab]                    = ImVec4(0.75f, 0.88f, 0.72f, 0.70f);  // 标签页
        colors[ImGuiCol_TabHovered]             = ImVec4(0.65f, 0.82f, 0.62f, 0.80f);  // 悬停
        colors[ImGuiCol_TabActive]              = ImVec4(0.55f, 0.75f, 0.52f, 0.90f);  // 激活
        colors[ImGuiCol_TabUnfocused]           = ImVec4(0.85f, 0.92f, 0.82f, 0.70f);  // 未聚焦
        colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.75f, 0.88f, 0.72f, 0.80f);  // 未聚焦但激活

        // 绘图相关
        colors[ImGuiCol_PlotLines]              = ImVec4(0.40f, 0.60f, 0.35f, 1.00f);  // 线条图
        colors[ImGuiCol_PlotLinesHovered]       = ImVec4(0.50f, 0.75f, 0.48f, 1.00f);  // 悬停
        colors[ImGuiCol_PlotHistogram]          = ImVec4(0.40f, 0.65f, 0.38f, 1.00f);  // 直方图
        colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(0.50f, 0.75f, 0.48f, 1.00f);  // 悬停

        // 表格
        colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.80f, 0.90f, 0.78f, 1.00f);  // 表头
        colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.70f, 0.80f, 0.65f, 1.00f);  // 强边框
        colors[ImGuiCol_TableBorderLight]       = ImVec4(0.80f, 0.88f, 0.75f, 1.00f);  // 弱边框
        colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);  // 行背景
        colors[ImGuiCol_TableRowBgAlt]          = ImVec4(0.93f, 0.96f, 0.91f, 0.50f);  // 交替行背景

        // 拖拽目标
        colors[ImGuiCol_DragDropTarget]         = ImVec4(0.30f, 0.70f, 0.40f, 0.90f);  // 拖放目标

        // 导航相关
        colors[ImGuiCol_NavHighlight]           = ImVec4(0.50f, 0.75f, 0.48f, 1.00f);  // 导航高亮
        colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(0.55f, 0.75f, 0.52f, 0.70f);  // 窗口化高亮
        colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.88f, 0.78f, 0.20f);  // 窗口化暗背景

        // 模态窗口暗背景
        colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.80f, 0.88f, 0.78f, 0.35f);  // 模态窗口背景
    }

    // 抹茶绿(暗色)
    inline void ApplyMatchaGreenDarkTheme()
    {
        ImGuiStyle& style = ImGui::GetStyle();

        // 保留Classic主题的所有非颜色属性
        style = ImGuiStyle();

        style.ScaleAllSizes(size / 10);

        // 暗色抹茶绿色系配置
        ImVec4* colors = style.Colors;

        // 基础颜色
        colors[ImGuiCol_Text]                   = ImVec4(0.80f, 0.95f, 0.85f, 1.00f);  // 亮绿文字
        colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.65f, 0.55f, 1.00f);  // 暗绿文字
        colors[ImGuiCol_WindowBg]               = ImVec4(0.10f, 0.15f, 0.12f, 1.00f);  // 深绿背景
        colors[ImGuiCol_ChildBg]                = ImVec4(0.12f, 0.18f, 0.15f, 0.50f);  // 半透子窗口
        colors[ImGuiCol_PopupBg]                = ImVec4(0.15f, 0.20f, 0.17f, 0.95f);  // 弹窗背景

        // 边框颜色
        colors[ImGuiCol_Border]                 = ImVec4(0.30f, 0.40f, 0.35f, 0.50f);  // 暗绿边框
        colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);  // 无阴影

        // 框架颜色（按钮、输入框等）
        colors[ImGuiCol_FrameBg]                = ImVec4(0.20f, 0.28f, 0.22f, 0.50f);  // 框架背景
        colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.25f, 0.35f, 0.28f, 0.50f);  // 悬停
        colors[ImGuiCol_FrameBgActive]          = ImVec4(0.30f, 0.40f, 0.32f, 0.50f);  // 激活

        // 标题栏
        colors[ImGuiCol_TitleBg]                = ImVec4(0.15f, 0.25f, 0.20f, 0.70f);  // 标题背景
        colors[ImGuiCol_TitleBgActive]          = ImVec4(0.20f, 0.35f, 0.25f, 1.00f);  // 激活标题
        colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.15f, 0.25f, 0.20f, 0.70f);  // 折叠标题

        // 菜单栏
        colors[ImGuiCol_MenuBarBg]              = ImVec4(0.15f, 0.22f, 0.18f, 0.90f);  // 菜单栏

        // 滚动条
        colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.12f, 0.18f, 0.15f, 0.50f);  // 滚动条背景
        colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.30f, 0.45f, 0.38f, 0.80f);  // 滚动条抓取
        colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.35f, 0.50f, 0.42f, 0.80f);  // 悬停
        colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.40f, 0.55f, 0.45f, 0.80f);  // 激活

        // 复选框、单选按钮
        colors[ImGuiCol_CheckMark]              = ImVec4(0.40f, 0.70f, 0.50f, 1.00f);  // 亮绿勾选标记

        // 滑块
        colors[ImGuiCol_SliderGrab]             = ImVec4(0.30f, 0.50f, 0.40f, 0.80f);  // 滑块抓取
        colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.40f, 0.60f, 0.48f, 1.00f);  // 激活

        // 按钮
        colors[ImGuiCol_Button]                 = ImVec4(0.25f, 0.40f, 0.32f, 0.70f);  // 普通按钮
        colors[ImGuiCol_ButtonHovered]          = ImVec4(0.30f, 0.50f, 0.38f, 0.80f);  // 悬停
        colors[ImGuiCol_ButtonActive]           = ImVec4(0.35f, 0.55f, 0.42f, 1.00f);  // 激活

        // 标题头（用于表格等）
        colors[ImGuiCol_Header]                 = ImVec4(0.25f, 0.40f, 0.32f, 0.50f);  // 标题头
        colors[ImGuiCol_HeaderHovered]          = ImVec4(0.30f, 0.50f, 0.38f, 0.80f);  // 悬停
        colors[ImGuiCol_HeaderActive]           = ImVec4(0.35f, 0.55f, 0.42f, 1.00f);  // 激活

        // 分隔线
        colors[ImGuiCol_Separator]              = ImVec4(0.30f, 0.40f, 0.35f, 0.50f);  // 分隔线
        colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.35f, 0.45f, 0.40f, 0.78f);  // 悬停
        colors[ImGuiCol_SeparatorActive]        = ImVec4(0.40f, 0.50f, 0.45f, 1.00f);  // 激活

        // 调整大小手柄
        colors[ImGuiCol_ResizeGrip]             = ImVec4(0.25f, 0.40f, 0.32f, 0.50f);  // 调整手柄
        colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.30f, 0.50f, 0.38f, 0.80f);  // 悬停
        colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.35f, 0.55f, 0.42f, 1.00f);  // 激活

        // 标签
        colors[ImGuiCol_Tab]                    = ImVec4(0.20f, 0.35f, 0.28f, 0.70f);  // 标签页
        colors[ImGuiCol_TabHovered]             = ImVec4(0.25f, 0.45f, 0.35f, 0.80f);  // 悬停
        colors[ImGuiCol_TabActive]              = ImVec4(0.30f, 0.50f, 0.38f, 0.90f);  // 激活
        colors[ImGuiCol_TabUnfocused]           = ImVec4(0.15f, 0.25f, 0.20f, 0.70f);  // 未聚焦
        colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.20f, 0.35f, 0.28f, 0.80f);  // 未聚焦但激活

        // 绘图相关
        colors[ImGuiCol_PlotLines]              = ImVec4(0.40f, 0.70f, 0.50f, 1.00f);  // 线条图
        colors[ImGuiCol_PlotLinesHovered]       = ImVec4(0.50f, 0.80f, 0.60f, 1.00f);  // 悬停
        colors[ImGuiCol_PlotHistogram]          = ImVec4(0.35f, 0.65f, 0.45f, 1.00f);  // 直方图
        colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(0.45f, 0.75f, 0.55f, 1.00f);  // 悬停

        // 表格
        colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.20f, 0.30f, 0.25f, 1.00f);  // 表头
        colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.30f, 0.40f, 0.35f, 1.00f);  // 强边框
        colors[ImGuiCol_TableBorderLight]       = ImVec4(0.20f, 0.30f, 0.25f, 1.00f);  // 弱边框
        colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);  // 行背景
        colors[ImGuiCol_TableRowBgAlt]          = ImVec4(0.12f, 0.18f, 0.15f, 0.50f);  // 交替行背景

        // 拖拽目标
        colors[ImGuiCol_DragDropTarget]         = ImVec4(0.30f, 0.70f, 0.40f, 0.90f);  // 拖放目标

        // 导航相关
        colors[ImGuiCol_NavHighlight]           = ImVec4(0.40f, 0.70f, 0.50f, 1.00f);  // 导航高亮
        colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(0.40f, 0.70f, 0.50f, 0.70f);  // 窗口化高亮
        colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.10f, 0.15f, 0.12f, 0.20f);  // 窗口化暗背景

        // 模态窗口暗背景
        colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.10f, 0.15f, 0.12f, 0.35f);  // 模态窗口背景
    }

    // 舒适的暗色
    inline void ApplyComfortableDarkTheme() {
        ImGuiStyle &style = ImGui::GetStyle();

        style = ImGuiStyle();

        style.ScaleAllSizes(size / 10);

        ImVec4 *colors = style.Colors;

        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.11f, 0.13f, 0.95f);        // 深蓝灰色背景，护眼舒适
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.13f, 0.16f, 0.19f, 1.00f);         // 渐变中间色
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.18f, 0.22f, 0.27f, 1.00f);   // 柔和的激活状态蓝色调
        style.Colors[ImGuiCol_Text] = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);            // 柔和的米白色文字

        style.Colors[ImGuiCol_Border] = ImVec4(0.25f, 0.29f, 0.33f, 0.50f);          // 柔和的边框色
        style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.02f, 0.02f, 0.03f, 0.00f);    // 轻微阴影

        style.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);          // 默认按钮
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.34f, 0.39f, 1.00f);   // 悬停状态
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.16f, 0.45f, 0.68f, 1.00f);    // 点击状态 - 舒适蓝色

        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.18f, 0.21f, 1.00f);         // 输入框背景
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.26f, 0.31f, 1.00f);  // 输入框悬停
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.30f, 0.35f, 1.00f);   // 输入框激活

        style.Colors[ImGuiCol_Header] = ImVec4(0.20f, 0.25f, 0.29f, 0.55f);          // 标题栏
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.34f, 0.39f, 0.80f);   // 标题栏悬停
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.16f, 0.45f, 0.68f, 1.00f);    // 标题栏激活

        style.Colors[ImGuiCol_CheckMark] = ImVec4(0.16f, 0.45f, 0.68f, 1.00f);       // 对勾颜色
        style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.16f, 0.45f, 0.68f, 1.00f);      // 滑块抓取点
        style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.20f, 0.55f, 0.80f, 1.00f);// 滑块激活

        style.Colors[ImGuiCol_Tab] = ImVec4(0.13f, 0.16f, 0.19f, 1.00f);             // 未激活标签
        style.Colors[ImGuiCol_TabHovered] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);      // 标签悬停
        style.Colors[ImGuiCol_TabActive] = ImVec4(0.18f, 0.22f, 0.27f, 1.00f);       // 激活标签
        style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.10f, 0.13f, 0.15f, 1.00f);    // 非焦点标签
        style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);

        style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.13f, 0.16f, 0.19f, 1.00f);       // 菜单栏背景
        style.Colors[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.11f, 0.13f, 0.98f);         // 弹出菜单背景

        style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.08f, 0.10f, 0.12f, 1.00f);     // 滚动条背景
        style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.25f, 0.30f, 0.35f, 1.00f);   // 滚动条抓取点
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.36f, 0.42f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.16f, 0.45f, 0.68f, 1.00f);

        style.Colors[ImGuiCol_Separator] = ImVec4(0.25f, 0.29f, 0.33f, 0.50f);       // 分隔线
        style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.35f, 0.40f, 0.45f, 0.78f);
        style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.16f, 0.45f, 0.68f, 1.00f);
    }
    
    // 少女粉(亮色)
// Tesla Model 3 粉色主题
inline void ApplyPinkGirlTheme()
{
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();

    style.ScaleAllSizes(size / 10);
    ImVec4* colors = style.Colors;

    // Model 3 配色核心：深灰背景 + 高饱和粉色 + 亮白文字
    // 特点：现代感强、对比度高、科技感十足

    // 基础颜色
    colors[ImGuiCol_Text]                   = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);  // 亮白文字
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);  // 灰色禁用文字
    colors[ImGuiCol_WindowBg]               = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);  // 深灰背景（#1f1f1f）
    colors[ImGuiCol_ChildBg]                = ImVec4(0.15f, 0.15f, 0.15f, 0.80f);  // 子窗口背景
    colors[ImGuiCol_PopupBg]                = ImVec4(0.15f, 0.15f, 0.15f, 0.95f);  // 弹窗背景

    // 边框颜色
    colors[ImGuiCol_Border]                 = ImVec4(0.30f, 0.30f, 0.35f, 0.50f);  // 深灰边框
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);  // 无阴影

    // 框架颜色（按钮、输入框等）
    colors[ImGuiCol_FrameBg]                = ImVec4(0.18f, 0.18f, 0.20f, 0.60f);  // 框架背景
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.22f, 0.22f, 0.25f, 0.70f);  // 悬停
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.25f, 0.25f, 0.28f, 0.80f);  // 激活

    // 标题栏
    colors[ImGuiCol_TitleBg]                = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);  // 标题背景（更深）
    colors[ImGuiCol_TitleBgActive]          = ImVec4(1.00f, 0.30f, 0.60f, 1.00f);  // 激活标题（高饱和粉色）
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.15f, 0.15f, 0.15f, 0.80f);  // 折叠标题

    // 菜单栏
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.12f, 0.12f, 0.12f, 0.95f);  // 菜单栏

    // 滚动条
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.15f, 0.15f, 0.15f, 0.50f);  // 滚动条背景
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.50f, 0.50f, 0.50f, 0.80f);  // 滚动条抓取（灰色）
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.65f, 0.65f, 0.65f, 0.80f);  // 悬停
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);  // 激活

    // 复选框、单选按钮
    colors[ImGuiCol_CheckMark]              = ImVec4(1.00f, 0.30f, 0.60f, 1.00f);  // 勾选标记（高饱和粉色）

    // 滑块
    colors[ImGuiCol_SliderGrab]             = ImVec4(1.00f, 0.30f, 0.60f, 0.85f);  // 滑块抓取（粉色）
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(1.00f, 0.20f, 0.50f, 1.00f);  // 激活（更深粉色）

    // 按钮 - 这是重点！Model 3 风格的粉色按钮
    colors[ImGuiCol_Button]                 = ImVec4(0.25f, 0.25f, 0.28f, 0.80f);  // 普通按钮（深灰）
    colors[ImGuiCol_ButtonHovered]          = ImVec4(1.00f, 0.30f, 0.60f, 0.80f);  // 悬停（粉色）
    colors[ImGuiCol_ButtonActive]           = ImVec4(1.00f, 0.20f, 0.50f, 1.00f);  // 激活（深粉色）

    // 标题头（用于表格等）
    colors[ImGuiCol_Header]                 = ImVec4(0.25f, 0.25f, 0.28f, 0.60f);  // 标题头
    colors[ImGuiCol_HeaderHovered]          = ImVec4(1.00f, 0.30f, 0.60f, 0.70f);  // 悬停（粉色）
    colors[ImGuiCol_HeaderActive]           = ImVec4(1.00f, 0.20f, 0.50f, 1.00f);  // 激活（深粉色）

    // 分隔线
    colors[ImGuiCol_Separator]              = ImVec4(0.30f, 0.30f, 0.35f, 0.50f);  // 分隔线
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.50f, 0.50f, 0.55f, 0.70f);  // 悬停
    colors[ImGuiCol_SeparatorActive]        = ImVec4(1.00f, 0.30f, 0.60f, 1.00f);  // 激活（粉色）

    // 调整大小手柄
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.25f, 0.25f, 0.28f, 0.50f);  // 调整手柄
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.50f, 0.50f, 0.55f, 0.70f);  // 悬停
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(1.00f, 0.30f, 0.60f, 1.00f);  // 激活（粉色）

    // 标签
    colors[ImGuiCol_Tab]                    = ImVec4(0.18f, 0.18f, 0.20f, 0.70f);  // 标签页
    colors[ImGuiCol_TabHovered]             = ImVec4(1.00f, 0.30f, 0.60f, 0.70f);  // 悬停（粉色）
    colors[ImGuiCol_TabActive]              = ImVec4(1.00f, 0.30f, 0.60f, 1.00f);  // 激活（粉色）
    colors[ImGuiCol_TabUnfocused]           = ImVec4(0.15f, 0.15f, 0.15f, 0.70f);  // 未聚焦
    colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.25f, 0.25f, 0.28f, 0.80f);  // 未聚焦但激活

    // 绘图相关
    colors[ImGuiCol_PlotLines]              = ImVec4(1.00f, 0.30f, 0.60f, 1.00f);  // 线条图（粉色）
    colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.50f, 0.75f, 1.00f);  // 悬停（浅粉色）
    colors[ImGuiCol_PlotHistogram]          = ImVec4(1.00f, 0.30f, 0.60f, 1.00f);  // 直方图（粉色）
    colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.50f, 0.75f, 1.00f);  // 悬停（浅粉色）

    // 表格
    colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.18f, 0.18f, 0.20f, 1.00f);  // 表头
    colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.35f, 0.35f, 0.40f, 1.00f);  // 强边框
    colors[ImGuiCol_TableBorderLight]       = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);  // 弱边框
    colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);  // 行背景
    colors[ImGuiCol_TableRowBgAlt]          = ImVec4(0.15f, 0.15f, 0.18f, 0.50f);  // 交替行背景

    // 拖拽目标 - 用粉色高亮
    colors[ImGuiCol_DragDropTarget]         = ImVec4(1.00f, 0.30f, 0.60f, 0.90f);  // 拖放目标（粉色）

    // 导航相关
    colors[ImGuiCol_NavHighlight]           = ImVec4(1.00f, 0.30f, 0.60f, 1.00f);  // 导航高亮（粉色）
    colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 0.40f, 0.65f, 0.85f);  // 窗口化高亮
    colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.12f, 0.12f, 0.12f, 0.30f);  // 窗口化暗背景

    // 模态窗口暗背景
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.12f, 0.12f, 0.12f, 0.50f);  // 模态窗口背景
}
}