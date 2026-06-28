// =============================================================================
// ESP.h — ESP 透视绘制系统头文件
// 基于 libhook.so 反编译重建 (Rust/egui 版) 移植到 ImGui + IL2CPP C++ 模板
//
// 反编译来源 (decompiled-src/src/ui/):
//   draw_circle.rs   → ESPSystem::Render() + DrawObjectESP()
//   data_reader.rs   → ESPSystem::Tick() + ReadGameObjects()
//   game_loop.rs     → ESP_Tick() / ESP_Render() 调度
//   config.rs        → ESPConfig / OverlayConfig
//
// 逆向还原的游戏类/方法/字段 (二进制字符串证据见各 .rs 文件):
//   类名:   GameCoreCenter, PlayerBase, Camera, ResManager
//   方法:   get_instance, get_current, get_name, Rename, IsDisableGameSkin
//   字段:   name, x, y, z, radius, score, rankId, isAlive, color
//   列表:   players / playerList, fishList, cells
// =============================================================================

#pragma once

#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <cstring>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <cmath>
#include "imgui.h"

#define ESP_TAG "ESP"
#define ESP_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  ESP_TAG, __VA_ARGS__)
#define ESP_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, ESP_TAG, __VA_ARGS__)
#define ESP_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, ESP_TAG, __VA_ARGS__)
#define ESP_LOGW(...) __android_log_print(ANDROID_LOG_WARN,  ESP_TAG, __VA_ARGS__)

// =============================================================================
// IL2CPP 前向声明 (与 il2cpp/mod.rs 一致)
// =============================================================================

struct Il2CppObject;
struct Il2CppClass;
struct Il2CppArray;
struct Il2CppImage;
struct Il2CppAssembly;
struct Il2CppDomain;
struct Il2CppException;
struct Il2CppString;
struct FieldInfo;
struct MethodInfo;

// IL2CPP 函数指针类型 (对应 il2cpp/mod.rs 的 *_fn 类型别名)
typedef Il2CppDomain*        (*il2cpp_domain_get_t)();
typedef const Il2CppAssembly** (*il2cpp_domain_get_assemblies_t)(const Il2CppDomain*, size_t*);
typedef Il2CppImage*         (*il2cpp_assembly_get_image_t)(const Il2CppAssembly*);
typedef Il2CppClass*         (*il2cpp_class_from_name_t)(Il2CppImage*, const char*, const char*);
typedef FieldInfo*           (*il2cpp_class_get_fields_t)(Il2CppClass*, void**);
typedef const char*          (*il2cpp_field_get_name_t)(FieldInfo*);
typedef size_t               (*il2cpp_field_get_offset_t)(FieldInfo*);
typedef MethodInfo*          (*il2cpp_class_get_method_from_name_t)(Il2CppClass*, const char*, int);
typedef Il2CppObject*        (*il2cpp_runtime_invoke_t)(MethodInfo*, Il2CppObject*, void**, Il2CppException**);
typedef size_t               (*il2cpp_array_length_t)(Il2CppArray*);
typedef Il2CppObject*        (*il2cpp_array_get_t)(Il2CppArray*, size_t);
typedef Il2CppClass*         (*il2cpp_object_get_class_t)(Il2CppObject*);
typedef const char*          (*il2cpp_string_to_utf8_t)(Il2CppString*);
// 替代符号 (IL2CPP 标准导出, 用于 string_to_utf8 不存在时手动转 UTF8)
typedef const uint16_t*      (*il2cpp_string_chars_t)(const Il2CppString*);
typedef int                  (*il2cpp_string_length_t)(const Il2CppString*);
typedef Il2CppString*        (*il2cpp_string_new_t)(const char*);
typedef const char*          (*il2cpp_class_get_name_t)(Il2CppClass*);
typedef const char*          (*il2cpp_image_get_name_t)(Il2CppImage*);
typedef void*                (*il2cpp_thread_attach_t)(Il2CppDomain*);
typedef const Il2CppAssembly* (*il2cpp_domain_assembly_open_t)(const Il2CppDomain*, const char*);
typedef size_t               (*il2cpp_image_get_class_count_t)(const Il2CppImage*);
typedef Il2CppClass*         (*il2cpp_image_get_class_t)(const Il2CppImage*, size_t);

// =============================================================================
// 游戏对象信息 (对应 draw_circle.rs / data_reader.rs 的 GameObjectInfo)
// =============================================================================

struct GameObjectInfo {
    std::string name;       // 对象名称 (get_name 方法读取)
    float world_x;          // 世界坐标 X
    float world_y;          // 世界坐标 Y
    float world_z;          // 世界坐标 Z (2D 游戏通常为 0)
    float radius;           // 对象半径/大小
    int   object_id;        // 对象唯一 ID
    int   rank_id;          // 排名 ID (识别自己 vs 敌人)
    int   score;            // 分数
    bool  is_self;          // 是否是自己的角色
    ImU32 color;            // 对象颜色 (ImGui 格式)
    bool  is_alive;         // 是否存活

    GameObjectInfo() : world_x(0), world_y(0), world_z(0), radius(0),
        object_id(0), rank_id(-1), score(0), is_self(false),
        color(IM_COL32(255, 255, 255, 255)), is_alive(true) {}
};

// =============================================================================
// ESP 配置 (对应 config.rs 的 OverlayConfig + GameSettings)
// =============================================================================

struct ESPConfig {
    // 绘制开关 (逆向: "draw_enabled", "show_id")
    bool draw_enabled;          // 总开关
    bool show_circle;           // 显示圆圈
    bool show_name;             // 显示名称
    bool show_tracer;           // 显示距离线/追踪线
    bool show_id;               // 显示 ID
    bool show_score;            // 显示分数
    bool show_radius;           // 显示半径
    bool show_distance;         // 显示距离
    bool show_self_marker;      // 自身额外标记 (对应 draw_circle.rs 的 is_self 圈)

    // 颜色配置 (逆向: "draw_color", "name_color")
    ImU32 circle_color;         // 圆圈颜色
    ImU32 name_color;           // 名称颜色
    ImU32 tracer_color;         // 追踪线颜色
    ImU32 self_color;           // 自身颜色
    ImU32 enemy_color;          // 敌人颜色
    ImU32 dead_color;           // 死亡对象颜色

    // 圆圈参数
    float circle_thickness;     // 圆圈线宽
    int   circle_segments;      // 圆圈分段数

    // 名称参数
    float name_font_size;       // 名称字体大小
    float name_offset_y;        // 名称 Y 偏移

    // 追踪线参数
    float tracer_thickness;     // 追踪线线宽

    // 自身识别 (逆向: "self_rank_id")
    int self_rank_id;

    // 改名功能 (逆向: "rename_enabled", "name_prefix")
    bool rename_enabled;
    std::string name_prefix;

    // 圆圈大小微调 (用户可拖滑块, 让圆圈贴合球体)
    // 1.0 = 默认, >1 放大圆圈, <1 缩小圆圈
    float circle_scale;

    ESPConfig() :
        draw_enabled(false), show_circle(true), show_name(true),
        show_tracer(false), show_id(false), show_score(false),
        show_radius(false), show_distance(false), show_self_marker(true),
        circle_color(IM_COL32(0, 255, 0, 255)),
        name_color(IM_COL32(255, 255, 0, 255)),
        tracer_color(IM_COL32(255, 0, 0, 128)),
        self_color(IM_COL32(0, 255, 255, 255)),
        enemy_color(IM_COL32(255, 0, 0, 255)),
        dead_color(IM_COL32(128, 128, 128, 128)),
        circle_thickness(2.0f), circle_segments(36),
        name_font_size(20.0f), name_offset_y(25.0f),
        tracer_thickness(1.0f),
        self_rank_id(-1),
        rename_enabled(false), name_prefix("[BOT]"),
        circle_scale(1.0f) {}
};

// =============================================================================
// IL2CPP 字段偏移缓存 (对应 data_reader.rs 的 FieldOffsets)
// =============================================================================

struct FieldOffsets {
    // PlayerBase 字段偏移 (真实字段名见日志)
    // PB.Name @ 0x18, PB.ID @ 0x10, PB.Color @ 0x28, PB.isAlive @ 0x15c
    // PB._pos @ 0x1b0 (Vector3: x,y,z) — 但实测读出来是 0, 位置在 Transform 上
    // PB.radius @ 0x200, PB.BallList @ 0xb8 (玩家管理的球列表)
    size_t pb_name;
    size_t pb_x;
    size_t pb_y;
    size_t pb_z;
    size_t pb_radius;
    size_t pb_id;
    size_t pb_rank_id;
    size_t pb_score;
    size_t pb_is_alive;
    size_t pb_color;
    size_t pb_transform;  // Transform 引用字段偏移 (Unity 位置在 Transform 上!)

    // GameCoreCenter 字段偏移 (真实字段名见日志)
    // GCC.PlayerDic @ 0x58 — Dictionary<int, PlayerBase> 玩家字典
    // GCC.BallDic   @ 0x68 — Dictionary<int, Ball>      球字典
    // GCC.SelfPlayerID @ 0x160 — int 自身玩家 ID
    size_t gcc_player_dic;     // 玩家字典 (Dictionary)
    size_t gcc_ball_dic;       // 球字典 (Dictionary)
    size_t gcc_self_player_id; // 自身玩家 ID (int 字段)

    // 兼容旧字段 (保留以防其他版本游戏用 List 而非 Dictionary)
    size_t gcc_player_list;
    size_t gcc_fish_list;
    size_t gcc_cell_list;
    size_t gcc_self_player;

    bool initialized;

    FieldOffsets() : pb_name(0), pb_x(0), pb_y(0), pb_z(0),
        pb_radius(0), pb_id(0), pb_rank_id(0), pb_score(0),
        pb_is_alive(0), pb_color(0), pb_transform(0),
        gcc_player_dic(0), gcc_ball_dic(0), gcc_self_player_id(0),
        gcc_player_list(0), gcc_fish_list(0), gcc_cell_list(0),
        gcc_self_player(0), initialized(false) {}
};

// =============================================================================
// 相机信息 (用于世界坐标 → 屏幕坐标转换)
// =============================================================================

struct CameraInfo {
    float cam_x;       // 相机世界坐标 X
    float cam_y;       // 相机世界坐标 Y
    float zoom;        // 缩放/正交大小 (orthographicSize)
    float screen_w;    // 屏幕宽度
    float screen_h;    // 屏幕高度
    bool  valid;       // 是否有效

    CameraInfo() : cam_x(0), cam_y(0), zoom(30.0f),
        screen_w(0), screen_h(0), valid(false) {}
};

// =============================================================================
// ESP 系统 — 主类 (对应 draw_circle.rs DrawCircle + data_reader.rs DataReader)
// =============================================================================

class ESPSystem {
public:
    static ESPSystem& Instance();

    // 初始化 IL2CPP API (dlopen libil2cpp.so + 解析符号 + 找类 + 字段偏移)
    bool InitIL2CPP();

    // 每帧更新 (读取游戏数据) — 对应 data_reader.rs tick()
    void Tick();

    // 渲染 ESP (ImGui 绘制) — 对应 draw_circle.rs render()
    void Render();

    // 配置 (供 UI 面板直接读写)
    ESPConfig config;

    // 游戏是否就绪
    bool IsGameReady() const { return m_gameReady; }

    // 获取对象数量
    size_t GetObjectCount() const { return m_objects.size(); }

    // 诊断信息 (供 UI 显示, 帮助排查问题)
    std::string m_diagStatus;   // 初始化状态描述
    std::string m_diagOffsets;  // 字段偏移描述
    const std::string& GetDiagStatus() const { return m_diagStatus; }
    const std::string& GetDiagOffsets() const { return m_diagOffsets; }

    // 应用改名 (逆向: "rename_enabled", GameCoreCenter.Rename)
    void ApplyRename(const std::string& newName);

private:
    ESPSystem();

    // --- IL2CPP API ---
    void* m_il2cppHandle;
    uintptr_t m_il2cppBase;   // 从 /proc/self/maps 找到的 libil2cpp.so 基址
    bool  m_il2cppLoaded;

    // IL2CPP 函数指针
    il2cpp_domain_get_t              m_fn_domain_get;
    il2cpp_domain_get_assemblies_t   m_fn_domain_get_assemblies;
    il2cpp_assembly_get_image_t      m_fn_assembly_get_image;
    il2cpp_class_from_name_t         m_fn_class_from_name;
    il2cpp_class_get_fields_t        m_fn_class_get_fields;
    il2cpp_field_get_name_t          m_fn_field_get_name;
    il2cpp_field_get_offset_t        m_fn_field_get_offset;
    il2cpp_class_get_method_from_name_t m_fn_class_get_method_from_name;
    il2cpp_runtime_invoke_t          m_fn_runtime_invoke;
    il2cpp_array_length_t            m_fn_array_length;
    il2cpp_array_get_t               m_fn_array_get;
    il2cpp_object_get_class_t        m_fn_object_get_class;
    il2cpp_string_to_utf8_t          m_fn_string_to_utf8;
    il2cpp_string_chars_t            m_fn_string_chars;
    il2cpp_string_length_t           m_fn_string_length;
    il2cpp_string_new_t              m_fn_string_new;
    il2cpp_class_get_name_t          m_fn_class_get_name;
    il2cpp_image_get_name_t          m_fn_image_get_name;
    il2cpp_thread_attach_t           m_fn_thread_attach;
    il2cpp_domain_assembly_open_t    m_fn_domain_assembly_open;
    il2cpp_image_get_class_count_t   m_fn_image_get_class_count;
    il2cpp_image_get_class_t         m_fn_image_get_class;

    // 缓存的游戏类
    Il2CppClass* m_classGameCoreCenter;
    Il2CppClass* m_classPlayerBase;
    Il2CppClass* m_classBall;       // Ball 类 (BallDic 里的对象)
    Il2CppClass* m_classCamera;
    Il2CppImage* m_imageCSharp;
    Il2CppImage* m_imageUnityEngine; // UnityEngine.CoreModule (Camera 所在)
    Il2CppDomain* m_domain;

    // 缓存的方法
    MethodInfo* m_methodGetInstance;
    MethodInfo* m_methodGetCurrent;
    MethodInfo* m_methodGetName;

    // 字段偏移
    FieldOffsets m_offsets;         // PlayerBase 偏移
    FieldOffsets m_ballOffsets;     // Ball 偏移 (独立解析, 字段布局可能不同)

    // --- 游戏状态 ---
    std::atomic<bool> m_gameReady;
    std::atomic<bool> m_threadAttached;
    std::atomic<uint32_t> m_counter;
    std::atomic<uint32_t> m_pcounter;
    Il2CppObject* m_gameInstance;

    // 游戏对象列表
    std::mutex m_objectMutex;
    std::vector<GameObjectInfo> m_objects;
    uint32_t m_emptyFrameCount;  // 连续空列表帧计数 (防闪烁: 瞬时读取失败保留旧数据)

    // 相机信息
    CameraInfo m_camera;

    // --- 内部方法 ---
    bool ResolveIL2CPPFunctions();
    bool FindGameClasses();
    bool InitFieldOffsets();
    void ReadGameObjects();
    void ReadCameraInfo();
    std::string ReadObjectName(Il2CppObject* obj);
    bool WorldToScreen(float worldX, float worldY, float& screenX, float& screenY, const CameraInfo& cam);
    void DrawObjectESP(const GameObjectInfo& obj, const CameraInfo& cam);
    // 通过 Transform.get_position 读取对象世界坐标 (Unity 位置都在 Transform 上)
    // obj: 游戏对象, transformOffset: Transform 引用字段在该对象上的偏移
    // 返回 true 表示成功读到坐标
    bool ReadObjectPositionViaTransform(Il2CppObject* obj, size_t transformOffset,
                                         float& outX, float& outY, float& outZ);
    ImU32 GetObjectColor(const GameObjectInfo& obj);

    // Dictionary 读取辅助 — IL2CPP Dictionary<int, T> 内存布局:
    //   +0x18: _entries (Entry[] 数组指针)
    //   +0x20: _count   (int, 已分配 entry 数, 含 free)
    //   +0x28: _freeCount (int, 空闲 entry 数)
    // Entry<int, TRef> 布局 (24 字节):
    //   +0x00: hashCode (int, free entry 为 -1)
    //   +0x04: next     (int)
    //   +0x08: key      (int, 4 字节 + 4 字节 padding)
    //   +0x10: value    (引用, 8 字节)
    // 返回所有有效 value (hashCode >= 0 且 value 非空)
    void ReadDictionaryValues(void* dictObj, std::vector<Il2CppObject*>& outValues);
};

// =============================================================================
// 便捷调用 (供 imguijni.cpp 的 step() 调用, 对应 game_loop.rs 的调度)
// =============================================================================

bool ESP_Init();
void ESP_Tick();
void ESP_Render();
