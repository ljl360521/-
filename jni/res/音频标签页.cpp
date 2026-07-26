#include "音频标签页.h"
#include "全局状态.h"
#include "res/music/MusicPlayer.h"
#include "imgui.h"
#include <vector>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <atomic>

// ==================== 上传包 UI：仅嵌入“音频播放”标签页内部 ====================
struct AudioTabSnowFlake { float x, y, speed, size, baseSize, angle; int colorR, colorG, colorB; };
std::vector<AudioTabSnowFlake> g_audio_tab_snow_flakes;
bool g_audio_tab_snow_initialized = false;

void AudioTabGetRandomColor(int& r, int& g, int& b) {
    int colorType = rand() % 7;
    switch (colorType) {
    case 0: r = 255; g = 80 + rand() % 100; b = 80 + rand() % 100; break;
    case 1: r = 80 + rand() % 100; g = 255; b = 80 + rand() % 100; break;
    case 2: r = 80 + rand() % 100; g = 80 + rand() % 100; b = 255; break;
    case 3: r = 255; g = 200 + rand() % 55; b = 80 + rand() % 100; break;
    case 4: r = 255; g = 80 + rand() % 100; b = 255; break;
    case 5: r = 80 + rand() % 100; g = 255; b = 255; break;
    default: r = 220 + rand() % 35; g = 220 + rand() % 35; b = 220 + rand() % 35; break;
    }
}

void AudioTabInitSnow(int count, int width, int height) {
    if (width <= 0) width = 900; if (height <= 0) height = 560;
    g_audio_tab_snow_flakes.resize(count);
    for (auto& flake : g_audio_tab_snow_flakes) {
        flake.x = (float)(rand() % width); flake.y = (float)(rand() % height);
        flake.speed = (float)(40 + rand() % 120); flake.baseSize = (float)(8 + rand() % 15);
        flake.size = flake.baseSize; flake.angle = (rand() % 360) * 3.14159f / 180.0f;
        AudioTabGetRandomColor(flake.colorR, flake.colorG, flake.colorB);
    }
    g_audio_tab_snow_initialized = true;
}

void AudioTabDrawSnow(ImDrawList* drawList, int width, int height, const ImVec2& pos) {
    if (!drawList || width <= 0 || height <= 0) return;
    if (!g_audio_tab_snow_initialized || (int)g_audio_tab_snow_flakes.size() != 250) {
        srand((unsigned int)time(nullptr)); AudioTabInitSnow(250, width, height); return;
    }
    float deltaTime = ImGui::GetIO().DeltaTime; if (deltaTime > 0.033f) deltaTime = 0.033f;
    static float smoothBeat = 0.0f, lastBeat = 0.0f;
    float rawBeat = g_BeatIntensity.load(std::memory_order_relaxed);
    smoothBeat = smoothBeat * 0.7f + rawBeat * 0.3f;
    if (rawBeat > lastBeat) smoothBeat = rawBeat; else smoothBeat = smoothBeat * 0.92f;
    lastBeat = rawBeat; if (smoothBeat < 0.0f) smoothBeat = 0.0f; if (smoothBeat > 1.0f) smoothBeat = 1.0f;
    float basePulseScale = 1.0f + smoothBeat * 0.9f;
    static float individualFactor[250] = {0}, snowState[250] = {0}; static bool factorInit = false;
    if (!factorInit) { for (int i = 0; i < 250; i++) individualFactor[i] = 0.5f + (rand() % 100) / 100.0f; factorInit = true; }
    for (size_t i = 0; i < g_audio_tab_snow_flakes.size(); ++i) {
        auto& flake = g_audio_tab_snow_flakes[i];
        flake.y += flake.speed * deltaTime; flake.x += sinf(flake.angle) * 30.0f * deltaTime; flake.angle += 1.5f * deltaTime;
        if (flake.y > height) { flake.y = 0.0f; flake.x = (float)(rand() % width); flake.speed = (float)(40 + rand() % 120); flake.baseSize = (float)(8 + rand() % 15); AudioTabGetRandomColor(flake.colorR, flake.colorG, flake.colorB); }
        if (flake.x < -20.0f) flake.x = (float)width + 20.0f; if (flake.x > (float)width + 20.0f) flake.x = -20.0f;
        int idx = (int)i; float personality = individualFactor[idx]; float depthFactor = 0.6f + (flake.y / (float)height) * 0.8f;
        float targetScale = 1.0f + (basePulseScale - 1.0f) * personality * depthFactor;
        float responseSpeed = 0.3f + personality * 0.5f; float decaySpeed = 0.85f + personality * 0.1f;
        snowState[idx] = snowState[idx] * (1.0f - responseSpeed) + targetScale * responseSpeed;
        if (targetScale < snowState[idx]) snowState[idx] = snowState[idx] * decaySpeed + targetScale * (1.0f - decaySpeed);
        float finalScale = snowState[idx]; if (finalScale < 0.8f) finalScale = 0.8f; if (finalScale > 2.2f) finalScale = 2.2f;
        float alpha = 120.0f + (flake.speed - 40.0f) / 120.0f * 100.0f; if (alpha > 220.0f) alpha = 220.0f; if (alpha < 100.0f) alpha = 100.0f;
        drawList->AddCircleFilled(ImVec2(pos.x + flake.x, pos.y + flake.y), flake.baseSize * finalScale * 0.8f, IM_COL32(flake.colorR, flake.colorG, flake.colorB, (int)alpha));
    }
}

void DrawUploadedUIInsideAudioTab() {
    static int page = 1; // 默认显示音乐页；左侧仅保留“音乐/设置”
    ImGui::BeginChild("上传音乐UI_外框", ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetWindowPos(); ImVec2 size = ImGui::GetWindowSize();
    drawList->PushClipRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), true);
    AudioTabDrawSnow(drawList, (int)size.x, (int)size.y, pos);
    drawList->PopClipRect();
    ImGui::BeginChild("左面板", ImVec2(110, 0), false);
    if (ImGui::Button("音乐", ImVec2(110, 80))) page = 1;
    if (ImGui::Button("设置", ImVec2(110, 80))) page = 2;
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("右面板", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
    if (page == 2) {
        static int playMode = 0;
        ImGui::SeparatorText("音乐列表播放设置");
        ImGui::RadioButton("顺序播放", &playMode, 0); ImGui::SameLine(); ImGui::RadioButton("随机播放", &playMode, 1); ImGui::SameLine(); ImGui::RadioButton("循环单曲", &playMode, 2); ImGui::SameLine(); ImGui::RadioButton("循环列表", &playMode, 3);
        switch (playMode) { case 0: g_Playlist.shuffle = false; g_Playlist.repeat = false; break; case 1: g_Playlist.shuffle = true; g_Playlist.repeat = false; break; case 2: g_Playlist.shuffle = false; g_Playlist.repeat = true; break; case 3: g_Playlist.shuffle = true; g_Playlist.repeat = true; break; }
    } else {
        音乐窗口();
    }
    ImGui::EndChild();
    ImGui::EndChild();
}
