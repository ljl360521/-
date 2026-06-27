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
#include <unordered_map>
#include <unordered_set>
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

    // 缩放微调 (用户可拖滑块微调, 让圆圈贴合球体)
    // 1.0 = 自动计算值, >1 放大, <1 缩小
    float zoom_scale;

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
        zoom_scale(1.0f) {}
};

// =============================================================================
// IL2CPP 字段偏移缓存 (对应 data_reader.rs 的 FieldOffsets)
// =============================================================================

struct FieldOffsets {
    // PlayerBase 字段偏移 (日志确认)
    size_t pb_name;         // Name @ 0x18
    size_t pb_id;           // ID @ 0x10
    size_t pb_pos;          // _pos @ 0x1b0 (Vector3, 值为0, 位置在Transform里)
    size_t pb_pos2;         // pos @ 0x218 (值为0)
    size_t pb_radius;       // radius @ 0x200 (值为0)
    size_t pb_is_alive;     // isAlive @ 0x15c
    size_t pb_score;        // AllBallScore @ 0x78
    size_t pb_color;        // Color @ 0x28
    size_t pb_team_id;      // TeamID @ 0x2c

    // GameCoreCenter 字段偏移 (日志确认)
    size_t gcc_player_dic;      // PlayerDic @ 0x58 (2D主)
    size_t gcc_cached_player_dic; // CachedPlayerDic @ 0x60
    size_t gcc_ball_dic;        // BallDic @ 0x68
    size_t gcc_ball_update_list; // BallUpdateList @ 0x70
    size_t gcc_person_list;     // PersonList @ 0x528 (3D)
    size_t gcc_person_dic;      // PersonDic @ 0x520 (3D)
    size_t gcc_self_player_id;  // SelfPlayerID @ 0x160
    size_t gcc_cur_player_id;   // curPlayerID @ 0x890
    size_t gcc_is_3d_room;      // Is3DRoomType @ 0x4f4
    size_t gcc_map3d;           // Map3D @ 0x4e8

    // List<T> 内部偏移
    size_t list_items;      // _items @ 0x10
    size_t list_size;       // _size @ 0x18

    // Dictionary 内部偏移
    size_t dic_entries;     // _entries @ 0x18
    size_t dic_count;       // _count @ 0x20
    size_t dic_entry_value; // Entry内value偏移 (默认16)

    // Ball (DrawCircle) 字段偏移 (日志确认)
    size_t ball_radius;     // Radius @ 0x028 (球半径, =4.54)
    size_t ball_self_tf;    // SelfTF @ 0xb8 (Transform引用)
    size_t ball_id;         // ID @ 0x10
    size_t ball_score;      // curScore @ 0x70
    size_t ball_pos;        // pos @ 0x170 (值为0, 位置在Transform里)

    bool initialized;

    FieldOffsets() : pb_name(0), pb_id(0), pb_pos(0), pb_pos2(0),
        pb_radius(0), pb_is_alive(0), pb_score(0), pb_color(0), pb_team_id(0),
        gcc_player_dic(0), gcc_cached_player_dic(0), gcc_ball_dic(0),
        gcc_ball_update_list(0), gcc_person_list(0), gcc_person_dic(0),
        gcc_self_player_id(0), gcc_cur_player_id(0),
        gcc_is_3d_room(0), gcc_map3d(0),
        list_items(0), list_size(0),
        dic_entries(0), dic_count(0), dic_entry_value(16),
        ball_radius(0), ball_self_tf(0), ball_id(0), ball_score(0), ball_pos(0),
        initialized(false) {}
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

    CameraInfo() : cam_x(0), cam_y(0), zoom(1.0f),
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
    il2cpp_class_get_method_from_name_t m_fn_class_get_method;
    il2cpp_runtime_invoke_t          m_fn_runtime_invoke;
    il2cpp_array_length_t            m_fn_array_length;
    il2cpp_array_get_t               m_fn_array_get;
    il2cpp_object_get_class_t        m_fn_object_get_class;
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
    Il2CppClass* m_classCamera;
    Il2CppClass* m_classBall;       // DrawCircle (球体类)
    Il2CppClass* m_classTransform;  // UnityEngine.Transform
    Il2CppImage* m_imageCSharp;
    Il2CppDomain* m_domain;

    // 缓存的方法
    MethodInfo* m_methodGetInstance;
    MethodInfo* m_methodGetCurrent;
    MethodInfo* m_methodGetName;
    MethodInfo* m_methodGetPosition;            // Transform.get_position
    MethodInfo* m_methodGetOrthographicSize;    // Camera.get_orthographicSize

    // 字段偏移
    FieldOffsets m_offsets;         // PlayerBase 偏移
    FieldOffsets m_ballOffsets;     // Ball 偏移 (独立解析, 字段布局可能不同)

    // 按类动态缓存字段偏移 (运行时用 object_get_class 拿真实类, 再解析偏移)
    // key = Il2CppClass*, value = 该类的字段偏移
    // 解决问题: BallDic 里的对象类名未知, 用 object_get_class 动态识别
    std::mutex m_classOffsetMutex;
    std::unordered_map<Il2CppClass*, FieldOffsets> m_classOffsetCache;

    // 已记录过的类名 (诊断用, 首次遇到某类时打印其所有字段)
    std::unordered_set<Il2CppClass*> m_loggedClasses;

    // --- 游戏状态 ---
    std::atomic<bool> m_gameReady;
    std::atomic<bool> m_threadAttached;
    std::atomic<uint32_t> m_counter;
    std::atomic<uint32_t> m_pcounter;
    Il2CppObject* m_gameInstance;

    // 游戏对象列表
    std::mutex m_objectMutex;
    std::vector<GameObjectInfo> m_objects;

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
    ImU32 GetObjectColor(const GameObjectInfo& obj);
    bool FindBallAndTransformClasses();
    bool InitBallFieldOffsets(Il2CppClass* ballClass);
    bool GetTransformPosition(Il2CppObject* transformObj, float& outX, float& outY, float& outZ);
    bool TryReadFromBallDic(void* dicObj, int selfPlayerId, std::vector<GameObjectInfo>& out, uint32_t c);
    void FillBallObject(Il2CppObject* ballObj, int selfPlayerId, std::vector<GameObjectInfo>& out, uint32_t c, size_t idx);

    // 动态解析对象所属类的字段偏移 (不依赖类名, 用 object_get_class 识别)
    // 返回缓存的偏移; 首次遇到某类时会遍历其所有字段并打印日志
    const FieldOffsets& GetOrResolveClassOffsets(Il2CppClass* objClass);
    // 通用字段名匹配 (供 GetOrResolveClassOffsets 调用, 填充 FieldOffsets)
    void MatchFieldOffsets(const char* fname, size_t offset, FieldOffsets& out);
    // 解析一个对象的字段 (用对象自身类的偏移)
    GameObjectInfo ReadObjectWithOwnClass(Il2CppObject* obj, int selfPlayerId);
};

// =============================================================================
// 便捷调用 (供 imguijni.cpp 的 step() 调用, 对应 game_loop.rs 的调度)
// =============================================================================

bool ESP_Init();
void ESP_Tick();
void ESP_Render();
