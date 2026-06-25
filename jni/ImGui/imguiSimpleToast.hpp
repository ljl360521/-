/**
 * @file SimpleToast.hpp
 * @brief 一个轻量级的 ImGui 通知库，使用前景层绘制
 * @version 2.1.0
 * @date 2025
 */

#ifndef SIMPLE_TOAST_HPP
#define SIMPLE_TOAST_HPP

#include "imgui.h"
#include <vector>
#include <string>
#include <chrono>

// ==================== 配置区域 ====================
#define TOAST_PADDING_X 50.0f           // 距离边缘的 X 间距
#define TOAST_PADDING_Y 50.0f           // 距离边缘的 Y 间距
#define TOAST_SPACING_Y 10.0f           // 通知之间的间距
#define TOAST_FADE_IN_TIME 200          // 淡入时间（毫秒）
#define TOAST_FADE_OUT_TIME 200         // 淡出时间（毫秒）
#define TOAST_DEFAULT_DURATION 3000     // 默认显示时间（毫秒）
#define TOAST_OPACITY 0.9f              // 不透明度（0-1）
#define TOAST_WIDTH 350.0f              // 通知宽度
#define TOAST_MIN_HEIGHT 60.0f          // 最小高度
#define TOAST_ROUNDING 8.0f             // 圆角半径
#define TOAST_BORDER_WIDTH 2.0f         // 边框宽度
#define TOAST_MAX_DISPLAY 0             // 最多同时显示数量（0=无限制）
#define TOAST_ICON_SIZE 20.0f           // 图标大小
#define TOAST_PADDING_INNER 20.0f       // 内部边距
// =================================================

enum class ToastType {
    Info,
    Success,
    Warning,
    Error
};

enum class ToastPosition {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
};

class Toast {
public:
    ToastType type;
    std::string title;
    std::string message;
    int duration;
    std::chrono::steady_clock::time_point createTime;
    
    Toast(ToastType t, const std::string& msg, const std::string& ttl = "", 
          int dur = TOAST_DEFAULT_DURATION)
        : type(t), message(msg), title(ttl), duration(dur), 
          createTime(std::chrono::steady_clock::now()) {}
    
    // 获取已经过的时间（毫秒）
    int GetElapsedTime() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - createTime).count();
    }
    
    // 获取透明度
    float GetAlpha() const {
        int elapsed = GetElapsedTime();
        int totalTime = duration + TOAST_FADE_OUT_TIME;
        
        // 淡入阶段
        if (elapsed < TOAST_FADE_IN_TIME) {
            return (float)elapsed / TOAST_FADE_IN_TIME * TOAST_OPACITY;
        }
        // 显示阶段
        else if (elapsed < duration) {
            return TOAST_OPACITY;
        }
        // 淡出阶段
        else if (elapsed < totalTime) {
            float fadeProgress = (float)(elapsed - duration) / TOAST_FADE_OUT_TIME;
            return (1.0f - fadeProgress) * TOAST_OPACITY;
        }
        
        return 0.0f;
    }
    
    // 是否已过期
    bool IsExpired() const {
        return GetElapsedTime() > (duration + TOAST_FADE_OUT_TIME);
    }
    
    // 获取颜色
    ImVec4 GetColor() const {
        switch (type) {
            case ToastType::Success: return ImVec4(0.2f, 0.8f, 0.2f, 1.0f);  // 绿色
            case ToastType::Warning: return ImVec4(1.0f, 0.8f, 0.0f, 1.0f);  // 黄色
            case ToastType::Error:   return ImVec4(0.9f, 0.2f, 0.2f, 1.0f);  // 红色
            case ToastType::Info:
            default:                 return ImVec4(0.2f, 0.6f, 1.0f, 1.0f);  // 蓝色
        }
    }
    
    // 获取图标Unicode字符
    const char* GetIcon() const {
        switch (type) {
            case ToastType::Success: return "✓";  // ✓
            case ToastType::Warning: return "!";  // !
            case ToastType::Error:   return "✕";  // ✕
            case ToastType::Info:
            default:                 return "i";  // i
        }
    }
    
    // 获取默认标题
    const char* GetDefaultTitle() const {
        if (!title.empty()) return title.c_str();
        
        switch (type) {
            case ToastType::Success: return "成功";
            case ToastType::Warning: return "警告";
            case ToastType::Error:   return "错误";
            case ToastType::Info:
            default:                 return "信息";
        }
    }
};

class ToastManager {
private:
    std::vector<Toast> toasts;
    ToastPosition position = ToastPosition::BottomRight;
    
    // 计算文本高度（支持自动换行）
    float CalculateTextHeight(const char* text, float wrapWidth) {
        if (!text || !text[0]) return 0.0f;
        
        ImFont* font = ImGui::GetFont();
        float fontSize = ImGui::GetFontSize();
        
        const char* textEnd = text + strlen(text);
        const char* lineStart = text;
        float totalHeight = 0.0f;
        
        while (lineStart < textEnd) {
            const char* lineEnd = lineStart;
            
            // 找到一行的结束位置
            while (lineEnd < textEnd && *lineEnd != '\n') {
                ImVec2 charSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, 
                                                      lineStart, lineEnd + 1);
                if (charSize.x > wrapWidth && lineEnd != lineStart) {
                    break;
                }
                lineEnd++;
            }
            
            totalHeight += fontSize;
            lineStart = lineEnd;
            if (*lineStart == '\n') lineStart++;
        }
        
        return totalHeight;
    }
    
    // 绘制文本（支持自动换行）
    void DrawTextWrapped(ImDrawList* drawList, ImVec2 pos, ImU32 color, 
                         const char* text, float wrapWidth) {
        if (!text || !text[0]) return;
        
        ImFont* font = ImGui::GetFont();
        float fontSize = ImGui::GetFontSize();
        
        const char* textEnd = text + strlen(text);
        const char* lineStart = text;
        float lineY = pos.y;
        
        while (lineStart < textEnd) {
            const char* lineEnd = lineStart;
            
            // 找到一行的结束位置
            while (lineEnd < textEnd && *lineEnd != '\n') {
                ImVec2 charSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, 
                                                      lineStart, lineEnd + 1);
                if (charSize.x > wrapWidth && lineEnd != lineStart) {
                    break;
                }
                lineEnd++;
            }
            
            // 绘制这一行
            drawList->AddText(ImVec2(pos.x, lineY), color, lineStart, lineEnd);
            
            lineY += fontSize;
            lineStart = lineEnd;
            if (*lineStart == '\n') lineStart++;
        }
    }
    
    // 绘制居中文本（支持自动换行）
    void DrawTextCentered(ImDrawList* drawList, ImVec2 pos, ImU32 color, 
                         const char* text, float wrapWidth) {
        if (!text || !text[0]) return;
        
        ImFont* font = ImGui::GetFont();
        float fontSize = ImGui::GetFontSize();
        
        const char* textEnd = text + strlen(text);
        const char* lineStart = text;
        float lineY = pos.y;
        
        while (lineStart < textEnd) {
            const char* lineEnd = lineStart;
            
            // 找到一行的结束位置
            while (lineEnd < textEnd && *lineEnd != '\n') {
                ImVec2 charSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, 
                                                      lineStart, lineEnd + 1);
                if (charSize.x > wrapWidth && lineEnd != lineStart) {
                    break;
                }
                lineEnd++;
            }
            
            // 计算这一行的宽度并居中
            ImVec2 lineSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, lineStart, lineEnd);
            float offsetX = (wrapWidth - lineSize.x) * 0.5f;
            
            // 绘制这一行（居中）
            drawList->AddText(ImVec2(pos.x + offsetX, lineY), color, lineStart, lineEnd);
            
            lineY += fontSize;
            lineStart = lineEnd;
            if (*lineStart == '\n') lineStart++;
        }
    }
    
public:
    static ToastManager& Instance() {
        static ToastManager instance;
        return instance;
    }
    
    // 设置通知位置
    void SetPosition(ToastPosition pos) {
        position = pos;
    }
    
    // 添加通知
    void Add(ToastType type, const std::string& message, 
             const std::string& title = "", int duration = TOAST_DEFAULT_DURATION) {
        toasts.emplace_back(type, message, title, duration);
    }
    
    // 快捷方法
    void Info(const std::string& message, const std::string& title = "") {
        Add(ToastType::Info, message, title);
    }
    
    void Success(const std::string& message, const std::string& title = "") {
        Add(ToastType::Success, message, title);
    }
    
    void Warning(const std::string& message, const std::string& title = "") {
        Add(ToastType::Warning, message, title);
    }
    
    void Error(const std::string& message, const std::string& title = "") {
        Add(ToastType::Error, message, title);
    }
    
    // 清除所有通知
    void Clear() {
        toasts.clear();
    }
    
    // 渲染所有通知（前景层绘制）
    void Render() {
        // 移除过期的通知
        toasts.erase(
            std::remove_if(toasts.begin(), toasts.end(), 
                [](const Toast& t) { return t.IsExpired(); }),
            toasts.end()
        );
        
        if (toasts.empty()) return;
        
        // 获取前景绘制列表
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 workPos = ImGui::GetMainViewport()->WorkPos;
        ImVec2 workSize = ImGui::GetMainViewport()->WorkSize;
        
        float yOffset = 0.0f;
        int displayCount = 0;
        
        for (size_t i = 0; i < toasts.size(); ++i) {
            // 限制显示数量
            #if TOAST_MAX_DISPLAY > 0
            if (displayCount >= TOAST_MAX_DISPLAY) break;
            #endif
            
            Toast& toast = toasts[i];
            float alpha = toast.GetAlpha();
            
            if (alpha <= 0.0f) continue;
            
            // 计算内容区域宽度（扣除图标、边距和间隔）
            float contentWidth = TOAST_WIDTH - TOAST_PADDING_INNER * 2 - TOAST_ICON_SIZE - 10;
            
            // 计算标题高度（支持换行）
            float titleHeight = CalculateTextHeight(toast.GetDefaultTitle(), contentWidth);
            
            // 计算消息高度（支持换行）
            float messageHeight = 0.0f;
            if (!toast.message.empty()) {
                messageHeight = CalculateTextHeight(toast.message.c_str(), contentWidth);
            }
            
            // 计算总高度（标题 + 消息 + 间距）
            float contentHeight = titleHeight;
            if (messageHeight > 0.0f) {
                contentHeight += 5.0f + messageHeight; // 5.0f 是标题和消息之间的间距
            }
            
            // 计算最终通知高度
            float toastHeight = TOAST_PADDING_INNER * 2 + contentHeight;
            if (toastHeight < TOAST_MIN_HEIGHT) {
                toastHeight = TOAST_MIN_HEIGHT;
            }
            
            // 计算通知位置
            ImVec2 toastPos;
            
            switch (position) {
                case ToastPosition::TopLeft:
                    toastPos = ImVec2(workPos.x + TOAST_PADDING_X, 
                                     workPos.y + TOAST_PADDING_Y + yOffset);
                    break;
                    
                case ToastPosition::TopRight:
                    toastPos = ImVec2(workPos.x + workSize.x - TOAST_PADDING_X - TOAST_WIDTH, 
                                     workPos.y + TOAST_PADDING_Y + yOffset);
                    break;
                    
                case ToastPosition::BottomLeft:
                    toastPos = ImVec2(workPos.x + TOAST_PADDING_X, 
                                     workPos.y + workSize.y - TOAST_PADDING_Y - yOffset - toastHeight);
                    break;
                    
                case ToastPosition::BottomRight:
                default:
                    toastPos = ImVec2(workPos.x + workSize.x - TOAST_PADDING_X - TOAST_WIDTH, 
                                     workPos.y + workSize.y - TOAST_PADDING_Y - yOffset - toastHeight);
                    break;
            }
            
            ImVec2 toastMin = toastPos;
            ImVec2 toastMax = ImVec2(toastPos.x + TOAST_WIDTH, toastPos.y + toastHeight);
            
            // 获取颜色
            ImVec4 typeColor = toast.GetColor();
            ImU32 bgColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.1f, 0.1f, 0.1f, alpha));
            ImU32 borderColor = ImGui::ColorConvertFloat4ToU32(
                ImVec4(typeColor.x, typeColor.y, typeColor.z, alpha));
            ImU32 iconColor = borderColor;
            ImU32 textColor = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, alpha));
            
            // 绘制背景和边框
            drawList->AddRectFilled(toastMin, toastMax, bgColor, TOAST_ROUNDING);
            drawList->AddRect(toastMin, toastMax, borderColor, TOAST_ROUNDING, 0, TOAST_BORDER_WIDTH);
            
            // 绘制图标
            ImVec2 iconPos = ImVec2(toastMin.x + TOAST_PADDING_INNER, 
                                   toastMin.y + TOAST_PADDING_INNER);
            
            // 绘制图标圆形背景
            ImVec2 iconCenter = ImVec2(iconPos.x + TOAST_ICON_SIZE * 0.5f, 
                                       iconPos.y + TOAST_ICON_SIZE * 0.5f);
            drawList->AddCircleFilled(iconCenter, TOAST_ICON_SIZE * 0.5f, borderColor);
            
            // 绘制图标文字
            const char* icon = toast.GetIcon();
            ImVec2 iconTextSize = ImGui::CalcTextSize(icon);
            ImVec2 iconTextPos = ImVec2(iconCenter.x - iconTextSize.x * 0.5f, 
                                        iconCenter.y - iconTextSize.y * 0.5f);
            drawList->AddText(iconTextPos, 
                            ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, alpha)), 
                            icon);
            
            // 绘制标题（居中，支持换行）
            ImVec2 titlePos = ImVec2(iconPos.x + TOAST_ICON_SIZE + 10, 
                                    toastMin.y + TOAST_PADDING_INNER);
            DrawTextCentered(drawList, titlePos, textColor, toast.GetDefaultTitle(), contentWidth);
            
            // 绘制消息内容（左对齐，支持换行）
            if (!toast.message.empty()) {
                ImVec2 messagePos = ImVec2(titlePos.x, titlePos.y + titleHeight + 5);
                DrawTextWrapped(drawList, messagePos, textColor, 
                              toast.message.c_str(), contentWidth);
            }
            
            // 更新偏移量
            yOffset += toastHeight + TOAST_SPACING_Y;
            displayCount++;
        }
    }
};

// 全局便捷函数
namespace SimpleToast {
    inline void SetPosition(ToastPosition pos) {
        ToastManager::Instance().SetPosition(pos);
    }
    
    inline void Info(const std::string& message, const std::string& title = "") {
        ToastManager::Instance().Info(message, title);
    }
    
    inline void Success(const std::string& message, const std::string& title = "") {
        ToastManager::Instance().Success(message, title);
    }
    
    inline void Warning(const std::string& message, const std::string& title = "") {
        ToastManager::Instance().Warning(message, title);
    }
    
    inline void Error(const std::string& message, const std::string& title = "") {
        ToastManager::Instance().Error(message, title);
    }
    
    inline void Clear() {
        ToastManager::Instance().Clear();
    }
    
    inline void Render() {
        ToastManager::Instance().Render();
    }
}

#endif // SIMPLE_TOAST_HPP