#define MINIAUDIO_IMPLEMENTATION
#define CACERT_DATA_IMPLEMENTATION
#include "miniaudio/miniaudio.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"
#include "MusicPlayer.h"
#include <curl/curl.h>
#include <curl/证书.h>
#include <nlohmann/json.hpp>
#include <vector>
#include <string>
#include <thread>
#include <cmath>
#include <complex>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <cstring>
#include <cstdio>
#include <atomic>

using json = nlohmann::json;

// ==================== 全局变量 ====================
// 数据
std::vector<Song> g_SongList;
Playlist g_Playlist;
std::string g_StatusMsg, g_ErrorMsg;
static std::mutex g_StatusMutex;
static std::mutex g_SongListMutex;
static std::mutex g_AudioVisualMutex; // 保护 g_AudioBuf/g_Waveform/g_Spectrum 的跨线程访问
Song g_CurrentSong;
std::string g_CurrentSongId;

// 音频引擎
static ma_decoder g_AudioDecoder;
static ma_device g_AudioDevice;
static bool g_DecoderReady = false, g_DeviceReady = false;

// 播放控制标志
// 注意：这些变量会被 UI 线程、播放线程和音频回调线程同时访问。
// volatile 不能提供跨线程同步语义，改为 atomic 避免数据竞争。
static std::atomic<bool> g_ThreadRun{true};
static std::atomic<bool> g_IsPlaying{false};
static std::atomic<bool> g_DeviceRunning{false};
static std::atomic<int>  g_PendingIndex{-1};
static std::atomic<bool> g_RequestNext{false}, g_RequestPrev{false};
static std::atomic<bool> g_RequestPause{false}, g_RequestResume{false};
static std::atomic<bool> g_AutoNext{false}, g_PlayNextFromNew{false};

// 播放状态数据
static std::vector<uint8_t> g_AudioData;
static std::chrono::steady_clock::time_point g_StartTime;
static float g_PausedPos = 0.0f;
static ma_uint64 g_PausedFrame = 0;

// 频谱
static const int FFT_SIZE = 8192, BANDS = 128;
static std::vector<float> g_Spectrum(BANDS, 0.0f);
static std::vector<float> g_Waveform(256, 0.0f);
static std::vector<float> g_AudioBuf, g_Window;
static float g_SpectrumGain = 2.0f;
static std::atomic<bool> g_VisualRun{false};

// 节奏检测
std::atomic<float> g_BeatIntensity{0.0f};  // 音乐节奏强度值，范围 0.0 ~ 1.0

// 线程
static std::thread g_PlayThread, g_VisualThread, g_SearchThread;
static std::atomic<bool> g_Searching(false);
static bool g_CurlInit = false;
static std::once_flag g_CurlInitOnce;
static bool g_MusicPlayerInit = false;

// ==================== 工具函数 ====================
void SetStatus(const std::string& msg, bool isError = false) {
    std::lock_guard<std::mutex> lock(g_StatusMutex);
    if (isError) { g_ErrorMsg = msg; g_StatusMsg.clear(); }
    else { g_StatusMsg = msg; g_ErrorMsg.clear(); }
}

void GetStatus(std::string& out, bool& isError) {
    std::lock_guard<std::mutex> lock(g_StatusMutex);
    isError = !g_ErrorMsg.empty();
    out = isError ? g_ErrorMsg : g_StatusMsg;
}

std::string FormatTime(float sec) {
    if (sec < 0) sec = 0;
    int m = (int)sec / 60, s = (int)sec % 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return buf;
}

// ==================== 音频工具 ====================
void InitWindow() {
    g_Window.resize(FFT_SIZE);
    for (int i = 0; i < FFT_SIZE; i++)
        g_Window[i] = 0.5f * (1.0f - cosf(2.0f * 3.14159265f * i / (FFT_SIZE - 1)));
}

void ComputeFFT(const std::vector<float>& in, std::vector<float>& out) {
    int n = in.size(), m = 0;
    while ((1 << m) < n) m++;
    int N = 1 << m;
    
    std::vector<std::complex<float>> data(N, 0.0f);
    for (int i = 0; i < n && i < FFT_SIZE; i++)
        data[i] = std::complex<float>(in[i] * g_Window[i], 0.0f);
    
    // 位反转
    for (int i = 0; i < N; i++) {
        int j = 0;
        for (int k = 0; k < m; k++) j = (j << 1) | ((i >> k) & 1);
        if (j > i) std::swap(data[i], data[j]);
    }
    
    // FFT
    for (int s = 1; s <= m; s++) {
        int len = 1 << s;
        float theta = -2.0f * 3.14159265f / len;
        std::complex<float> wm(cosf(theta), sinf(theta));
        for (int k = 0; k < N; k += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int j = 0; j < len/2; j++) {
                std::complex<float> t = w * data[k + j + len/2];
                std::complex<float> u = data[k + j];
                data[k + j] = u + t;
                data[k + j + len/2] = u - t;
                w *= wm;
            }
        }
    }
    
    // 幅度
    std::vector<float> mag(N/2);
    float maxMag = 0.0f;
    for (int i = 0; i < N/2; i++) {
        mag[i] = sqrtf(data[i].real()*data[i].real() + data[i].imag()*data[i].imag());
        if (mag[i] > maxMag) maxMag = mag[i];
    }
    if (maxMag > 0.0f) for (auto& v : mag) v /= maxMag;
    
    // 对数频带
    out.assign(BANDS, 0.0f);
    float logEnd = logf(N/2.0f);
    float bw = logEnd / BANDS;
    for (int b = 0; b < BANDS; b++) {
        int s = (int)expf(b * bw), e = (int)expf((b + 1) * bw);
        if (s < 0) s = 0;
        if (e > N/2) e = N/2;
        if (s >= e) continue;
        float sum = 0.0f;
        for (int i = s; i < e; i++) sum += mag[i];
        out[b] = sum / (e - s);
    }
    for (int i = 0; i < BANDS; i++) {
        float p = i > 0 ? out[i-1] : 0.0f;
        float n = i < BANDS-1 ? out[i+1] : 0.0f;
        out[i] = (p + out[i] + n) / 3.0f;
    }
}

void AudioCallback(ma_device*, void* out, const void*, ma_uint32 n) {
    if (!g_DeviceRunning || !g_IsPlaying || !g_DecoderReady || n <= 4) {
        memset(out, 0, n * 2 * sizeof(float));
        return;
    }
    
    // 实时音频回调里避免每帧 new/delete：使用 thread_local 缓冲区复用内存。
    static thread_local std::vector<float> samplesBuffer;
    static thread_local std::vector<float> mono;
    samplesBuffer.resize((size_t)n * 2);
    float* samples = samplesBuffer.data();
    
    ma_uint64 framesRead64 = 0;
    ma_result decResult = ma_decoder_read_pcm_frames(&g_AudioDecoder, samples, n, &framesRead64);
    ma_uint32 framesRead = framesRead64 > n ? n : (ma_uint32)framesRead64;
    if (framesRead > 4) {
        memset(out, 0, n * 2 * sizeof(float));
        memcpy(out, samples, framesRead * 2 * sizeof(float));
        if (framesRead < n) g_AutoNext = true;
        
        mono.resize((size_t)framesRead);
        for (ma_uint32 i = 0; i < framesRead; i++)
            mono[(size_t)i] = (samples[i*2] + samples[i*2+1]) * 0.5f;
        
        {
            // 音频回调线程不能阻塞等待锁；可视化数据本帧拿不到锁就跳过，优先保证播放不卡顿。
            std::unique_lock<std::mutex> lock(g_AudioVisualMutex, std::try_to_lock);
            if (lock.owns_lock()) {
                int wp = (int)g_Waveform.size();
                for (int i = 0; i < wp; i++) {
                    int idx = i * (int)framesRead / wp;
                    if (idx < (int)framesRead)
                        g_Waveform[i] = g_Waveform[i] * 0.6f + fabsf(mono[(size_t)idx]) * 0.4f;
                }
            }
        }
        
        // ========== 多频段鼓点检测 + 人声辅助 ==========
        static float bandHistory[3][43] = {{0}};  // 3个频段，各约1秒历史
        static int historyIdx = 0;
        static float beatIntensityCache = 0.0f;
        
        // 简单的低通和高通滤波（移动平均差分）
        float lowBand = 0.0f;    // 低频：底鼓
        float midBand = 0.0f;    // 中频：军鼓 + 人声
        float highBand = 0.0f;   // 高频：镲片
        
        int window = 4;  // 滑动窗口大小
        
        for (ma_uint32 i = window; i < framesRead; i++) {
            // 低频：长窗口平均（检测底鼓的持续能量）
            float lowSum = 0;
            for (int j = 0; j < window; j++) {
                lowSum += fabsf(mono[i - j]);
            }
            lowSum /= window;
            
            // 中频：短窗口变化率（检测军鼓的攻击性 + 人声）
            float midDiff = fabsf(mono[i] - mono[i - window/2]);
            
            // 高频：极短窗口变化（检测镲片的瞬态）
            float highDiff = fabsf(mono[i] - mono[i-1]);
            
            lowBand += lowSum;
            midBand += midDiff;
            highBand += highDiff;
        }
        
        lowBand = lowBand / (framesRead - window);
        midBand = midBand / (framesRead - window);
        highBand = highBand / (framesRead - window);
        
        // 归一化各频段（让它们在同一量级）
        static float lowNorm = 1.0f, midNorm = 1.0f, highNorm = 1.0f;
        lowNorm = lowNorm * 0.99f + lowBand * 0.01f;
        midNorm = midNorm * 0.99f + midBand * 0.01f;
        highNorm = highNorm * 0.99f + highBand * 0.01f;
        
        float lowNormed = lowBand / (lowNorm + 0.01f);
        float midNormed = midBand / (midNorm + 0.01f);
        float highNormed = highBand / (highNorm + 0.01f);
        
        // 检测人声：中频持续能量（人声比军鼓更持续，攻击性较弱）
        static float lastMidSmooth = 0.0f;
        float prevMidSmooth = lastMidSmooth;
        float midSmooth = midNormed * 0.3f + prevMidSmooth * 0.7f;  // 平滑中频
        
        // 人声特征：中频能量高 且 变化率小（持续稳定）
        float voiceDetect = 0.0f;
        float midDelta = fabsf(midSmooth - prevMidSmooth);
        lastMidSmooth = midSmooth;
        if (midNormed > 0.6f && midDelta < 0.1f) {
            voiceDetect = (midNormed - 0.5f) * 0.3f;  // 人声幅度最大0.15
            if (voiceDetect > 0.15f) voiceDetect = 0.15f;
            if (voiceDetect < 0) voiceDetect = 0;
        }
        
        // 综合鼓点能量（加权求和）
        float drumEnergy = lowNormed * 0.5f + midNormed * 0.3f + highNormed * 0.2f;
        
        // 能量变化率
        static float lastDrumEnergy = 0;
        float energyDelta = drumEnergy - lastDrumEnergy;
        lastDrumEnergy = drumEnergy;
        
        // 更新历史
        bandHistory[0][historyIdx] = lowNormed;
        bandHistory[1][historyIdx] = midNormed;
        bandHistory[2][historyIdx] = highNormed;
        
        // 计算动态阈值
        float maxHistory = 0;
        for (int i = 0; i < 43; i++) {
            maxHistory = std::max(maxHistory, bandHistory[0][i]);
            maxHistory = std::max(maxHistory, bandHistory[1][i]);
            maxHistory = std::max(maxHistory, bandHistory[2][i]);
        }
        
        float dynamicThreshold = maxHistory * 0.4f;
        
        static float timeSinceLastBeat = 0;
        timeSinceLastBeat += (float)framesRead / 44100.0f;
        
        // 检测节拍
        bool isBeat = false;
        float currentIntensity = 0;
        
        if (timeSinceLastBeat > 0.1f) {
            if (lowNormed > dynamicThreshold && energyDelta > -0.1f) {
                isBeat = true;
                currentIntensity = lowNormed * 0.8f;
            }
            else if (midNormed > dynamicThreshold * 0.9f && energyDelta > -0.05f) {
                isBeat = true;
                currentIntensity = midNormed * 0.7f;
            }
            else if (highNormed > dynamicThreshold * 1.2f && energyDelta > 0) {
                isBeat = true;
                currentIntensity = highNormed * 0.5f;
            }
        }
        
        // 最终强度 = 鼓点强度 + 人声强度（人声幅度小于鼓点）
        float finalIntensity = 0;
        
        if (isBeat) {
            timeSinceLastBeat = 0;
            finalIntensity = std::min(1.0f, currentIntensity * 1.2f);
        } else {
            // 无鼓点时，加入微量人声响应（最大0.2）
            finalIntensity = voiceDetect * 0.6f;
            // 衰减
            beatIntensityCache = beatIntensityCache * 0.92f;
            if (beatIntensityCache < 0.01f) beatIntensityCache = 0;
        }
        
        // 如果鼓点强度大于人声强度，用鼓点；否则用混合
        if (isBeat) {
            beatIntensityCache = finalIntensity;
        } else if (voiceDetect > beatIntensityCache) {
            beatIntensityCache = std::max(beatIntensityCache, finalIntensity);
        } else {
            beatIntensityCache = beatIntensityCache * 0.96f;
        }
        
        if (beatIntensityCache > 1.0f) beatIntensityCache = 1.0f;
        if (beatIntensityCache < 0) beatIntensityCache = 0;
        
        historyIdx = (historyIdx + 1) % 43;
        
        g_BeatIntensity.store(beatIntensityCache, std::memory_order_relaxed);
        // ========== 检测结束 ==========
        
        {
            std::unique_lock<std::mutex> lock(g_AudioVisualMutex, std::try_to_lock);
            if (lock.owns_lock()) {
                g_AudioBuf.insert(g_AudioBuf.end(), mono.begin(), mono.end());
                if (g_AudioBuf.size() > FFT_SIZE * 4)
                    g_AudioBuf.erase(g_AudioBuf.begin(), g_AudioBuf.end() - FFT_SIZE * 2);
            }
        }
        
        ma_uint64 cur = 0, len = 0;
        ma_decoder_get_cursor_in_pcm_frames(&g_AudioDecoder, &cur);
        ma_decoder_get_length_in_pcm_frames(&g_AudioDecoder, &len);
        if (len > 0) {
            g_Playlist.progress = (float)cur / len;
            g_Playlist.duration = (float)len / 44100.0f;
            if (g_Playlist.progress >= 0.999f) g_AutoNext = true;
        }
    } else {
        if (decResult != MA_SUCCESS) g_AutoNext = true;
        memset(out, 0, n * 2 * sizeof(float));
    }
}

bool InitDevice() {
    if (g_DeviceReady) { ma_device_uninit(&g_AudioDevice); g_DeviceReady = false; }
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = 44100;
    cfg.dataCallback = AudioCallback;
    if (ma_device_init(NULL, &cfg, &g_AudioDevice) != MA_SUCCESS) return false;
    g_DeviceReady = true;
    return true;
}

// ==================== 网络请求 ====================
void InitCurl() {
    std::call_once(g_CurlInitOnce, [](){
        curl_global_init(CURL_GLOBAL_DEFAULT);
        g_CurlInit = true;
    });
}

void ApplyCurlCommonOptions(CURL* c, long timeoutSeconds) {
    if (!c) return;
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, timeoutSeconds);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "Mozilla/5.0");
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    static struct curl_blob caBlob = { (void*)cacert_data, (size_t)cacert_data_len, CURL_BLOB_NOCOPY };
    curl_easy_setopt(c, CURLOPT_CAINFO_BLOB, &caBlob);
}

size_t WriteStr(void* c, size_t s, size_t n, std::string* out) {
    out->append((char*)c, s * n);
    return s * n;
}

size_t WriteVec(void* c, size_t s, size_t n, std::vector<uint8_t>* out) {
    size_t old = out->size();
    out->resize(old + s * n);
    memcpy(out->data() + old, c, s * n);
    return s * n;
}

std::string HttpGet(const std::string& url) {
    InitCurl();
    std::string res;
    CURL* c = curl_easy_init();
    if (!c) return "";
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, WriteStr);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &res);
    ApplyCurlCommonOptions(c, 10L);
    CURLcode r = curl_easy_perform(c);
    if (r != CURLE_OK) { curl_easy_cleanup(c); return ""; }
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(c);
    return code == 200 ? res : "";
}

std::vector<uint8_t> Download(const std::string& url) {
    InitCurl();
    std::vector<uint8_t> data;
    CURL* c = curl_easy_init();
    if (!c) return data;
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, WriteVec);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &data);
    ApplyCurlCommonOptions(c, 30L);
    CURLcode r = curl_easy_perform(c);
    if (r != CURLE_OK) data.clear();
    curl_easy_cleanup(c);
    return data;
}

std::string UrlEncode(const std::string& v) {
    InitCurl();
    CURL* c = curl_easy_init();
    if (!c) return v;
    char* e = curl_easy_escape(c, v.c_str(), v.length());
    std::string r = e ? e : v;
    curl_free(e);
    curl_easy_cleanup(c);
    return r;
}

// ==================== API 接口 ====================
void DoSearch(const std::string& kw) {
    InitCurl();
    SetStatus("搜索: " + kw);
    std::string url = "https://oiapi.net/API/Music_163?name=" + UrlEncode(kw) + "&limit=50";
    std::string res = HttpGet(url);
    if (res.empty()) { SetStatus("搜索失败", true); return; }
    
    json data = json::parse(res, nullptr, false);
    if (data.is_discarded() || (data.contains("code") && data["code"] != 0)) {
        SetStatus("API错误", true); return;
    }
    if (!data.contains("data") || !data["data"].is_array()) {
        SetStatus("无数据", true); return;
    }
    
    std::vector<Song> songs;
    for (auto& item : data["data"]) {
        Song s;
        s.name = item.value("name", "未知");
        if (item["id"].is_number()) s.id = std::to_string(item["id"].get<int64_t>());
        else s.id = item.value("id", "");
        s.picurl = item.value("picurl", "");
        s.jumpurl = item.value("jumpurl", "");
        if (item.contains("singers") && item["singers"].is_array())
            for (auto& singer : item["singers"]) {
                if (!s.singers.empty()) s.singers += ", ";
                s.singers += singer.value("name", "");
            }
        songs.push_back(s);
    }
    
    size_t foundCount = songs.size();
    {
        std::lock_guard<std::mutex> lock(g_SongListMutex);
        g_SongList = std::move(songs);
        g_Playlist.songs = g_SongList;
        if (g_IsPlaying && !g_CurrentSongId.empty()) {
            g_PlayNextFromNew = true;
            g_Playlist.currentIndex = -1;
        } else {
            g_Playlist.currentIndex = -1;
            g_Playlist.state = PlayState::Idle;
            g_CurrentSongId.clear();
        }
    }
    
    if (g_IsPlaying && !g_CurrentSongId.empty()) {
        SetStatus("找到 " + std::to_string(foundCount) + " 首，当前播完切新列表");
    } else {
        SetStatus("找到 " + std::to_string(foundCount) + " 首");
    }
}

void StartSearchAsync(const std::string& kw) {
    if (kw.empty()) return;
    if (g_Searching.exchange(true)) {
        SetStatus("正在搜索，请稍候...");
        return;
    }
    if (g_SearchThread.joinable()) g_SearchThread.join();
    SetStatus("搜索中: " + kw);
    g_SearchThread = std::thread([kw]() {
        DoSearch(kw);
        g_Searching = false;
    });
}

std::string GetPlayUrl(const std::string& id) {
    std::string res = HttpGet("https://oiapi.net/API/Music_163?id=" + id);
    if (res.empty()) return "";
    json data = json::parse(res, nullptr, false);
    if (data.is_discarded() || (data.contains("code") && data["code"] != 0)) return "";
    if (data.contains("data") && data["data"].is_array() && !data["data"].empty())
        if (data["data"][0].contains("url") && !data["data"][0]["url"].is_null())
            return data["data"][0]["url"];
    return "";
}

// ==================== 播放控制 ====================
void SafeStop() {
    g_IsPlaying = false;
    g_DeviceRunning = false;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (g_DeviceReady) { ma_device_stop(&g_AudioDevice); ma_device_uninit(&g_AudioDevice); g_DeviceReady = false; }
    if (g_DecoderReady) { ma_decoder_uninit(&g_AudioDecoder); g_DecoderReady = false; }
    g_AudioData.clear();
    g_PausedFrame = 0;
    g_PausedPos = 0.0f;
}

int GetNextIdx() {
    if (g_Playlist.songs.empty()) return -1;
    if (g_PlayNextFromNew) { g_PlayNextFromNew = false; return 0; }
    if (g_Playlist.currentIndex < 0) return 0;
    
    int n = g_Playlist.shuffle ? rand() % g_Playlist.songs.size() : g_Playlist.currentIndex + 1;
    if (n >= (int)g_Playlist.songs.size())
        return g_Playlist.repeat ? 0 : -1;
    return n;
}

int GetPrevIdx() {
    if (g_Playlist.songs.empty()) return -1;
    if (g_Playlist.currentIndex < 0) return g_Playlist.songs.size() - 1;
    
    int p = g_Playlist.shuffle ? rand() % g_Playlist.songs.size() : g_Playlist.currentIndex - 1;
    if (p < 0) return g_Playlist.repeat ? g_Playlist.songs.size() - 1 : -1;
    return p;
}

bool PlaySong(int idx) {
    if (idx < 0 || idx >= (int)g_Playlist.songs.size()) return false;
    
    SafeStop();
    Song& s = g_Playlist.songs[idx];
    g_Playlist.state = PlayState::Loading;
    g_Playlist.currentIndex = idx;
    g_Playlist.progress = 0.0f;
    g_CurrentSongId = s.id;
    g_CurrentSong = s;
    SetStatus("加载: " + s.name);
    
    std::string url = GetPlayUrl(s.id);
    if (url.empty()) { g_Playlist.state = PlayState::Idle; SetStatus("获取URL失败", true); return false; }
    
    g_AudioData = Download(url);
    if (g_AudioData.empty()) { g_Playlist.state = PlayState::Idle; SetStatus("下载失败", true); return false; }
    
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, 44100);
    if (ma_decoder_init_memory(g_AudioData.data(), g_AudioData.size(), &cfg, &g_AudioDecoder) != MA_SUCCESS) {
        g_Playlist.state = PlayState::Idle; SetStatus("解码失败", true); return false;
    }
    g_DecoderReady = true;
    
    if (!InitDevice()) { ma_decoder_uninit(&g_AudioDecoder); g_DecoderReady = false; g_Playlist.state = PlayState::Idle; return false; }
    
    g_AudioBuf.clear();
    if (ma_device_start(&g_AudioDevice) != MA_SUCCESS) {
        ma_device_uninit(&g_AudioDevice); g_DeviceReady = false;
        ma_decoder_uninit(&g_AudioDecoder); g_DecoderReady = false;
        g_Playlist.state = PlayState::Idle; return false;
    }
    
    g_DeviceRunning = g_IsPlaying = true;
    g_StartTime = std::chrono::steady_clock::now();
    g_Playlist.state = PlayState::Playing;
    SetStatus("播放: " + s.name);
    return true;
}

void PlayThread() {
    InitCurl();
    while (g_ThreadRun) {
        // 等待事件
        while (g_PendingIndex < 0 && !g_RequestNext && !g_RequestPrev && 
               !g_RequestPause && !g_RequestResume && !g_AutoNext && g_ThreadRun)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (!g_ThreadRun) break;
        
        // 暂停
        if (g_RequestPause) {
            g_RequestPause = false;
            if (g_IsPlaying && g_DecoderReady) {
                ma_decoder_get_cursor_in_pcm_frames(&g_AudioDecoder, &g_PausedFrame);
                auto now = std::chrono::steady_clock::now();
                g_PausedPos += std::chrono::duration<float>(now - g_StartTime).count();
                g_IsPlaying = false;
                g_Playlist.state = PlayState::Paused;
                SetStatus("暂停");
            }
            continue;
        }
        
        // 继续
        if (g_RequestResume) {
            g_RequestResume = false;
            if (g_Playlist.state == PlayState::Paused && g_DecoderReady) {
                ma_decoder_seek_to_pcm_frame(&g_AudioDecoder, g_PausedFrame);
                g_IsPlaying = true;
                g_StartTime = std::chrono::steady_clock::now();
                g_Playlist.state = PlayState::Playing;
                SetStatus("播放");
            }
            continue;
        }
        
        // 确定播放索引
        int idx = -1;
        if (g_AutoNext)      { g_AutoNext = false; idx = GetNextIdx(); }
        else if (g_RequestNext) { g_RequestNext = false; g_PlayNextFromNew = false; idx = GetNextIdx(); if (idx < 0 && !g_Playlist.songs.empty()) idx = 0; }
        else if (g_RequestPrev) { g_RequestPrev = false; g_PlayNextFromNew = false; idx = GetPrevIdx(); if (idx < 0 && !g_Playlist.songs.empty()) idx = 0; }
        else                 { idx = g_PendingIndex; g_PendingIndex = -1; g_PlayNextFromNew = false; }
        
        if (idx < 0) {
            SafeStop();
            g_Playlist.state = PlayState::Idle;
            g_CurrentSongId.clear();
            g_CurrentSong = Song();
            SetStatus("列表结束");
            continue;
        }
        
        if (!PlaySong(idx)) {
            if (!g_Playlist.songs.empty()) g_AutoNext = true;
            continue;
        }
        
        // 播放循环
        while (g_IsPlaying && g_DeviceRunning && g_ThreadRun) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            if (g_RequestPause) {
                g_RequestPause = false;
                if (g_DecoderReady) {
                    ma_decoder_get_cursor_in_pcm_frames(&g_AudioDecoder, &g_PausedFrame);
                    auto now = std::chrono::steady_clock::now();
                    g_PausedPos += std::chrono::duration<float>(now - g_StartTime).count();
                    g_IsPlaying = false;
                    g_Playlist.state = PlayState::Paused;
                }
                while (g_Playlist.state == PlayState::Paused && g_ThreadRun) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    if (g_RequestResume) { g_RequestResume = false; g_IsPlaying = true; g_StartTime = std::chrono::steady_clock::now(); g_Playlist.state = PlayState::Playing; break; }
                    if (g_RequestNext || g_RequestPrev || g_PendingIndex >= 0 || g_AutoNext) break;
                }
            }
            
            if (g_RequestNext || g_RequestPrev || g_PendingIndex >= 0 || g_AutoNext) break;
            
            if (g_Playlist.state == PlayState::Playing && g_DecoderReady) {
                ma_uint64 cur = 0, len = 0;
                ma_decoder_get_cursor_in_pcm_frames(&g_AudioDecoder, &cur);
                ma_decoder_get_length_in_pcm_frames(&g_AudioDecoder, &len);
                if (len > 0) g_Playlist.progress = (float)cur / len;
            }
        }
        
        g_IsPlaying = false;
        if (g_Playlist.state != PlayState::Paused && !g_AutoNext && g_PendingIndex < 0 && !g_RequestNext && !g_RequestPrev) {
            SafeStop();
            g_Playlist.state = PlayState::Idle;
            g_CurrentSongId.clear();
            g_CurrentSong = Song();
            SetStatus("播放结束");
        }
    }
    SafeStop();
}

// ==================== 公共接口 ====================
void RequestPlay(int idx) {
    if (idx < 0 || idx >= (int)g_Playlist.songs.size()) return;
    g_RequestNext = g_RequestPrev = false;
    g_PendingIndex = idx;
}

void PlayPause() {
    if (g_Playlist.state == PlayState::Playing) g_RequestPause = true;
    else if (g_Playlist.state == PlayState::Paused) g_RequestResume = true;
    else if (!g_Playlist.songs.empty()) RequestPlay(g_Playlist.currentIndex >= 0 ? g_Playlist.currentIndex : 0);
}

void NextSong() { if (!g_Playlist.songs.empty()) g_RequestNext = true; }
void PrevSong() { if (!g_Playlist.songs.empty()) g_RequestPrev = true; }

void StopPlayback() {
    g_RequestNext = g_RequestPrev = false;
    g_PendingIndex = -1;
    SafeStop();
    g_Playlist.state = PlayState::Idle;
    g_Playlist.progress = 0.0f;
    g_CurrentSongId.clear();
    g_CurrentSong = Song();
    SetStatus("停止");
}

// ==================== 可视化 ====================
void VisualThread() {
    InitWindow();
    while (g_VisualRun) {
        std::vector<float> samples;
        {
            std::lock_guard<std::mutex> lock(g_AudioVisualMutex);
            if (g_AudioBuf.size() >= FFT_SIZE)
                samples.assign(g_AudioBuf.end() - FFT_SIZE, g_AudioBuf.end());
        }
        if (!samples.empty()) {
            std::vector<float> spec(BANDS);
            ComputeFFT(samples, spec);
            std::lock_guard<std::mutex> lock(g_AudioVisualMutex);
            for (int i = 0; i < BANDS; i++)
                g_Spectrum[i] = g_Spectrum[i] * 0.7f + spec[i] * 0.3f;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

void DrawSpectrum(ImVec2 pos, ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (!dl || size.x <= 30 || size.y <= 20) return;

    std::vector<float> spectrumSnapshot;
    {
        std::lock_guard<std::mutex> lock(g_AudioVisualMutex);
        spectrumSnapshot = g_Spectrum;
    }

    // 频谱改为自适应宽度：占满整个底部横条，而不是固定 128*5 只画左侧。
    const float marginX = 10.0f;
    const float bottomPad = 8.0f;
    const float usableW = size.x - marginX * 2.0f;
    const float barStep = usableW / (float)BANDS;
    const float barW = barStep > 2.0f ? barStep - 1.0f : barStep;
    const float sy = pos.y + size.y - bottomPad;
    const float maxH = size.y - 12.0f;

    // 先画一层淡背景，形成完整横条区域。
    dl->AddRectFilled(ImVec2(pos.x + marginX, pos.y), ImVec2(pos.x + size.x - marginX, pos.y + size.y), IM_COL32(20, 20, 25, 70), 6.0f);

    for (int i = 0; i < BANDS; i++) {
        float h = spectrumSnapshot[(size_t)i] * maxH * g_SpectrumGain;
        if (h < 2) h = 2;
        if (h > maxH) h = maxH;
        float x = pos.x + marginX + i * barStep;
        float y = sy - h;
        float t = (float)i / (BANDS - 1);
        int r, g, b;
        if (t < 0.25f)      { r = 50; g = 100 + (int)(t * 4 * 155); b = 255; }
        else if (t < 0.5f)  { r = 50; g = 255; b = 255 - (int)((t-0.25f)*4*155); }
        else if (t < 0.75f) { r = 100 + (int)((t-0.5f)*4*155); g = 255; b = 50; }
        else                { r = 255; g = 255 - (int)((t-0.75f)*4*155); b = 50; }
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + barW, sy), IM_COL32(r, g, b, 190), 4.0f);
    }
}

void DrawProgress(float f, ImVec2 p, ImVec2 s, float r, ImU32 c) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + s.x, p.y + s.y), IM_COL32(100, 100, 100, 150), r);
    if (f > 0) dl->AddRectFilled(p, ImVec2(p.x + s.x * f, p.y + s.y), c, r);
    dl->AddRect(p, ImVec2(p.x + s.x, p.y + s.y), IM_COL32(200, 200, 200, 200), r, 0, 1.0f);
}

// ==================== 初始化 ====================
void InitMusicPlayer() {
    if (g_MusicPlayerInit) return;
    g_ThreadRun = true;
    srand((unsigned)time(NULL));
    g_VisualRun = true;
    g_VisualThread = std::thread(VisualThread);
    g_PlayThread = std::thread(PlayThread);
    g_MusicPlayerInit = true;
}

void CleanupMusicPlayer() {
    if (!g_MusicPlayerInit) return;
    g_VisualRun = g_ThreadRun = false;
    g_IsPlaying = false;
    if (g_SearchThread.joinable()) g_SearchThread.join();
    if (g_VisualThread.joinable()) g_VisualThread.join();
    if (g_PlayThread.joinable()) g_PlayThread.join();
    SafeStop();
    // curl_global_init 由 std::call_once 管理；这里不调用 curl_global_cleanup，
    // 避免清理后再次进入音乐页时 call_once 不会重新初始化导致不可预期行为。
    g_MusicPlayerInit = false;
}

// ==================== UI ====================
void 音乐窗口() {
    InitMusicPlayer();
    
        static char kw[128] = "";
        ImGui::SetNextItemWidth(-120);
        ImGui::InputTextWithHint("##搜索", "搜索音乐", kw, sizeof(kw));
        ImGui::SameLine();
        if (ImGui::Button("搜索", {100, 0}) && strlen(kw) > 0) StartSearchAsync(kw);
        ImGui::Separator();
        
        std::string status; bool err;
        GetStatus(status, err);
        std::vector<Song> songSnapshot;
        {
            std::lock_guard<std::mutex> lock(g_SongListMutex);
            songSnapshot = g_SongList;
        }
        
        ImGui::BeginChild("列表", {0, ImGui::GetContentRegionAvail().y - 150}, false);
        if (!songSnapshot.empty()) {
            ImGui::Text("共 %d 首", (int)songSnapshot.size());
            ImGui::SameLine();
            if (g_Playlist.state == PlayState::Loading) {
                ImGui::PushStyleColor(ImGuiCol_Text, {1,1,0.5f,1});
                ImGui::TextWrapped("加载中...");
                ImGui::PopStyleColor();
            } else if (!status.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, err ? ImVec4(1,0.4f,0.4f,1) : ImVec4(0.5f,0.8f,0.5f,1));
                ImGui::TextWrapped("%s", status.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::Separator();
            
            ImGui::Columns(3, "cols", false);
            ImGui::SetColumnWidth(0, 330); ImGui::SetColumnWidth(1, 190); ImGui::SetColumnWidth(2, 140);
            ImGui::Text("歌曲"); ImGui::NextColumn();
            ImGui::Text("歌手"); ImGui::NextColumn();
            ImGui::Text("操作"); ImGui::NextColumn();
            ImGui::Separator();
            
            for (size_t i = 0; i < songSnapshot.size(); i++) {
                auto& s = songSnapshot[i];
                bool cur = (g_Playlist.currentIndex == (int)i && !g_CurrentSongId.empty() && s.id == g_CurrentSongId);
                if (cur) ImGui::PushStyleColor(ImGuiCol_Text, {0.4f,1,0.4f,1});
                ImGui::Text("%s", s.name.c_str()); ImGui::NextColumn();
                ImGui::Text("%s", s.singers.empty() ? "未知" : s.singers.c_str()); ImGui::NextColumn();
                if (g_Playlist.state == PlayState::Loading) ImGui::BeginDisabled();
                char id[32]; snprintf(id, sizeof(id), "播放##%zu", i);
                if (ImGui::Button(id, {110, 0})) RequestPlay((int)i);
                if (g_Playlist.state == PlayState::Loading) ImGui::EndDisabled();
                ImGui::NextColumn();
                if (cur) ImGui::PopStyleColor();
            }
            ImGui::Columns(1);
        } else if (!err) {
            ImGui::TextColored({0.5f,0.5f,0.5f,1}, "输入歌曲名开始搜索");
        }
        ImGui::EndChild();
        
        ImGui::Separator();
        ImGui::BeginChild("控制", {0, 130}, false, ImGuiWindowFlags_NoScrollbar);
        ImVec2 pos = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize();
        
        std::string info = "未播放";
        float prog = 0, cur = 0, dur = 215;
        if (g_IsPlaying || g_Playlist.state == PlayState::Paused) {
            if (!g_CurrentSong.name.empty())
                info = g_CurrentSong.name + " - " + (g_CurrentSong.singers.empty() ? "未知" : g_CurrentSong.singers);
            else if (g_Playlist.currentIndex >= 0 && g_Playlist.currentIndex < (int)g_Playlist.songs.size()) {
                auto& s = g_Playlist.songs[g_Playlist.currentIndex];
                info = s.name + " - " + (s.singers.empty() ? "未知" : s.singers);
            }
        }
        if (g_Playlist.state == PlayState::Playing) {
            prog = g_Playlist.progress; dur = g_Playlist.duration;
            cur = std::chrono::duration<float>(std::chrono::steady_clock::now() - g_StartTime).count();
            if (cur > dur) cur = dur;
        } else if (g_Playlist.state == PlayState::Paused) {
            prog = g_Playlist.progress; dur = g_Playlist.duration; cur = g_PausedPos;
        }
        if (dur <= 0) dur = 215;
        
        // 顶部信息和按钮占一行；频谱单独占满下面横条，避免文字遮挡律动画面。
        float buttonW = 46.0f;
        float buttonH = 32.0f;
        float gap = 6.0f;
        float groupW = buttonW * 3.0f + gap * 2.0f;
        float groupX = sz.x - groupW - 12.0f;
        if (groupX < 12.0f) groupX = 12.0f;
        float textMaxW = groupX - 24.0f;
        if (textMaxW < 120.0f) textMaxW = sz.x - 24.0f;

        ImGui::SetCursorPos({12, 8});
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textMaxW);
        ImGui::Text("%s", info.c_str());
        ImGui::PopTextWrapPos();
        ImGui::SetCursorPos({12, 34});
        ImGui::Text("%s / %s", FormatTime(cur).c_str(), FormatTime(dur).c_str());

        ImGui::SetCursorPos({groupX, 15});
        if (ImGui::Button("|<", {buttonW, buttonH})) PrevSong();
        ImGui::SameLine(0, gap);
        if (ImGui::Button(g_Playlist.state == PlayState::Playing ? "||" : ">", {buttonW, buttonH})) PlayPause();
        ImGui::SameLine(0, gap);
        if (ImGui::Button(">|", {buttonW, buttonH})) NextSong();

        // 律动横条从 y=58 开始，占满控制区宽度，避免与上方文字/按钮重叠。
        DrawSpectrum({pos.x, pos.y + 58}, {sz.x, 54});
        ImGui::SetCursorPos({12, 116});
        DrawProgress(prog, ImGui::GetCursorScreenPos(), {sz.x - 24, 8}, 4.0f, IM_COL32(66, 150, 250, 255));
        ImGui::Dummy({sz.x - 24, 8});
        ImGui::EndChild();
}

