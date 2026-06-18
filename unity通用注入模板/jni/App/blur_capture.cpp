//
// 高斯模糊 - 帧捕获模块实现
// Hook eglSwapBuffers，在目标 App 的 GL 上下文中用 glReadPixels 截取帧缓冲
// 双缓冲 + 原子指针无锁传递给 ImGui 渲染线程
//
// 性能策略：
// - 降采样到 1/2 尺寸截取（减少 glReadPixels 数据量 4 倍）
// - 降频到每 2 帧截一次（约 30fps，毛玻璃背景不需要 60fps）
// - 双缓冲避免读写冲突
//

#include "blur_capture.hpp"
#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include <cstring>
#include <atomic>
#include <thread>
#include <unistd.h>
#include "dobby.h"
#include "xdl.h"
#include "log.h"

namespace blur_capture {

// ============================================================================
// 配置
// ============================================================================
static constexpr int DOWNSAMPLE = 2;        // 截取降采样倍数（2 = 1/2 尺寸）
static constexpr int FRAME_SKIP = 2;        // 每 N 帧截一次（2 = 30fps）
static constexpr int MAX_WIDTH = 1920;      // 最大截取宽度
static constexpr int MAX_HEIGHT = 1080;     // 最大截取高度

// ============================================================================
// 双缓冲（无锁传递）
// hook 线程写 buffer A/B，ImGui 线程读 buffer B/A
// ============================================================================
struct FrameBuffer {
    uint8_t* data = nullptr;     // RGBA 数据
    int width = 0;
    int height = 0;
    int capacity = 0;
    std::atomic<bool> ready{false};  // true = 数据已就绪可读
};

static FrameBuffer g_buffers[2];
static std::atomic<int> g_write_index{0};   // hook 线程当前写入的 buffer 索引
static std::atomic<int> g_latest_index{-1}; // 最新就绪的 buffer 索引（-1 = 无）
static std::atomic<bool> g_hooked{false};
static std::atomic<int> g_frame_counter{0};

// ============================================================================
// 确保缓冲区容量足够
// ============================================================================
static void ensure_capacity(FrameBuffer& fb, int w, int h) {
    int needed = w * h * 4;
    if (fb.capacity < needed) {
        if (fb.data) delete[] fb.data;
        fb.data = new uint8_t[needed];
        fb.capacity = needed;
    }
    fb.width = w;
    fb.height = h;
}

// ============================================================================
// eglSwapBuffers Hook
// ============================================================================
static EGLBoolean (*orig_eglSwapBuffers)(EGLDisplay dpy, EGLSurface surface) = nullptr;

static EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    // 降频：每 FRAME_SKIP 帧截一次
    int counter = g_frame_counter.fetch_add(1, std::memory_order_relaxed);
    bool should_capture = (counter % FRAME_SKIP == 0) && g_hooked.load(std::memory_order_relaxed);

    if (should_capture) {
        // 查询当前绘制缓冲尺寸
        EGLint width = 0, height = 0;
        eglQuerySurface(dpy, surface, EGL_WIDTH, &width);
        eglQuerySurface(dpy, surface, EGL_HEIGHT, &height);

        if (width > 0 && height > 0 && width <= MAX_WIDTH && height <= MAX_HEIGHT) {
            // 降采样后的尺寸
            int cw = width / DOWNSAMPLE;
            int ch = height / DOWNSAMPLE;
            if (cw > 0 && ch > 0) {
                int widx = g_write_index.load(std::memory_order_relaxed);
                FrameBuffer& fb = g_buffers[widx];

                // 确保容量
                ensure_capacity(fb, cw, ch);

                // 设置视口到降采样尺寸，然后用 glReadPixels 读取
                // 注意：此时还在目标 App 的 GL 上下文中，默认 framebuffer 是它的渲染结果
                // glReadPixels 读取的是当前绑定的 framebuffer
                glReadPixels(0, 0, cw, ch, GL_RGBA, GL_UNSIGNED_BYTE, fb.data);

                // 标记就绪，切换 latest 指针
                fb.ready.store(true, std::memory_order_release);
                g_latest_index.store(widx, std::memory_order_release);

                // 切换 write index（双缓冲交替）
                g_write_index.store(1 - widx, std::memory_order_relaxed);
            }
        }
    }

    // 调用原始 eglSwapBuffers
    return orig_eglSwapBuffers ? orig_eglSwapBuffers(dpy, surface) : EGL_FALSE;
}

// ============================================================================
// 安装 Hook
// ============================================================================
static void install_hook() {
    if (g_hooked.load()) return;

    // 通过 xdl 查找 eglSwapBuffers
    void* handle = xdl_open("libEGL.so", XDL_DEFAULT);
    if (!handle) {
        LOGE("[blur_capture] xdl_open libEGL.so 失败");
        return;
    }

    size_t sym_size = 0;
    void* sym = xdl_sym(handle, "eglSwapBuffers", &sym_size);
    if (!sym) {
        LOGE("[blur_capture] 未找到 eglSwapBuffers 符号");
        xdl_close(handle);
        return;
    }

    int ret = DobbyHook(sym, (void*)hook_eglSwapBuffers, (void**)&orig_eglSwapBuffers);
    xdl_close(handle);

    if (ret != 0) {
        LOGE("[blur_capture] DobbyHook 失败: %d", ret);
        return;
    }

    g_hooked.store(true, std::memory_order_release);
    LOGI("[blur_capture] ✓ eglSwapBuffers Hook 安装成功");
}

// ============================================================================
// init - 在独立线程中安装 Hook（避免阻塞主初始化）
// ============================================================================
void init() {
    std::thread([]() {
        // 等待 libEGL.so 完全加载
        for (int i = 0; i < 30; i++) {
            void* h = xdl_open("libEGL.so", XDL_DEFAULT);
            if (h) { xdl_close(h); break; }
            usleep(200000); // 200ms
        }
        install_hook();
    }).detach();
}

// ============================================================================
// 获取最新帧
// ============================================================================
const uint8_t* get_latest_frame(int* out_w, int* out_h) {
    int idx = g_latest_index.load(std::memory_order_acquire);
    if (idx < 0 || idx > 1) {
        if (out_w) *out_w = 0;
        if (out_h) *out_h = 0;
        return nullptr;
    }
    FrameBuffer& fb = g_buffers[idx];
    if (!fb.ready.load(std::memory_order_acquire)) {
        if (out_w) *out_w = 0;
        if (out_h) *out_h = 0;
        return nullptr;
    }
    if (out_w) *out_w = fb.width;
    if (out_h) *out_h = fb.height;
    return fb.data;
}

// ============================================================================
// 是否有新帧
// ============================================================================
bool has_new_frame() {
    int idx = g_latest_index.load(std::memory_order_acquire);
    return idx >= 0 && g_buffers[idx].ready.load(std::memory_order_acquire);
}

} // namespace blur_capture
