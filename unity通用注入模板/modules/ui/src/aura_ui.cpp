//
// Aura UI 模块实现
// 移植自 AuraNexus 项目，适配 Vulkan 渲染后端
// 所有 widget 实现、配色、动画参数、窗口结构与原版完全一致
//

#include "stb_image.h"
#include "aura_ui.hpp"
#include "LOGO.h"
#include "vulkan_helper.h"
#include "imgui_impl_vulkan.h"
#include "ImGuiDraw.h"
#include "native_register_vk.h"
#include "logger.h"
#include "KittyMemory.h"
#include "il2cpp_api.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <cmath>
#include <unistd.h>
#include <fcntl.h>
#include <sys/sysinfo.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <vector>
#include <algorithm>

namespace aura_ui {

// ============================================================================
// 全局变量（原版 main.cpp + draw.cpp）
// ============================================================================
ImTextureID LOGO = 0;
static VkImage g_logo_image = VK_NULL_HANDLE;
static VkDeviceMemory g_logo_memory = VK_NULL_HANDLE;
static VkImageView g_logo_view = VK_NULL_HANDLE;
static VkSampler g_logo_sampler = VK_NULL_HANDLE;

// 原版 main.cpp 全局变量
static float menu_expand = 0.0f;
static bool last_menu_state = false;
static bool MainAuraOne = true;
float FPSControlSize = 60.0f;
int background_mode = 0;
float ImGuiDrawESP = 0.7f;
float ImGuiDrawESP2 = 0.7f;
int Prevent = 0;
float window_opacity = 0.85f;  // 窗口透明度（0=全透明, 1=不透明）

// ============================================================================
// 绘制球体功能（基于 IL2CPP API 访问 GameCoreCenter.BallDic）
// 通过 il2cpp_class_from_name + il2cpp_field_get_offset 动态获取字段偏移
// 不依赖硬编码偏移，游戏更新也能自适应
// ============================================================================

// 球体数据结构
struct SphereEntity {
    float x;        // 世界坐标 X
    float y;        // 世界坐标 Y
    float radius;   // 球体半径
    int rankId;     // 组ID
};

#define MAX_SPHERES 128

// ---- 球体绘制全局变量 ----
static bool g_draw_sphere_enabled = false;       // 绘制球体开关
static bool g_draw_sphere_line = true;           // 绘制连线
static bool g_draw_sphere_circle = true;         // 绘制圆球
static float g_sphere_scale = 1.0f;              // 比例

// IL2CPP 缓存的类和字段偏移
static bool g_il2cpp_sphere_inited = false;
static Il2CppClass* g_class_GameCoreCenter = nullptr;
static Il2CppClass* g_class_DrawCircle = nullptr;
static size_t g_off_instance = (size_t)-1;       // GameCoreCenter.instance 静态字段偏移
static size_t g_off_BallDic = (size_t)-1;        // GameCoreCenter.BallDic 实例字段偏移
static size_t g_off_DC_Radius = (size_t)-1;      // DrawCircle.Radius
static size_t g_off_DC_pos = (size_t)-1;         // DrawCircle.pos (Vector3)
static size_t g_off_DC_SelfTF = (size_t)-1;      // DrawCircle.SelfTF (Transform)
static size_t g_off_DC_ID = (size_t)-1;          // DrawCircle.ID
static size_t g_off_DC_curScore = (size_t)-1;    // DrawCircle.curScore

// Unity icall 函数指针
// Transform.get_position_Injected(Transform* self, Vector3* outPos)
typedef void (*Transform_get_position_t)(void* self, void* outPos);
static Transform_get_position_t p_Transform_get_position = nullptr;

// 球体列表
static SphereEntity g_sphere_list[MAX_SPHERES];
static int g_sphere_count = 0;
static std::atomic<bool> g_sphere_searching{false};
static std::atomic<bool> g_sphere_search_failed{false};
static std::thread g_sphere_thread;

// 我方球体世界坐标（用于坐标转换中心点）
static float g_my_sphere_world_x = 0.0f;
static float g_my_sphere_world_y = 0.0f;
static bool g_my_sphere_valid = false;

// 初始化 IL2CPP 字段偏移
static bool InitSphereIl2cppOffsets() {
    if (g_il2cpp_sphere_inited) return true;
    if (!il2cpp_api_init()) {
        g_sphere_search_failed = true;
        return false;
    }

    // 查找 GameCoreCenter 类（无命名空间）
    g_class_GameCoreCenter = il2cpp_get_class("", "GameCoreCenter");
    if (!g_class_GameCoreCenter) {
        LOGE("SphereDraw: 找不到 GameCoreCenter 类");
        g_sphere_search_failed = true;
        return false;
    }

    // 查找 DrawCircle 类
    g_class_DrawCircle = il2cpp_get_class("", "DrawCircle");
    if (!g_class_DrawCircle) {
        LOGE("SphereDraw: 找不到 DrawCircle 类");
        g_sphere_search_failed = true;
        return false;
    }

    // 获取字段偏移
    // GameCoreCenter.instance 是静态字段，用 il2cpp_get_static_field_data 获取静态区
    // BallDic 是实例字段
    g_off_BallDic = il2cpp_get_field_offset(g_class_GameCoreCenter, "BallDic");
    if (g_off_BallDic == (size_t)-1) {
        LOGE("SphereDraw: 找不到 GameCoreCenter.BallDic 字段");
        g_sphere_search_failed = true;
        return false;
    }

    g_off_DC_Radius = il2cpp_get_field_offset(g_class_DrawCircle, "Radius");
    g_off_DC_pos = il2cpp_get_field_offset(g_class_DrawCircle, "pos");
    g_off_DC_SelfTF = il2cpp_get_field_offset(g_class_DrawCircle, "SelfTF");
    g_off_DC_ID = il2cpp_get_field_offset(g_class_DrawCircle, "ID");
    g_off_DC_curScore = il2cpp_get_field_offset(g_class_DrawCircle, "curScore");

    LOGI("SphereDraw: 偏移 BallDic=0x%zx Radius=0x%zx pos=0x%zx SelfTF=0x%zx",
         g_off_BallDic, g_off_DC_Radius, g_off_DC_pos, g_off_DC_SelfTF);

    // 获取 Unity Transform.get_position icall
    // 这个 icall 返回的是内部函数，签名: void get_position_Injected(Transform*, Vector3*)
    p_Transform_get_position = (Transform_get_position_t)il2cpp_get_icall("UnityEngine.Transform::get_position_Injected");

    g_il2cpp_sphere_inited = true;
    return true;
}

// 读取 Vector3（通过 Transform icall 或直接读内存）
struct Vec3 { float x, y, z; };

static Vec3 GetTransformPosition(void* transform) {
    Vec3 pos = {0, 0, 0};
    if (!transform) return pos;
    if (p_Transform_get_position) {
        // 用 icall 获取世界坐标（最准确）
        p_Transform_get_position(transform, &pos);
    } else {
        // fallback: 直接读 Transform 内部数据（不准确，不同版本布局不同）
        // 这里不实现，依赖 icall
    }
    return pos;
}

// 遍历 Dictionary<UInt32, DrawCircle> 获取所有 DrawCircle
// IL2CPP Dictionary 内部结构:
//   entries 数组在偏移 0x68（不同版本可能不同）
//   count 在偏移 0x58
// 这里用 il2cpp 调用 GetEnumerator 的方式太复杂，改用直接读内存
// 但 Dictionary 内部布局版本相关，所以我们用更简单的方式：
// 通过 GameCoreCenter.BallUpdateList（BetterList<ControlBase>）遍历
// 或者直接遍历 Dictionary 的 entries

// 实际上，最可靠的方式是 Hook 渲染方法。但这里我们用 Dictionary 内部遍历
// IL2CPP Dictionary 布局（Unity 2019+）:
//   offset 0x18: buckets (int[])
//   offset 0x20: entries (Entry[])  <- 关键
//   offset 0x28: count
//   offset 0x40: version
// Entry 结构: { int hashCode, int next, TKey key, TValue value }
//   对于 Dictionary<UInt32, DrawCircle>:
//   { int hashCode(4), int next(4), UInt32 key(4), padding(4), void* value(8) } = 24 bytes
// 但这个布局版本相关，我们用 il2cpp_get_field_offset 动态获取

// 简化方案：直接读 Dictionary 的 entries 数组和 count
// entries 字段偏移需要动态获取
static size_t g_off_Dict_entries = (size_t)-1;
static size_t g_off_Dict_count = (size_t)-1;

// 后台线程：持续读取球体数据
static void UpdateSpheresThread() {
    // 初始化 IL2CPP
    while (g_draw_sphere_enabled && !g_il2cpp_sphere_inited) {
        g_sphere_searching = true;
        if (InitSphereIl2cppOffsets()) {
            g_sphere_searching = false;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    g_sphere_searching = false;

    if (!g_draw_sphere_enabled || !g_il2cpp_sphere_inited) return;

    // 持续读取球体数据
    while (g_draw_sphere_enabled) {
        // 获取 GameCoreCenter.instance（静态字段）
        void* static_data = il2cpp_get_static_field_data(g_class_GameCoreCenter);
        if (!static_data) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // instance 是 <instance>k__BackingField，需要找到它的偏移
        // 静态字段在 static_data 中，偏移需要单独获取
        // 这里用 il2cpp_get_static_field_value 获取 instance
        void* instance = nullptr;
        il2cpp_get_static_field_value(g_class_GameCoreCenter, "<instance>k__BackingField", &instance);
        if (!instance) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // 读取 BallDic（Dictionary<UInt32, DrawCircle>*）
        void* ballDic = il2cpp_read_ptr(instance, g_off_BallDic);
        if (!ballDic) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // 获取 Dictionary 的 entries 和 count
        // 尝试动态获取 entries 字段偏移
        if (g_off_Dict_entries == (size_t)-1) {
            Il2CppClass* dictClass = il2cpp_get_class("System.Collections.Generic", "Dictionary`2");
            if (dictClass) {
                g_off_Dict_entries = il2cpp_get_field_offset(dictClass, "_entries");
                g_off_Dict_count = il2cpp_get_field_offset(dictClass, "_count");
                LOGI("SphereDraw: Dictionary _entries=0x%zx _count=0x%zx", g_off_Dict_entries, g_off_Dict_count);
            }
        }

        SphereEntity local_list[MAX_SPHERES];
        int local_count = 0;
        float myRadius = 0.0f;
        Vec3 myPos = {0, 0, 0};

        if (g_off_Dict_entries != (size_t)-1 && g_off_Dict_count != (size_t)-1) {
            // 读取 count
            int count = il2cpp_read_field<int>(ballDic, g_off_Dict_count);
            if (count < 0) count = 0;
            if (count > 1024) count = 1024;

            // 读取 entries 数组（IL2CPP 数组结构: [ptr, monitor, bounds(8), length(8), data...]）
            void* entriesArr = il2cpp_read_ptr(ballDic, g_off_Dict_entries);
            if (entriesArr) {
                // IL2CPP 数组: 偏移 0x18 是 length，0x20 开始是数据
                // 但实际布局: Il2CppArray 头部 + bounds + max_length + data
                // 简化: 假设偏移 0x18 是 length，0x20 开始是元素
                int arrLen = il2cpp_read_field<int>(entriesArr, 0x18);
                if (arrLen > 1024) arrLen = 1024;

                // Entry 结构: { int hashCode, int next, UInt32 key, padding, void* value }
                // = 4 + 4 + 4 + 4 + 8 = 24 bytes (32位下可能不同)
                // 但在 64 位下，指针是 8 字节，可能有对齐
                // 假设 Entry: hashCode(4) + next(4) + key(4) + pad(4) + value(8) = 24
                const int ENTRY_SIZE = 24;
                const int ENTRY_VALUE_OFFSET = 16;  // value 在 Entry 中的偏移
                const int ENTRY_HASH_OFFSET = 0;    // hashCode 在 Entry 中的偏移

                for (int i = 0; i < arrLen && local_count < MAX_SPHERES; i++) {
                    char* entry = (char*)entriesArr + 0x20 + i * ENTRY_SIZE;
                    int hashCode = *(int*)(entry + ENTRY_HASH_OFFSET);
                    if (hashCode < 0) continue;  // 空槽位

                    void* drawCircle = *(void**)(entry + ENTRY_VALUE_OFFSET);
                    if (!drawCircle) continue;

                    // 读取 DrawCircle 数据
                    float radius = il2cpp_read_field<float>(drawCircle, g_off_DC_Radius);
                    if (radius <= 0.0f) continue;

                    // 读取位置：优先用 SelfTF（Transform）的世界坐标
                    Vec3 pos = {0, 0, 0};
                    if (g_off_DC_SelfTF != (size_t)-1) {
                        void* selfTF = il2cpp_read_ptr(drawCircle, g_off_DC_SelfTF);
                        if (selfTF) {
                            pos = GetTransformPosition(selfTF);
                        }
                    }
                    // fallback: 读 pos 字段
                    if (pos.x == 0 && pos.y == 0 && pos.z == 0 && g_off_DC_pos != (size_t)-1) {
                        pos = il2cpp_read_field<Vec3>(drawCircle, g_off_DC_pos);
                    }

                    local_list[local_count].x = pos.x;
                    local_list[local_count].y = pos.y;
                    local_list[local_count].radius = radius;
                    local_list[local_count].rankId = 0;
                    local_count++;

                    // 记录最大的球体作为我方球体
                    if (radius > myRadius) {
                        myRadius = radius;
                        myPos = pos;
                    }
                }
            }
        }

        // 更新全局数据
        memcpy(g_sphere_list, local_list, sizeof(SphereEntity) * local_count);
        g_sphere_count = local_count;
        g_my_sphere_world_x = myPos.x;
        g_my_sphere_world_y = myPos.y;
        g_my_sphere_valid = (local_count > 0);

        std::this_thread::sleep_for(std::chrono::milliseconds(16));  // ~60fps
    }
}

// ============================================================================
// 游戏功能 stub（原版 MainDefinition.h 第889行起 + gjc.h）
// 本项目无对应游戏功能，提供空实现以保持 UI 与原版一致
// ============================================================================
std::atomic<bool> lb_gjc_injected{false};
std::string lb_gjc_status = "\xe6\x9c\xaa\xe6\xb3\xa8\xe5\x85\xa5"; // 未注入
std::atomic<bool> lb_game_running{false};
std::atomic<bool> lb_game_ready{false};
static unsigned long lb_base_libUE4 = 0;

// 获取注入软件的包名（从 /proc/self/cmdline 读取，即当前进程名）
static std::string get_injected_package_name() {
    static std::string cached_name;
    if (!cached_name.empty()) return cached_name;
    int fd = open("/proc/self/cmdline", O_RDONLY);
    if (fd >= 0) {
        char buf[256] = {0};
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n > 0) cached_name = std::string(buf);
    }
    if (cached_name.empty()) cached_name = "unknown";
    return cached_name;
}

std::atomic<bool> lb_feature_luna{false};
std::atomic<bool> lb_feature_head{false};
std::atomic<bool> lb_feature_body{false};
std::atomic<bool> lb_feature_bullet{false};
std::atomic<bool> lb_feature_norecoil{false};
std::atomic<bool> lb_feature_thirdperson{false};
std::atomic<bool> lb_feature_neitou{false};

std::atomic<float> lb_head_val{90.0f};
std::atomic<float> lb_body_val{80.0f};

std::atomic<bool> lb_freeze_running{false};
std::atomic<float> lb_abdominal_target{99.0f};
std::thread lb_freeze_thread;

uintptr_t lb_bullet_addr = 0;
int lb_bullet_val = 505873377;
std::atomic<bool> lb_bullet_frozen{false};

uintptr_t lb_norecoil_addr = 0;
int lb_norecoil_val = -1119858432;
std::atomic<bool> lb_norecoil_frozen{false};

uintptr_t lb_thirdperson_addr = 0;
int lb_thirdperson_val = 1384120352;
std::atomic<bool> lb_thirdperson_frozen{false};
std::thread lb_thirdperson_thread;

uintptr_t lb_neitou_world_ptr = 0;
std::atomic<bool> lb_neitou_running{false};
std::thread lb_neitou_thread;
std::string lb_neitou_status = "\xe6\x9c\xaa\xe5\x90\xaf\xe5\x8a\xa8"; // 未启动

void lb_init_game() {}
void lb_clean_exit() {}
void lb_writeDword(uintptr_t, int) {}
void lb_freeze_thread_func() {}
void lb_thirdperson_thread_func() {}
void lb_neitou_thread_func() {}

// ============================================================================
// init - 加载 LOGO 纹理（Vulkan 后端：创建 VkImage + 上传 + 注册 DescriptorSet）
// ============================================================================
void init() {
    if (LOGO != 0) return;
    int w, h, channels;
    unsigned char* data = stbi_load_from_memory(Logo, sizeof(Logo), &w, &h, &channels, 4);
    if (!data) { LOGE("[AuraUI] LOGO PNG decode failed"); return; }

    VkDevice device = g_Vk.device;
    VkPhysicalDevice phys = g_Vk.physicalDevice;
    VkCommandPool cmdPool = g_Vk.commandPool;
    VkQueue queue = g_Vk.graphicsQueue;

    // 1. 创建 staging buffer
    VkDeviceSize imageSize = (VkDeviceSize)w * h * 4;
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    VkBufferCreateInfo bci{}; bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = imageSize; bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(device, &bci, nullptr, &stagingBuffer);
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device, stagingBuffer, &memReq);
    VkMemoryAllocateInfo bai{}; bai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    bai.allocationSize = memReq.size;
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(phys, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((memReq.memoryTypeBits & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            bai.memoryTypeIndex = i; break;
        }
    }
    vkAllocateMemory(device, &bai, nullptr, &stagingMemory);
    vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);
    void* mapped = nullptr;
    vkMapMemory(device, stagingMemory, 0, imageSize, 0, &mapped);
    memcpy(mapped, data, (size_t)imageSize);
    vkUnmapMemory(device, stagingMemory);
    stbi_image_free(data);

    // 2. 创建 VkImage
    VkImageCreateInfo ici{}; ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent = {(uint32_t)w, (uint32_t)h, 1};
    ici.mipLevels = 1; ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(device, &ici, nullptr, &g_logo_image);
    vkGetImageMemoryRequirements(device, g_logo_image, &memReq);
    VkMemoryAllocateInfo iai{}; iai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    iai.allocationSize = memReq.size;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((memReq.memoryTypeBits & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            iai.memoryTypeIndex = i; break;
        }
    }
    vkAllocateMemory(device, &iai, nullptr, &g_logo_memory);
    vkBindImageMemory(device, g_logo_image, g_logo_memory, 0);

    // 3. 录制命令缓冲：布局转换 + 拷贝
    VkCommandBufferAllocateInfo cai{}; cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = cmdPool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &cai, &cmd);
    VkCommandBufferBeginInfo cbi{}; cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &cbi);

    // 转换 UNDEFINED -> TRANSFER_DST
    VkImageMemoryBarrier b1{};
    b1.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b1.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b1.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b1.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b1.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b1.image = g_logo_image;
    b1.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b1.srcAccessMask = 0;
    b1.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b1);

    // 拷贝 buffer -> image
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {(uint32_t)w, (uint32_t)h, 1};
    vkCmdCopyBufferToImage(cmd, stagingBuffer, g_logo_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // 转换 TRANSFER_DST -> SHADER_READ_ONLY
    VkImageMemoryBarrier b2{};
    b2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b2.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b2.image = g_logo_image;
    b2.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b2);

    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, cmdPool, 1, &cmd);

    // 清理 staging
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);

    // 4. 创建 VkImageView
    VkImageViewCreateInfo vci{}; vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = g_logo_image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VK_FORMAT_R8G8B8A8_UNORM;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(device, &vci, nullptr, &g_logo_view);

    // 5. 创建 VkSampler
    VkSamplerCreateInfo sci{}; sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    vkCreateSampler(device, &sci, nullptr, &g_logo_sampler);

    // 6. 注册到 ImGui Vulkan 后端，获取 VkDescriptorSet 作为 ImTextureID
    VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(g_logo_sampler, g_logo_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    LOGO = (ImTextureID)(intptr_t)ds;
    LOGI("[AuraUI] LOGO texture created (%dx%d)", w, h);
}

// ============================================================================
// DrawAuraSectionTitle（原版 MainDefinition.h 第17-41行）
// ============================================================================
void DrawAuraSectionTitle(const char* title) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

    float title_height = ImGui::CalcTextSize(title).y;
    float line_width = 4.0f;
    float spacing = 8.0f;

    ImVec2 cursor_pos = window->DC.CursorPos;

    // 左侧竖条 - Mac 蓝色
    ImVec2 line_p1(cursor_pos.x, cursor_pos.y);
    ImVec2 line_p2(cursor_pos.x + line_width, cursor_pos.y + title_height);
    window->DrawList->AddRectFilled(line_p1, line_p2, IM_COL32(0, 122, 255, 255), 2.0f);

    // 标题文字 - Mac 风格白色
    ImVec2 text_pos(cursor_pos.x + line_width + spacing, cursor_pos.y);
    window->DrawList->AddText(text_pos, IM_COL32(235, 235, 240, 255), title);

    ImGui::ItemSize(ImVec2(0, title_height + style.ItemSpacing.y), 0.0f);
}

// ============================================================================
// _FreqControl 类（原版 MainDefinition.h 第43-161行）
// CPU/GPU/RAM 使用率读取
// ============================================================================
class _FreqControl {
private:
    static std::chrono::steady_clock::time_point cpuLastTime;
    static float cpuCachedValue;
    static unsigned long long cpuLastTotalUser;
    static unsigned long long cpuLastTotalUserLow;
    static unsigned long long cpuLastTotalSys;
    static unsigned long long cpuLastTotalIdle;
    static std::chrono::steady_clock::time_point memLastTime;
    static float memCachedPercent;
    static std::chrono::steady_clock::time_point gpuLastTime;
    static int gpuCachedValue;
    static constexpr int CPU_MIN_INTERVAL = 1000;
    static constexpr int MEM_MIN_INTERVAL = 2000;
    static constexpr int GPU_MIN_INTERVAL = 1000;
public:
    static float GetCPUUsage();
    static float GetMemPercent();
    static int GetGPUUsage();
private:
    static float _ReadCPU();
    static float _ReadMemPercent();
    static int _ReadGPU();
};
std::chrono::steady_clock::time_point _FreqControl::cpuLastTime =
    std::chrono::steady_clock::now() - std::chrono::milliseconds(2000);
float _FreqControl::cpuCachedValue = 0.0f;
unsigned long long _FreqControl::cpuLastTotalUser = 0;
unsigned long long _FreqControl::cpuLastTotalUserLow = 0;
unsigned long long _FreqControl::cpuLastTotalSys = 0;
unsigned long long _FreqControl::cpuLastTotalIdle = 0;
std::chrono::steady_clock::time_point _FreqControl::memLastTime =
    std::chrono::steady_clock::now() - std::chrono::milliseconds(3000);
float _FreqControl::memCachedPercent = 0.0f;
std::chrono::steady_clock::time_point _FreqControl::gpuLastTime =
    std::chrono::steady_clock::now() - std::chrono::milliseconds(2000);
int _FreqControl::gpuCachedValue = 0;

float _FreqControl::_ReadCPU() {
    unsigned long long user, nice, sys, idle;
    FILE* f = fopen("/proc/stat", "r");
    if (!f) return cpuCachedValue;
    if (fscanf(f, "cpu %llu %llu %llu %llu", &user, &nice, &sys, &idle) != EOF) {
        if (cpuLastTotalUser != 0) {
            unsigned long long d_user = user - cpuLastTotalUser;
            unsigned long long d_nice = nice - cpuLastTotalUserLow;
            unsigned long long d_sys  = sys - cpuLastTotalSys;
            unsigned long long d_idle = idle - cpuLastTotalIdle;
            unsigned long long total = d_user + d_nice + d_sys;
            unsigned long long all   = total + d_idle;
            if (all > 0)
                cpuCachedValue = (float)total / all * 100.0f;
        }
        cpuLastTotalUser= user;
        cpuLastTotalUserLow = nice;
        cpuLastTotalSys = sys;
        cpuLastTotalIdle= idle;
    }
    fclose(f);
    return cpuCachedValue;
}
float _FreqControl::GetCPUUsage() {
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - cpuLastTime).count();
    if (ms >= CPU_MIN_INTERVAL) {
        cpuLastTime = now;
        cpuCachedValue = _ReadCPU();
    }
    return cpuCachedValue;
}
float _FreqControl::_ReadMemPercent() {
    long total = 0, avail = 0;
    FILE* f = fopen("/proc/meminfo", "r");
    if (!f) return memCachedPercent;
    char buf[256];
    while (fgets(buf, sizeof(buf), f)) {
        if (!total) sscanf(buf, "MemTotal: %ld kB", &total);
        if (sscanf(buf, "MemAvailable: %ld kB", &avail)) break;
    }
    fclose(f);
    if (total <= 0) return 0.0f;
    return 100.0f - (float)avail / total * 100.0f;
}
float _FreqControl::GetMemPercent() {
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - memLastTime).count();
    if (ms >= MEM_MIN_INTERVAL) {
        memLastTime = now;
        memCachedPercent = _ReadMemPercent();
    }
    return memCachedPercent;
}
int _FreqControl::_ReadGPU() {
    const char* paths[] = {
        "/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage",
        "/sys/devices/platform/kgsl-3d0/kgsl/kgsl-3d0/gpu_busy_percentage",
        nullptr
    };
    for (const char* p : paths) {
        if (!p) continue;
        FILE* f = fopen(p, "r");
        if (!f) continue;
        int val = 0;
        if (fscanf(f, "%d", &val) == 1) {
            fclose(f);
            return val;
        }
        fclose(f);
    }
    return gpuCachedValue;
}
int _FreqControl::GetGPUUsage() {
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - gpuLastTime).count();
    if (ms >= GPU_MIN_INTERVAL) {
        gpuLastTime = now;
        gpuCachedValue = _ReadGPU();
    }
    return gpuCachedValue;
}

// ============================================================================
// FPSCounter 类（原版 MainDefinition.h 第162-181行）
// ============================================================================
class FPSCounter {
private:
    static long long lastTick;
    static int frames;
    static int currentFPS;
public:
    static int GetFPS() {
        long long now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        frames++;
        if (now - lastTick >= 1000) {
            currentFPS = frames;
            frames = 0;
            lastTick = now;
        }
        return currentFPS;
    }
};
long long FPSCounter::lastTick = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
int FPSCounter::frames = 0;
int FPSCounter::currentFPS = 0;

int GetRealFPS() { return FPSCounter::GetFPS(); }
int GetRealCPU() { return (int)_FreqControl::GetCPUUsage(); }
int GetRealGPU() { return _FreqControl::GetGPUUsage(); }
int GetRealRAMPercent()  { return (int)_FreqControl::GetMemPercent(); }

// ============================================================================
// DrawCircleGauge（原版 MainDefinition.h 第192-204行）
// ============================================================================
void DrawCircleGauge(ImVec2 center, float radius, float progress, ImU32 color, float thickness) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;
    progress = ImClamp(progress, 0.0f, 1.0f);
    window->DrawList->PathArcTo(center, radius, 0.0f, IM_PI * 2.0f, 32);
    window->DrawList->PathStroke(IM_COL32(60, 60, 66, 100), 0, thickness);
    if (progress > 0.0f) {
        float angle_start = -IM_PI * 0.5f;
        float angle_end = angle_start + progress * IM_PI * 2.0f;
        window->DrawList->PathArcTo(center, radius, angle_start, angle_end, 32);
        window->DrawList->PathStroke(color, 0, thickness);
    }
}

// ============================================================================
// DrawSystemInfoCard（原版 MainDefinition.h 第206-369行）
// ============================================================================
bool DrawSystemInfoCard(SystemInfoType type, float value, const ImVec2& size) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

    const char* title = "";
    const char* unit = "";
    float max_value = 100.0f;

    switch (type) {
        case SystemInfoType::FPS:
            title = "FPS";
            unit = "fps";
            max_value = 240.0f;
            break;
        case SystemInfoType::CPU:
            title = "CPU";
            unit = "%";
            max_value = 100.0f;
            break;
        case SystemInfoType::GPU:
            title = "GPU";
            unit = "%";
            max_value = 100.0f;
            break;
        case SystemInfoType::RAM:
            title = "RAM";
            unit = "%";
            max_value = 100.0f;
            break;
    }

    const ImGuiID id = window->GetID(title);
    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size_final = ImGui::CalcItemSize(size, 0.0f, 0.0f);
    const ImRect bb(pos, ImVec2(pos.x + size_final.x, pos.y + size_final.y));

    ImGui::ItemSize(size_final, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, 0);

    bool* is_active = ImGui::GetStateStorage()->GetBoolRef(id, false);
    if (pressed) *is_active = !(*is_active);

    // 动画效果
    float* anim_y = ImGui::GetStateStorage()->GetFloatRef(id + 0x1000, 0.0f);
    float* anim_scale = ImGui::GetStateStorage()->GetFloatRef(id + 0x2000, 1.0f);
    const float dt = g.IO.DeltaTime;
    const float speed = 8.0f;

    float target_y = (hovered && !held) ? -2.0f : 0.0f;
    *anim_y = ImLerp(*anim_y, target_y, dt * speed);

    float target_scale = (held && hovered) ? 0.98f : 1.0f;
    *anim_scale = ImLerp(*anim_scale, target_scale, dt * speed);

    ImVec2 center = bb.GetCenter();
    ImVec2 animated_size = ImVec2(size_final.x * *anim_scale, size_final.y * *anim_scale);
    ImRect animated_bb(
        ImVec2(center.x - animated_size.x * 0.5f, center.y - animated_size.y * 0.5f + *anim_y),
        ImVec2(center.x + animated_size.x * 0.5f, center.y + animated_size.y * 0.5f + *anim_y)
    );

    // Mac 黑色毛玻璃风格配色
    ImU32 col_bg, col_border, col_text, col_sub_text;
    if (held && hovered) {
        col_bg = IM_COL32(28, 28, 32, 220);
        col_border = IM_COL32(80, 80, 90, 200);
        col_text = IM_COL32(255, 255, 255, 255);
        col_sub_text = IM_COL32(255, 255, 255, 153);
    } else if (*is_active) {
        col_bg = IM_COL32(0, 122, 255, 30);
        col_border = IM_COL32(0,122,255,255);
        col_text = IM_COL32(255, 255, 255, 255);
        col_sub_text = IM_COL32(255, 255, 255, 180);
    } else if (hovered) {
        col_bg = IM_COL32(44, 44, 48, 200);
        col_border = IM_COL32(60, 60, 66, 200);
        col_text = IM_COL32(235, 235, 240, 255);
        col_sub_text = IM_COL32(235, 235, 240, 153);
    } else {
        col_bg = IM_COL32(20, 20, 24, 160);
        col_border = IM_COL32(50, 50, 55, 120);
        col_text = IM_COL32(200, 200, 205, 255);
        col_sub_text = IM_COL32(200, 200, 205, 140);
    }

    // 绘制背景和边框
    ImGui::RenderFrame(animated_bb.Min, animated_bb.Max, col_bg, true, 10.0f);
    window->DrawList->AddRect(animated_bb.Min, animated_bb.Max, col_border, 10.0f, 0, 1.0f);
    ImGui::RenderNavCursor(animated_bb, id);

    // 计算圆环位置（居中）
    ImVec2 content_center = ImVec2(
        animated_bb.Min.x + (animated_bb.Max.x - animated_bb.Min.x) * 0.5f,
        animated_bb.Min.y + (animated_bb.Max.y - animated_bb.Min.y) * 0.5f
    );

    float gauge_radius = ImMin(size_final.x, size_final.y) * 0.30f;
    float progress = value / max_value;

    // 圆环颜色
    ImU32 gauge_color;
    switch (type) {
        case SystemInfoType::FPS:
            gauge_color = IM_COL32(52, 199, 89, 255);   // Mac 绿色
            break;
        case SystemInfoType::CPU:
            gauge_color = IM_COL32(255, 159, 10, 255);  // Mac 橙色
            break;
        case SystemInfoType::GPU:
            gauge_color = IM_COL32(191, 90, 242, 255);  // Mac 紫色
            break;
        case SystemInfoType::RAM:
            gauge_color = IM_COL32(0, 122, 255, 255);   // Mac 蓝色
            break;
        default:
            gauge_color = IM_COL32(0, 122, 255, 255);
            break;
    }

    // 绘制圆环
    DrawCircleGauge(content_center, gauge_radius, progress, gauge_color, 5.0f);

    // 绘制数值（居中，大字）
    char value_buf[16];
    if (type == SystemInfoType::FPS) {
        snprintf(value_buf, sizeof(value_buf), "%.0f", value);
    } else {
        snprintf(value_buf, sizeof(value_buf), "%.0f", value);
    }

    ImVec2 value_size = ImGui::CalcTextSize(value_buf);
    ImVec2 title_size = ImGui::CalcTextSize(title);

    // 计算文本整体高度（数值 + 标题）
    float text_spacing = 2.0f;
    float total_text_height = value_size.y + text_spacing + title_size.y;

    // 数值位置（居中偏上）
    ImVec2 value_pos = ImVec2(
        content_center.x - value_size.x * 0.5f,
        content_center.y - total_text_height * 0.5f
    );

    // 标题位置（数值下方）
    ImVec2 title_pos = ImVec2(
        content_center.x - title_size.x * 0.5f,
        value_pos.y + value_size.y + text_spacing
    );

    // 数值使用亮色
    ImU32 value_color = (*is_active || hovered || held) ? IM_COL32(255, 255, 255, 255) : IM_COL32(235, 235, 240, 255);
    window->DrawList->AddText(value_pos, value_color, value_buf);

    // 标题使用副文本颜色
    window->DrawList->AddText(title_pos, col_sub_text, title);

    return pressed;
}

// ============================================================================
// BeginGlassCard / EndGlassCard（原版 MainDefinition.h 第857-887行）
// ============================================================================
bool BeginGlassCard(const char* label, const ImVec2& size, bool border, ImGuiWindowFlags flags) {
    ImGuiStyle& style = ImGui::GetStyle();
    static ImVec4 saved_child_bg;
    static ImVec4 saved_border;
    static float saved_rounding;
    static float saved_border_size;
    static ImVec2 saved_padding;
    saved_child_bg = style.Colors[ImGuiCol_ChildBg];
    saved_border = style.Colors[ImGuiCol_Border];
    saved_rounding = style.ChildRounding;
    saved_border_size = style.ChildBorderSize;
    saved_padding = style.WindowPadding;
    style.Colors[ImGuiCol_ChildBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.5f);
    style.Colors[ImGuiCol_Border] = ImVec4(1.0f, 1.0f, 1.0f, 0.00f);
    style.ChildRounding = 40.0f;
    style.ChildBorderSize = 1.5f;
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    bool ret = ImGui::BeginChild(label, size, border, flags);
    if (ret) {
    }
    style.Colors[ImGuiCol_ChildBg] = saved_child_bg;
    style.Colors[ImGuiCol_Border] = saved_border;
    style.ChildRounding = saved_rounding;
    style.ChildBorderSize = saved_border_size;
    style.WindowPadding = saved_padding;
    return ret;
}

void EndGlassCard() {
    ImGui::EndChild();
}

// ============================================================================
// ButtonWithIcon（原版 imgui_widgets.cpp 第829-900行）
// ============================================================================
bool ButtonWithIcon(const char* label, const char* subtitle, ImTextureID icon, const ImVec2& size_arg, ImGuiButtonFlags flags)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    const ImVec2 subtitle_size = ImGui::CalcTextSize(subtitle, NULL, true);

    ImVec2 pos = window->DC.CursorPos;
    if ((flags & ImGuiButtonFlags_AlignTextBaseLine) && style.FramePadding.y < window->DC.CurrLineTextBaseOffset)
        pos.y += window->DC.CurrLineTextBaseOffset - style.FramePadding.y;

    // 计算所需尺寸
    float icon_size = 70.0f;           // 图标大小
    float left_margin = 10.0f;         // 图标左边距
    float icon_text_spacing = 10.0f;   // 图标与文本间距
    float text_width = ImMax(label_size.x, subtitle_size.x);
    float text_height = label_size.y + subtitle_size.y + 2.0f;  // 两行文本总高度

    ImVec2 size = ImGui::CalcItemSize(size_arg,
        left_margin + icon_size + icon_text_spacing + text_width + style.FramePadding.x * 2.0f,
        ImMax(icon_size, text_height) + style.FramePadding.y * 2.0f);

    const ImRect bb(pos, pos + size);
    ImGui::ItemSize(size, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id))
        return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, flags);

    ImDrawList* draw_list = window->DrawList;

    // 计算图标位置（垂直居中）
    float icon_x = bb.Min.x + left_margin;
    float icon_y = bb.Min.y + (size.y - icon_size) * 0.5f;
    ImVec2 icon_min(icon_x, icon_y);
    ImVec2 icon_max(icon_x + icon_size, icon_y + icon_size);

    // 渲染图标
    ImU32 icon_tint = hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(220, 220, 220, 255);
    draw_list->AddImage(icon, icon_min, icon_max, ImVec2(0, 0), ImVec2(1, 1), icon_tint);

    // 计算文本位置
    float text_start_x = icon_x + icon_size + icon_text_spacing;
    float total_text_height = label_size.y + subtitle_size.y + 2.0f;
    float text_start_y = bb.Min.y + (size.y - total_text_height) * 0.5f;

    // 渲染主文本（第一行）
    ImVec2 label_pos(text_start_x, text_start_y);
    ImU32 label_color = ImGui::GetColorU32(ImGuiCol_Text);
    if (hovered)
        label_color = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
    draw_list->AddText(label_pos, label_color, label);

    // 渲染副文本（第二行，灰色）
    ImVec2 subtitle_pos(text_start_x, text_start_y + label_size.y + 2.0f);
    ImU32 subtitle_color = IM_COL32(128, 128, 128, 255);  // 灰色
    if (hovered)
        subtitle_color = IM_COL32(160, 160, 160, 255);    // 悬停时稍亮
    draw_list->AddText(subtitle_pos, subtitle_color, subtitle);

    if (g.LogEnabled)
        ImGui::LogSetNextTextDecoration("[", "]");

    return pressed;
}

// ============================================================================
// ButtonTab（原版 imgui_widgets.cpp 第1855-1970行）
// ============================================================================
bool ButtonTab(const char* label, const ImVec2& size_arg, ImGuiButtonFlags flags)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);

    ImVec2 pos = window->DC.CursorPos;
    if ((flags & ImGuiButtonFlags_AlignTextBaseLine) && style.FramePadding.y < window->DC.CurrLineTextBaseOffset)
        pos.y += window->DC.CurrLineTextBaseOffset - style.FramePadding.y;
    ImVec2 size = ImGui::CalcItemSize(size_arg, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);

    const ImRect bb(pos, pos + size);
    ImGui::ItemSize(size, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id))
        return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, flags);

    static ImGuiID current_selected_id = 0;
    static ImVec2 target_rect_pos = ImVec2(0, 0);
    static ImVec2 current_rect_pos = ImVec2(0, 0);
    static ImVec2 target_rect_size = ImVec2(0, 0);
    static ImVec2 current_rect_size = ImVec2(0, 0);
    static float anim_alpha = 0.4f;
    static bool first_frame = true;
    static bool background_drawn = false;

    bool selected = (current_selected_id == id);

    // 默认选中主页
    static bool home_initialized = false;
    static ImGuiID home_id = 0;

    if (id == window->GetID(ICON_FA_HOUSE""))
    {
        home_id = id;
        if (!home_initialized)
        {
            current_selected_id = id;
            home_initialized = true;
        }
    }

    if (first_frame)
    {
        target_rect_pos = pos;
        current_rect_pos = target_rect_pos;
        target_rect_size = size;
        current_rect_size = target_rect_size;
        first_frame = false;
        anim_alpha = 1.0f;
    }

    if (selected)
    {
        target_rect_pos = pos;
        target_rect_size = size;
    }

    float move_t = 0.08f;
    current_rect_pos.x = current_rect_pos.x + (target_rect_pos.x - current_rect_pos.x) * move_t;
    current_rect_pos.y = current_rect_pos.y + (target_rect_pos.y - current_rect_pos.y) * move_t;
    current_rect_size.x = current_rect_size.x + (target_rect_size.x - current_rect_size.x) * move_t;
    current_rect_size.y = current_rect_size.y + (target_rect_size.y - current_rect_size.y) * move_t;

    float target_alpha = selected ? 1.0f : 1.0f;
    if (anim_alpha < target_alpha)
        anim_alpha = ImMin(anim_alpha + g.IO.DeltaTime * 4.0f, target_alpha);
    else if (anim_alpha > target_alpha)
        anim_alpha = ImMax(anim_alpha - g.IO.DeltaTime * 4.0f, target_alpha);

    // 绘制 Mac 蓝色毛玻璃背景矩形
    if (!background_drawn)
    {
        float radius = current_rect_size.y * 0.5f;
        ImColor bg_color = ImColor(0.0f, 122/255.0f, 1.0f, selected ? 0.75f : anim_alpha * 0.5f);
        window->DrawList->AddRectFilled(current_rect_pos, current_rect_pos + current_rect_size, bg_color, radius, ImDrawFlags_RoundCornersRight);
        background_drawn = true;
    }

    // 绘制文本（包含图标）- 水平居中
    ImVec4 text_color = selected ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(89/255.0f, 89/255.0f, 89/255.0f, 1.0f);

    // 水平居中计算
    float text_x = pos.x + (size.x - label_size.x) * 0.5f;
    float text_y = pos.y + (size.y - label_size.y) * 0.5f;
    ImVec2 text_pos = ImVec2(text_x, text_y);

    ImGui::PushStyleColor(ImGuiCol_Text, text_color);
    ImGui::RenderText(text_pos, label);
    ImGui::PopStyleColor();

    // 重置背景绘制标志
    if (id == home_id || id == window->GetID(ICON_FA_GEAR""))
        background_drawn = false;

    if (pressed)
    {
        current_selected_id = id;
        target_rect_pos = pos;
        target_rect_size = size;
        anim_alpha = 1.0f;
    }

    return pressed;
}

// ============================================================================
// HorizontalToggleBar（原版 imgui_widgets.cpp 第3013-3208行）
// ============================================================================
bool HorizontalToggleBar(const char* label, int* current_item, const char* const items[], int items_count)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

    ImGui::PushID(label);
    const ImGuiID id = window->GetID(label);

    // 尺寸参数
    const float bar_height = 60.0f;
    const float toggle_height = 40.0f;  // 高度距顶部10，底部10 = 60 - 10 - 10 = 40
    const float rounding = toggle_height * 0.5f;  // 纯圆角（胶囊形）
    const float left_margin = 10.0f;
    const float right_margin = 10.0f;
    const float toggle_area_width = 300.0f;

    // 文本尺寸
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);

    // 宽度-1自动填满父容器
    ImVec2 size_arg(-1.0f, bar_height);
    ImVec2 actual_size = ImGui::CalcItemSize(size_arg, label_size.x + toggle_area_width + left_margin + right_margin, bar_height);

    const float total_width = actual_size.x;
    const float total_height = actual_size.y;

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect total_bb(pos, pos + ImVec2(total_width, total_height));

    ImGui::ItemSize(total_bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(total_bb, id))
    {
        ImGui::PopID();
        return false;
    }

    // 文本区域（左侧，距离左边框10）
    ImRect text_bb(
        ImVec2(pos.x + left_margin, pos.y),
        ImVec2(pos.x + left_margin + label_size.x, pos.y + total_height)
    );

    // 切换条区域（右侧，距离右边框10，距顶部和底部各10）
    ImRect toggle_bb(
        ImVec2(pos.x + total_width - right_margin - toggle_area_width,
               pos.y + 10.0f),  // 距顶部10
        ImVec2(pos.x + total_width - right_margin,
               pos.y + total_height - 10.0f)  // 距底部10
    );

    // 轨道总有效宽度，等分每个选项
    const float toggle_total_width = toggle_bb.Max.x - toggle_bb.Min.x;
    const float item_width = toggle_total_width / items_count;

    // 交互检测
    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(total_bb, id, &hovered, &held);

    // 处理点击
    bool value_changed = false;
    if (pressed && hovered)
    {
        ImVec2 local_mouse_pos = ImGui::GetMousePos();
        float toggle_left = toggle_bb.Min.x;

        for (int i = 0; i < items_count; i++)
        {
            ImRect item_bb(
                ImVec2(toggle_left + item_width * i, toggle_bb.Min.y),
                ImVec2(toggle_left + item_width * (i + 1), toggle_bb.Max.y)
            );

            if (item_bb.Contains(local_mouse_pos))
            {
                if (*current_item != i)
                {
                    *current_item = i;
                    value_changed = true;
                    ImGui::MarkItemEdited(id);
                }
                break;
            }
        }
    }

    // 持久化激活态
    ImGuiStorage* storage = window->DC.StateStorage;
    bool* is_active = storage->GetBoolRef(id, false);
    if (pressed) *is_active = !(*is_active);

    // 动画状态
    float* animation_pos = storage->GetFloatRef(id + items_count, 0.0f);
    const float target_pos = item_width * (*current_item);
    const float animation_speed = 0.06f;

    if (*animation_pos != target_pos)
    {
        float delta = target_pos - *animation_pos;
        *animation_pos += delta * animation_speed;
        if (fabs(*animation_pos - target_pos) < 0.5f)
            *animation_pos = target_pos;
    }

    // 动画位移与缩放
    float* anim_y = storage->GetFloatRef(id + 0x1000, 0.0f);
    float* anim_scale = storage->GetFloatRef(id + 0x2000, 1.0f);
    const float dt = g.IO.DeltaTime;
    const float speed = 8.0f;

    float target_y = (hovered && !held) ? -2.0f : 0.0f;
    *anim_y = ImLerp(*anim_y, target_y, dt * speed);

    float target_scale = (held && hovered) ? 0.98f : 1.0f;
    *anim_scale = ImLerp(*anim_scale, target_scale, dt * speed);

    ImVec2 center = total_bb.GetCenter();
    ImVec2 animated_size = total_bb.GetSize() * *anim_scale;
    ImRect animated_bb(
        ImVec2(center.x - animated_size.x * 0.5f, center.y - animated_size.y * 0.5f + *anim_y),
        ImVec2(center.x + animated_size.x * 0.5f, center.y + animated_size.y * 0.5f + *anim_y)
    );

    // 重新计算动画后的区域
    ImRect animated_text_bb(
        ImVec2(animated_bb.Min.x + left_margin, animated_bb.Min.y),
        ImVec2(animated_bb.Min.x + left_margin + label_size.x, animated_bb.Max.y)
    );

    ImRect animated_toggle_bb(
        ImVec2(animated_bb.Max.x - right_margin - toggle_area_width,
               animated_bb.Min.y + 10.0f),  // 距顶部10
        ImVec2(animated_bb.Max.x - right_margin,
               animated_bb.Max.y - 10.0f)  // 距底部10
    );
    const float animated_toggle_width = animated_toggle_bb.GetWidth();
    const float animated_item_width = animated_toggle_width / items_count;

    // 配色规则
    ImU32 col_text;
    if (held && hovered) {
        col_text = IM_COL32(255, 255, 255, 255);
    } else if (*is_active) {
        col_text = IM_COL32(255, 255, 255, 255);
    } else if (hovered) {
        col_text = IM_COL32(22, 119, 255, 255);
    } else {
        col_text = IM_COL32(102, 102, 102, 255);
    }

    // 切换条颜色
    ImU32 col_toggle_border = IM_COL32(200, 200, 200, 255);
    ImU32 col_toggle_active = IM_COL32(22, 119, 255, 255);

    // 绘制标签文本（左侧，垂直居中）
    ImVec2 animated_text_pos(
        animated_text_bb.Min.x,
        animated_text_bb.Min.y + (animated_text_bb.GetHeight() - label_size.y) * 0.5f
    );
    window->DrawList->AddText(animated_text_pos, col_text, label);

    // 绘制切换条边框（只有边框，无背景，纯圆角）
    window->DrawList->AddRect(animated_toggle_bb.Min, animated_toggle_bb.Max, col_toggle_border, rounding, 0, 1.5f);

    // 绘制活跃滑块（纯圆角）
    const ImRect slider_bb(
        ImVec2(animated_toggle_bb.Min.x + *animation_pos, animated_toggle_bb.Min.y),
        ImVec2(animated_toggle_bb.Min.x + *animation_pos + animated_item_width, animated_toggle_bb.Max.y)
    );
    window->DrawList->AddRectFilled(slider_bb.Min, slider_bb.Max, col_toggle_active, rounding);

    // 绘制所有选项文本
    for (int i = 0; i < items_count; i++)
    {
        const char* item_text = items[i];
        ImVec2 text_size = ImGui::CalcTextSize(item_text, NULL, true);

        float item_center_x = animated_toggle_bb.Min.x + animated_item_width * i + animated_item_width * 0.5f;
        ImVec2 item_text_pos(
            item_center_x - text_size.x * 0.5f,
            animated_toggle_bb.Min.y + (animated_toggle_bb.GetHeight() - text_size.y) * 0.5f
        );

        bool is_over_slider = (item_center_x >= slider_bb.Min.x && item_center_x <= slider_bb.Max.x);
        ImU32 item_color = is_over_slider ? IM_COL32(255, 255, 255, 255) : IM_COL32(102, 102, 102, 255);

        window->DrawList->AddText(item_text_pos, item_color, item_text);
    }

    ImGui::RenderNavHighlight(total_bb, id);
    ImGui::PopID();
    return value_changed;
}

// ============================================================================
// render_dynamic_island - 灵动岛（窗口上方胶囊，点击切换窗口显示/隐藏）
// ============================================================================
// 灵动岛状态（文件内静态，供 render 和 bounds 查询共用）
static bool g_island_expanded = false;       // 展开状态（窗口隐藏时展开）
static float g_island_expand_t = 0.0f;       // 展开/收起动画进度 0~1
static float g_island_cw = 240.0f;           // 当前宽度
static float g_island_ch = 52.0f;            // 当前高度
static float g_island_x = 0.0f;              // 当前左上角 x
static float g_island_y = 0.0f;              // 当前左上角 y
static bool g_island_first_render = false;   // 是否已渲染过至少一帧

bool get_dynamic_island_bounds(float outBounds[4]) {
    // 如果还没渲染过，返回全屏区域（确保第一帧前触摸也能传进来）
    if (!g_island_first_render) {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        outBounds[0] = vp ? vp->Pos.x : 0;
        outBounds[1] = vp ? vp->Pos.y : 0;
        outBounds[2] = vp ? (vp->Pos.x + vp->Size.x) : 9999;
        outBounds[3] = vp ? (vp->Pos.y + 200) : 200;  // 屏幕上方 200px 区域
        return true;
    }
    // 正常返回灵动岛边界，并扩大 30px 容错范围（手指触摸精度问题）
    float tol = 30.0f;
    outBounds[0] = g_island_x - tol;
    outBounds[1] = g_island_y - tol;
    outBounds[2] = g_island_x + g_island_cw + tol;
    outBounds[3] = g_island_y + g_island_ch + tol;
    return true;
}

void render_dynamic_island() {
    // 局部动画状态（仅渲染用）
    static float hover_glow = 0.0f;        // 悬停光晕强度
    static float press_scale = 1.0f;       // 按压缩放
    static float dot_pulse = 0.0f;         // 收起态圆点呼吸
    // 跨帧点击状态机（模拟 ImGui Button 行为，不依赖 IsMouseClicked 单帧检测）
    static bool island_pressed = false;    // 鼠标按下时是否在灵动岛上

    ImGuiIO& io = ImGui::GetIO();
    const float dt = io.DeltaTime;

    // 展开/收起动画（丝滑 lerp）
    float target_t = g_island_expanded ? 1.0f : 0.0f;
    g_island_expand_t += (target_t - g_island_expand_t) * ImMin(dt * 10.0f, 1.0f);

    // 尺寸参数（调大）
    const float collapsed_w = 240.0f, collapsed_h = 52.0f;
    const float expanded_w = 360.0f, expanded_h = 84.0f;
    g_island_cw = collapsed_w + (expanded_w - collapsed_w) * g_island_expand_t;
    g_island_ch = collapsed_h + (expanded_h - collapsed_h) * g_island_expand_t;
    const float rounding = g_island_ch * 0.5f;

    // 定位：屏幕顶部居中，紧贴窗口上方（间距加大到 24px，更往上）
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    g_island_x = (viewport->Pos.x + viewport->Size.x * 0.5f) - (g_island_cw * 0.5f);
    g_island_y = (viewport->Pos.y + viewport->Size.y * 0.5f) - 400.0f - g_island_ch - 24.0f;

    // 灵动岛区域
    ImVec2 island_pos(g_island_x, g_island_y);
    ImVec2 island_size(g_island_cw, g_island_ch);
    ImRect island_bb(island_pos, island_pos + island_size);

    // === 手动 hit-test（不依赖 InvisibleButton 和窗口系统）===
    ImVec2 mouse = io.MousePos;
    bool hovered = (mouse.x >= island_bb.Min.x && mouse.x <= island_bb.Max.x &&
                    mouse.y >= island_bb.Min.y && mouse.y <= island_bb.Max.y);

    // === 跨帧点击状态机（模拟 ImGui ButtonBehavior）===
    bool mouse_down = io.MouseDown[0];

    // 检测按下：鼠标按下且在灵动岛上且之前未按下
    if (mouse_down && hovered && !island_pressed) {
        island_pressed = true;
    }
    // 检测释放：鼠标释放且之前按下了
    if (!mouse_down && island_pressed) {
        island_pressed = false;
        // 释放时仍在灵动岛上 → 触发点击
        if (hovered) {
            g_island_expanded = !g_island_expanded;
            MainAuraOne = !g_island_expanded;  // 展开时隐藏窗口，收起时显示窗口
        }
    }

    // 悬停光晕动画
    float target_glow = hovered ? 1.0f : 0.0f;
    hover_glow += (target_glow - hover_glow) * ImMin(dt * 12.0f, 1.0f);

    // 按压缩放动画
    float target_scale = (island_pressed && hovered) ? 0.93f : 1.0f;
    press_scale += (target_scale - press_scale) * ImMin(dt * 18.0f, 1.0f);

    // 圆点呼吸
    dot_pulse += dt * 3.0f;

    // === 绘制（用 ForegroundDrawList，确保在最上层，不需要窗口）===
    ImDrawList* draw_list = ImGui::GetForegroundDrawList();

    // 计算缩放后的矩形（以中心缩放）
    ImVec2 center = island_bb.GetCenter();
    ImVec2 scaled_half = ImVec2(g_island_cw * press_scale * 0.5f, g_island_ch * press_scale * 0.5f);
    ImRect draw_bb(center - scaled_half, center + scaled_half);

    // 悬停蓝色光晕
    if (hover_glow > 0.01f) {
        for (int i = 3; i >= 1; i--) {
            float glow_r = i * 7.0f * hover_glow;
            int alpha = (int)(14.0f * hover_glow * (4 - i));
            draw_list->AddRect(
                ImVec2(draw_bb.Min.x - glow_r, draw_bb.Min.y - glow_r),
                ImVec2(draw_bb.Max.x + glow_r, draw_bb.Max.y + glow_r),
                IM_COL32(0, 122, 255, alpha),
                rounding + glow_r, 0, 1.5f
            );
        }
    }

    // 主体黑色背景
    draw_list->AddRectFilled(draw_bb.Min, draw_bb.Max, IM_COL32(0, 0, 0, 245), rounding);

    // 边框微光
    ImU32 border_col = IM_COL32(60, 60, 60, 80);
    if (hover_glow > 0.01f) {
        int a = (int)(80 + 120 * hover_glow);
        border_col = IM_COL32(0, 122, 255, a);
    }
    draw_list->AddRect(draw_bb.Min, draw_bb.Max, border_col, rounding, 0, 1.0f);

    // 内容绘制（丝滑动画，无文字提示）
    // 收起态：两个呼吸圆点
    // 展开态：圆点拉长为胶囊条（无文字，纯动画）
    float dot_alpha = 1.0f;
    float dot_r = 6.0f;
    float pulse = 0.8f + 0.2f * sinf(dot_pulse);
    int dot_a = (int)(255 * dot_alpha * pulse);
    float gap = 20.0f;
    ImVec2 dot_center = center;

    // 收起态（expand_t < 0.5）：两个小圆点
    // 展开态（expand_t >= 0.5）：圆点逐渐拉长融合为一条胶囊
    float expand_blend = g_island_expand_t; // 0~1
    if (expand_blend < 0.5f) {
        // 两个圆点
        draw_list->AddCircleFilled(
            ImVec2(dot_center.x - gap * 0.5f, dot_center.y),
            dot_r, IM_COL32(255, 255, 255, dot_a), 16
        );
        draw_list->AddCircleFilled(
            ImVec2(dot_center.x + gap * 0.5f, dot_center.y),
            dot_r, IM_COL32(255, 255, 255, dot_a), 16
        );
    } else {
        // 圆点融合为胶囊条（宽度随展开度增加）
        float bar_blend = (expand_blend - 0.5f) / 0.5f; // 0~1
        float bar_half_w = gap * 0.5f + bar_blend * 40.0f; // 拉长
        float bar_h = dot_r * 2.0f * (1.0f - bar_blend * 0.3f); // 略微变细
        // 圆角胶囊
        draw_list->AddRectFilled(
            ImVec2(dot_center.x - bar_half_w, dot_center.y - bar_h * 0.5f),
            ImVec2(dot_center.x + bar_half_w, dot_center.y + bar_h * 0.5f),
            IM_COL32(255, 255, 255, dot_a),
            bar_h * 0.5f, ImDrawFlags_RoundCornersAll
        );
    }

    g_island_first_render = true;
}

// ============================================================================
// DrawSpheres - 绘制球体
// 使用 ImGui BackgroundDrawList 在游戏画面上层绘制所有球体
// 数据来源: UpdateSpheresThread() 通过 IL2CPP API 读取 GameCoreCenter.BallDic
// 坐标转换: 用我方球体世界坐标作为屏幕中心，其他球体按相对偏移和比例转换
// ============================================================================

static void DrawSpheres() {
    if (!g_draw_sphere_enabled) return;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* bg = ImGui::GetBackgroundDrawList();

    // 屏幕中心 = 我方球体位置（相机跟随我方球体）
    float startX = io.DisplaySize.x * 0.5f;
    float startY = io.DisplaySize.y * 0.5f;

    // 如果初始化失败，显示错误提示（优先级最高）
    if (g_sphere_search_failed && g_sphere_count == 0) {
        bg->AddText(ImVec2(startX - 140, startY - 20),
            IM_COL32(255, 0, 0, 255),
            "\xe5\x88\x9d\xe5\xa7\x8b\xe5\x8c\x96\xe5\xa4\xb1\xe8\xb4\xa5\xef\xbc\x8c\xe8\xaf\xb7\xe7\xa1\xae\xe8\xae\xa4\xe6\xb8\xb8\xe6\x88\x8f\xe5\xb7\xb2\xe5\xbc\x80\xe5\xa7\x8b"); // 初始化失败，请确认游戏已开始
        return;
    }

    // 如果正在初始化 IL2CPP，或者已启用但还没数据，显示提示
    // 关键修复：线程启动有延迟，searching 标志可能还没被设置，
    // 所以只要 enabled 且 count==0 且没失败，就显示"正在初始化"
    if (g_sphere_searching || g_sphere_count == 0) {
        bg->AddText(ImVec2(startX - 100, startY - 20),
            IM_COL32(255, 255, 0, 255),
            "\xe6\xad\xa3\xe5\x9c\xa8\xe5\x88\x9d\xe5\xa7\x8b\xe5\x8c\x96 IL2CPP..."); // 正在初始化 IL2CPP...
        return;
    }

    // 坐标转换：用我方球体世界坐标作为屏幕中心
    // 其他球体相对我方球体的世界坐标差，乘以比例系数得到屏幕偏移
    // 比例系数 g_sphere_scale 可调节（用户通过滑块调整）
    // 球球大作战中，世界坐标单位约等于像素/23.25，这里用 g_sphere_scale 调节
    float world_to_screen = 23.25f * g_sphere_scale;

    // 我方球体世界坐标（屏幕中心）
    float myX = g_my_sphere_world_x;
    float myY = g_my_sphere_world_y;

    // 颜色
    ImU32 sphere_color = IM_COL32(255, 255, 255, 255);  // 白色
    ImU32 my_color = IM_COL32(0, 255, 0, 255);          // 绿色（我方）

    // 遍历所有球体绘制
    int count = g_sphere_count;
    if (count > MAX_SPHERES) count = MAX_SPHERES;

    for (int i = 0; i < count; i++) {
        const SphereEntity& e = g_sphere_list[i];

        // 世界坐标 -> 屏幕坐标
        // 相对我方球体的偏移 * 比例 + 屏幕中心
        float screenX = ((e.x - myX) * world_to_screen) + startX;
        float screenY = ((e.y - myY) * world_to_screen) + startY;
        float drawRadius = e.radius * world_to_screen;
        if (drawRadius < 3.0f) drawRadius = 3.0f;
        if (drawRadius > 500.0f) drawRadius = 500.0f;

        // 判断是否是我方球体（坐标接近中心）
        bool isMe = (fabs(e.x - myX) < 0.01f && fabs(e.y - myY) < 0.01f);

        // 绘制连线（从我方球体到该球体）
        if (g_draw_sphere_line && !isMe) {
            bg->AddLine(
                ImVec2(startX, startY),
                ImVec2(screenX, screenY),
                sphere_color, 2.0f
            );
        }

        // 绘制球体圆圈
        if (g_draw_sphere_circle) {
            ImU32 color = isMe ? my_color : sphere_color;
            bg->AddCircle(
                ImVec2(screenX, screenY),
                drawRadius,
                color, 48, 3.0f
            );
        }
    }

    // 中心点（我方球体位置 - 绿点）
    bg->AddCircleFilled(ImVec2(startX, startY), 5.0f, IM_COL32(0, 255, 0, 255), 16);
}

// ============================================================================
// render_window（原版 MainUI.h 第1-270行，严格逐行移植）
// ============================================================================
void render_window() {
    // 绘制球体（独立于菜单，始终绘制）
    DrawSpheres();

    // === 菜单展开动画（MainUI.h 第1-19行）===
    if (MainAuraOne) {
        if (!last_menu_state) {
            last_menu_state = true;
        }
        float delta = ImGui::GetIO().DeltaTime;
        menu_expand += (1.0f - menu_expand) * delta * 12.0f;
        menu_expand = fminf(1.0f, menu_expand);
    } else {
        if (last_menu_state) {
            last_menu_state = false;
        }
        float delta = ImGui::GetIO().DeltaTime;
        menu_expand += (0.0f - menu_expand) * delta * 15.0f;
        menu_expand = fmaxf(0.0f, menu_expand);
    }
    if (menu_expand < 0.01f && !MainAuraOne) {
        return;
    }
    float width_factor = menu_expand;
    float min_width = 2.0f;
    float current_width = min_width + (1200.0f - min_width) * width_factor;

    // === 标签切换动画（MainUI.h 第23-39行）===
    static int Tab_Main = 1;
    static int prev_Tab_Main = 1;
    static float tab_alpha = 1.0f;
    static bool tab_is_animating = false;
    static float 模糊强度 = 0.8;
    if (prev_Tab_Main != Tab_Main) {
        tab_is_animating = true;
        tab_alpha = 0.0f;
        prev_Tab_Main = Tab_Main;
    }
    if (tab_is_animating) {
        tab_alpha += ImGui::GetIO().DeltaTime * 2.0f;
        if (tab_alpha >= 1.0f) {
            tab_alpha = 1.0f;
            tab_is_animating = false;
        }
    }

    // === 窗口样式（MainUI.h 第40-53行）===
    // 窗口背景：黑色，透明度由 window_opacity 控制
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, window_opacity));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 40.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGuiWindowFlags mainWindowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoMove;
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 window_pos = ImVec2((viewport->Pos.x + viewport->Size.x * 0.5f) - (current_width * 0.5f),(viewport->Pos.y + viewport->Size.y * 0.5f) - 400.0f);
    ImVec2 window_size = ImVec2(current_width, 800.0f);
    ImGui::SetNextWindowPos(window_pos);
    ImGui::SetNextWindowSize(ImVec2(current_width, 800.0f));
    float content_alpha = fminf(1.0f, (width_factor - 0.2f) / 0.6f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, content_alpha);
    if (ImGui::Begin("MainAuraNexusUI", nullptr, mainWindowFlags)) {
        // 设置窗口边界供 Java 层触摸命中判断
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        g_MainWindowBounds[0] = win->Pos.x;
        g_MainWindowBounds[1] = win->Pos.y;
        g_MainWindowBounds[2] = win->Pos.x + win->Size.x;
        g_MainWindowBounds[3] = win->Pos.y + win->Size.y;

        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        // === 顶部卡片 BidirectionalCard1（MainUI.h 第56-78行）===
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("MainAuraNexusUI_BidirectionalCard1", ImVec2(-1, 100), false, ImGuiWindowFlags_NoScrollbar);
        ImVec2 child_pos = ImGui::GetWindowPos();
        ImVec2 child_size = ImGui::GetWindowSize();
        draw_list->AddRectFilled(child_pos, ImVec2(child_pos.x + child_size.x, child_pos.y + child_size.y), ImGui::ColorConvertFloat4ToU32(ImVec4(255/255.f, 255/255.f, 255/255.f, 0.5f)), 40.0f, ImDrawFlags_RoundCornersTop);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.00f, 0.00f, 0.00f, 0.00f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::SetCursorPosX(10.0f);ImGui::SetCursorPosY(10.0f);
        float CardWidth1 = ImGui::GetContentRegionAvail().x - 10.0f;
        float CardHeight1 = ImGui::GetContentRegionAvail().y - 10.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));
        if (ImGui::BeginChild("MainAuraNexusUI_Card1", ImVec2(CardWidth1, CardHeight1), false, ImGuiWindowFlags_NoScrollbar)) {
            if (ButtonWithIcon("\xe7\xbd\x91\xe6\x98\x93\xe4\xba\x91", "\xe8\xa3\xb8\xe5\xa5\x94\xe5\x8a\x9f\xe8\x83\xbd", LOGO, ImVec2(300, -1))) { }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        // === 左侧标签栏 BidirectionalCard2（MainUI.h 第79-103行）===
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("MainAuraNexusUI_BidirectionalCard2", ImVec2(200, -1), false, ImGuiWindowFlags_NoScrollbar);
        ImVec2 child_pos1 = ImGui::GetWindowPos();
        ImVec2 child_size1 = ImGui::GetWindowSize();
        draw_list->AddRectFilled(child_pos1, ImVec2(child_pos1.x + child_size1.x, child_pos1.y + child_size1.y), ImGui::ColorConvertFloat4ToU32(ImVec4(255/255.f, 255/255.f, 255/255.f, 0.5f)), 40.0f, ImDrawFlags_RoundCornersBottomLeft);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.00f, 0.00f, 0.00f, 0.00f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::SetCursorPosX(0.0f);ImGui::SetCursorPosY(15.0f);
        float CardWidth2 = ImGui::GetContentRegionAvail().x;
        float CardHeight2 = ImGui::GetContentRegionAvail().y - 10.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));
        if (ImGui::BeginChild("MainAuraNexusUI_Card2", ImVec2(CardWidth2, CardHeight2), false, ImGuiWindowFlags_NoScrollbar)) {
            if (ButtonTab(ICON_FA_HOUSE"", ImVec2(-1, 80))) { Tab_Main = 1; }
            if (ButtonTab(ICON_FA_CROSSHAIRS"", ImVec2(-1, 80))) { Tab_Main = 2; }
            if (ButtonTab(ICON_FA_GEAR"", ImVec2(-1, 80))) { Tab_Main = 3; }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        ImGui::SameLine();

        // === 右侧内容区 BidirectionalCard3（MainUI.h 第105-265行）===
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("MainAuraNexusUI_BidirectionalCard3", ImVec2(-1, -1), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImVec2 o = origin;
        ImVec2 a = ImVec2(origin.x + 0.5, origin.y + 40);
        ImVec2 b = ImVec2(origin.x + 40, origin.y);
        ImVec2 center = ImVec2(origin.x + 40, origin.y + 40);
        float radius = 40.0f;
        draw_list->PathClear();
        draw_list->PathLineTo(o);
        draw_list->PathLineTo(a);
        draw_list->PathArcTo(center, radius, IM_PI, IM_PI * 1.5f, 16);
        draw_list->PathFillConvex(IM_COL32(255, 255, 255, 128));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.00f, 0.00f, 0.00f, 0.00f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::SetCursorPosX(10.0f);ImGui::SetCursorPosY(15.0f);
        float CardWidth3 = ImGui::GetContentRegionAvail().x - 10.0f;
        float CardHeight3 = ImGui::GetContentRegionAvail().y - 10.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, tab_alpha);
        if (ImGui::BeginChild("MainAuraNexusUI_Card3", ImVec2(CardWidth3, CardHeight3), false, ImGuiWindowFlags_NoScrollbar)) {

            // ===== 标签页 1：主页（MainUI.h 第129-163行）=====
            if (Tab_Main == 1) {
                ImGui::Columns(2, nullptr, false);
                if (BeginGlassCard("HomeCard1", ImVec2(-1, -1), true, ImGuiWindowFlags_NoScrollbar)) {
                    DrawAuraSectionTitle("\xe6\xb8\xb8\xe6\x88\x8f\xe5\x88\x9d\xe5\xa7\x8b\xe5\x8c\x96"); // 游戏初始化
                    ImGui::Spacing();
                    if (ImGui::Button("\xe5\x90\xaf\xe5\x8a\xa8\xe6\xb8\xb8\xe6\x88\x8f\xe5\xb9\xb6\xe6\xb3\xa8\xe5\x85\xa5", ImVec2(-1, 70))) { // 启动游戏并注入
                        std::thread([](){ lb_init_game(); }).detach();
                    }
                    ImGui::Separator();
                    if (ImGui::Button("\xe6\x97\xa0\xe7\x97\x95\xe9\x80\x80\xe5\x87\xba\xe7\xa8\x8b\xe5\xba\x8f", ImVec2(-1, 50))) { // 无痕退出程序
                        std::thread([](){ lb_clean_exit(); }).detach();
                    }
                    ImGui::Separator();
                    ImGui::TextColored(lb_gjc_injected ? ImVec4(0.1f,0.7f,0.1f,1) : ImVec4(0.8f,0.3f,0,1),
                        "\xe8\xbf\x87\xe6\xa3\x80\xe6\xb5\x8b: %s", lb_gjc_status.c_str()); // 过检测
                    ImGui::Separator();
                    ImGui::Text("\xe6\xb3\xa8\xe5\x85\xa5\xe5\x8c\x85\xe5\x90\x8d: %s", get_injected_package_name().c_str()); // 注入包名
                    ImGui::Text("\xe5\x9f\xba\xe5\x9d\x80\xe7\x8a\xb6\xe6\x80\x81: %s", lb_base_libUE4 ? "\xe5\xb7\xb2\xe8\x8e\xb7\xe5\x8f\x96" : "\xe6\x9c\xaa\xe8\x8e\xb7\xe5\x8f\x96"); // 基址状态: 已获取/未获取
                }
                EndGlassCard();
                ImGui::NextColumn();
                if (BeginGlassCard("HomeCard2", ImVec2(-1, -1), true, ImGuiWindowFlags_NoScrollbar)) {
                    DrawAuraSectionTitle("\xe7\xb3\xbb\xe7\xbb\x9f\xe4\xbf\xa1\xe6\x81\xaf"); // 系统信息
                    ImGui::Spacing();
                    ImGui::Text("FPS: %d", GetRealFPS());
                    ImGui::Text("CPU: %d%%", GetRealCPU());
                    ImGui::Text("GPU: %d%%", GetRealGPU());
                    ImGui::Text("\xe5\x86\x85\xe5\xad\x98: %d%%", GetRealRAMPercent()); // 内存
                    ImGui::Separator();
                    const char* Antirecordingscreen[] = {"\xe5\x8f\xaf\xe5\xbd\x95\xe5\xb1\x8f", "\xe9\x98\xb2\xe5\xbd\x95\xe5\xb1\x8f"}; // 可录屏/防录屏
                    static int last_Prevent = -1;
                    if (HorizontalToggleBar("\xe9\x98\xb2\xe5\xbd\x95\xe5\xb1\x8f\xe8\xae\xbe\xe7\xbd\xae", &Prevent, Antirecordingscreen, 2)) { // 防录屏设置
                        // Prevent: 0=可录屏, 1=防录屏
                        bool secure = (Prevent == 1);
                        setSurfaceSecurity(secure, secure);
                    }
                    if (last_Prevent != Prevent) {
                        last_Prevent = Prevent;
                        bool secure = (Prevent == 1);
                        setSurfaceSecurity(secure, secure);
                    }
                }
                EndGlassCard();
                ImGui::Columns(1);
            }
            // ===== 标签页 2：裸奔功能（MainUI.h 第164-228行）=====
            else if (Tab_Main == 2) {
                ImGui::Columns(2, nullptr, false);
                if (BeginGlassCard("LB_Card1", ImVec2(-1, -1), true, ImGuiWindowFlags_NoScrollbar)) {
                    DrawAuraSectionTitle("\xe5\x8a\x9f\xe8\x83\xbd\xe5\xbc\x80\xe5\x85\xb3"); // 功能开关
                    ImGui::Spacing();
                    bool features_enabled = lb_game_ready;
                    ImGui::BeginDisabled(!features_enabled);
                    bool luna = lb_feature_luna;
                    if (ImGui::Checkbox("\xe9\x9c\xb2\xe5\xa8\x9c\xe5\x86\x85\xe9\x80\x8f", &luna)) lb_feature_luna = luna; // 露娜内透
                    ImGui::Separator();
                    bool head = lb_feature_head;
                    if (ImGui::Checkbox("\xe5\xa4\xb4\xe9\x83\xa8\xe8\x8c\x83\xe5\x9b\xb4", &head)) lb_feature_head = head; // 头部范围
                    ImGui::SameLine(); ImGui::SetNextItemWidth(120.0f);
                    float hv = lb_head_val; if (ImGui::SliderFloat("##head", &hv, 50, 150)) lb_head_val = hv;
                    ImGui::Separator();
                    bool body = lb_feature_body;
                    if (ImGui::Checkbox("\xe8\xba\xab\xe4\xbd\x93\xe8\x8c\x83\xe5\x9b\xb4", &body)) lb_feature_body = body; // 身体范围
                    ImGui::SameLine(); ImGui::SetNextItemWidth(120.0f);
                    float bv = lb_body_val; if (ImGui::SliderFloat("##body", &bv, 50, 150)) lb_body_val = bv;
                    ImGui::Separator();
                    static bool abd_enabled = false;
                    static float abd_val = 99.0f;
                    if (ImGui::Checkbox("\xe8\x85\xb9\xe9\x83\xa8\xe8\x8c\x83\xe5\x9b\xb4", &abd_enabled)) { // 腹部范围
                        if (abd_enabled) { if (!lb_freeze_running) { lb_abdominal_target=abd_val; lb_freeze_running=true; lb_freeze_thread=std::thread(lb_freeze_thread_func); lb_freeze_thread.detach(); } }
                        else lb_freeze_running = false;
                    }
                    ImGui::SameLine(); ImGui::SetNextItemWidth(120.0f);
                    if (ImGui::SliderFloat("##abd", &abd_val, 50, 150)) lb_abdominal_target = abd_val;
                    ImGui::Separator();
                    bool bullet = lb_feature_bullet;
                    if (ImGui::Checkbox("\xe5\xad\x90\xe5\xbc\xb9\xe6\x8d\xae\xe7\x82\xb9", &bullet)) { // 子弹据点
                        lb_feature_bullet = bullet;
                        if (bullet && lb_bullet_addr) { lb_writeDword(lb_bullet_addr, lb_bullet_val); lb_bullet_frozen=true; } else lb_bullet_frozen=false;
                    }
                    ImGui::SameLine();
                    ImGui::TextColored(lb_bullet_frozen ? ImVec4(0.1f,0.7f,0.1f,1) : ImVec4(0.8f,0.3f,0,1), lb_bullet_frozen ? "\xe5\xb7\xb2\xe5\xbc\x80\xe5\x90\xaf" : "\xe6\x9c\xaa\xe5\xbc\x80\xe5\x90\xaf"); // 已开启/未开启
                    ImGui::Separator();
                    bool norecoil = lb_feature_norecoil;
                    if (ImGui::Checkbox("\xe5\x85\xa8\xe5\xb1\x80\xe6\x97\xa0\xe5\x90\x8e", &norecoil)) { // 全局无后
                        lb_feature_norecoil = norecoil;
                        if (norecoil && lb_norecoil_addr) { lb_writeDword(lb_norecoil_addr, lb_norecoil_val); lb_norecoil_frozen=true; } else lb_norecoil_frozen=false;
                    }
                    ImGui::SameLine();
                    ImGui::TextColored(lb_norecoil_frozen ? ImVec4(0.1f,0.7f,0.1f,1) : ImVec4(0.8f,0.3f,0,1), lb_norecoil_frozen ? "\xe5\xb7\xb2\xe5\xbc\x80\xe5\x90\xaf" : "\xe6\x9c\xaa\xe5\xbc\x80\xe5\x90\xaf");
                    ImGui::Separator();
                    bool tp = lb_feature_thirdperson;
                    if (ImGui::Checkbox("\xe7\xac\xac\xe4\xb8\x89\xe4\xba\xba\xe7\xa7\xb0", &tp)) { // 第三人称
                        lb_feature_thirdperson = tp;
                        if (tp && lb_thirdperson_addr) { lb_writeDword(lb_thirdperson_addr, lb_thirdperson_val); lb_thirdperson_frozen=true; if(!lb_thirdperson_thread.joinable()){lb_thirdperson_thread=std::thread(lb_thirdperson_thread_func);lb_thirdperson_thread.detach();} } else lb_thirdperson_frozen=false;
                    }
                    ImGui::SameLine();
                    ImGui::TextColored(lb_thirdperson_frozen ? ImVec4(0.1f,0.7f,0.1f,1) : ImVec4(0.8f,0.3f,0,1), lb_thirdperson_frozen ? "\xe5\xb7\xb2\xe5\xbc\x80\xe5\x90\xaf" : "\xe6\x9c\xaa\xe5\xbc\x80\xe5\x90\xaf");
                    ImGui::Separator();
                    bool nt = lb_feature_neitou;
                    if (ImGui::Checkbox("\xe5\x85\xa8\xe5\xb1\x80\xe5\x86\x85\xe9\x80\x8f", &nt)) { // 全局内透
                        lb_feature_neitou = nt;
                        if (nt && !lb_neitou_running) { lb_neitou_world_ptr=lb_base_libUE4?lb_base_libUE4:0; lb_neitou_thread=std::thread(lb_neitou_thread_func); lb_neitou_thread.detach(); }
                    }
                    ImGui::SameLine();
                    ImGui::TextColored(lb_neitou_running ? ImVec4(0.1f,0.7f,0.1f,1) : ImVec4(0.8f,0.3f,0,1), "%s", lb_neitou_status.c_str());
                    ImGui::EndDisabled();
                }
                EndGlassCard();
                ImGui::Columns(1);
            }
            // ===== 标签页 3：设置（MainUI.h 第229-257行）=====
            else if (Tab_Main == 3) {
                ImGui::Columns(2, nullptr, false);
                if (BeginGlassCard("SetCard1", ImVec2(-1, -1), true, ImGuiWindowFlags_NoScrollbar)) {
                    DrawAuraSectionTitle("\xe6\x98\xbe\xe7\xa4\xba\xe8\xae\xbe\xe7\xbd\xae"); // 显示设置
                    ImGui::Spacing();
                    ImGui::SliderFloat("\xe5\xb8\xa7\xe7\x8e\x87", &FPSControlSize, 0.0f, 165.0f, "%.0f"); // 帧率
                    ImGui::Separator();
                    ImGui::SliderFloat("\xe7\xaa\x97\xe5\x8f\xa3\xe9\x80\x8f\xe6\x98\x8e\xe5\xba\xa6", &window_opacity, 0.0f, 1.0f, "%.2f"); // 窗口透明度
                    ImGui::Separator();
                    ImGui::SliderFloat("\xe6\xa8\xa1\xe7\xb3\x8a\xe5\xbc\xba\xe5\xba\xa6", &模糊强度, 0, 1, "%.1f"); // 模糊强度
                    ImGui::Separator();
                    const char* background_modes[] = {"\xe6\x9c\x89\xe5\x90\x8e\xe5\x8f\xb0", "\xe6\x97\xa0\xe5\x90\x8e\xe5\x8f\xb0"}; // 有后台/无后台
                    if (HorizontalToggleBar("\xe6\x97\xa0\xe5\x90\x8e\xe5\x8f\xb0\xe8\xae\xbe\xe7\xbd\xae", &background_mode, background_modes, 2)) { } // 无后台设置
                }
                EndGlassCard();
                ImGui::NextColumn();
                if (BeginGlassCard("SetCard2", ImVec2(-1, -1), true, ImGuiWindowFlags_NoScrollbar)) {
                    DrawAuraSectionTitle("\xe7\xbb\x98\xe5\x9b\xbe\xe8\xae\xbe\xe7\xbd\xae"); // 绘图设置
                    ImGui::Spacing();
                    ImGui::SliderFloat("\xe4\xba\xba\xe7\x89\xa9\xe7\xbb\x98\xe5\x9b\xbe\xe5\xa4\xa7\xe5\xb0\x8f", &ImGuiDrawESP, 0.3, 1.5, "%.1f"); // 人物绘图大小
                    ImGui::Separator();
                    ImGui::SliderFloat("\xe4\xb8\x96\xe7\x95\x8c\xe7\xbb\x98\xe5\x9b\xbe\xe5\xa4\xa7\xe5\xb0\x8f", &ImGuiDrawESP2, 0.3, 1.5, "%.1f"); // 世界绘图大小
                    ImGui::Separator();
                    if (ImGui::Button("\xe7\xbb\x98\xe5\x88\xb6\xe7\x90\x83\xe4\xbd\x93", ImVec2(-1, 50))) {
                        // 绘制球体：点击切换开关，启动/停止 IL2CPP 数据读取线程
                        g_draw_sphere_enabled = !g_draw_sphere_enabled;
                        if (g_draw_sphere_enabled) {
                            // 启动后台数据读取线程
                            if (g_sphere_thread.joinable()) g_sphere_thread.join();
                            g_il2cpp_sphere_inited = false;
                            // 关键修复：立即设置 searching=true，避免线程启动延迟期间
                            // DrawSpheres() 因 g_sphere_count==0 直接返回什么都不显示
                            g_sphere_searching = true;
                            g_sphere_search_failed = false;
                            g_sphere_count = 0;
                            g_sphere_thread = std::thread(UpdateSpheresThread);
                        }
                    } // 绘制球体
                    ImGui::Separator();
                    ImGui::SliderFloat("\xe7\x90\x83\xe4\xbd\x93\xe6\xaf\x94\xe4\xbe\x8b", &g_sphere_scale, 0.3f, 3.0f, "%.2f"); // 球体比例
                    ImGui::Separator();
                    if (ImGui::Button("\xe5\x8a\xa0\xe8\xbd\xbd\xe9\x85\x8d\xe7\xbd\xae", ImVec2(-1, 50))) { } // 加载配置
                }
                EndGlassCard();
                ImGui::Columns(1);
            }
            ImGui::PopStyleVar();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }
    ImGui::End();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

} // namespace aura_ui
