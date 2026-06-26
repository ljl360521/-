#ifndef PLAYER_ESP_H
#define PLAYER_ESP_H

#include "il2cpp.h"
#include "imgui.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include <cmath>

namespace PlayerOffsets {

    // ===== 核心类特征值 =====
    constexpr uintptr_t Feature_Offset_A         = 0x3b8;
    constexpr uintptr_t Feature_Offset_B         = 0x3bC;
    constexpr float     Feature_Value_A          = 3.5f;
    constexpr float     Feature_Value_B          = 5.0f;

    // ===== 核心类实例 → ActorDic =====
    constexpr uintptr_t Terrain_ActorDic         = 0xb0;

    // ===== Dictionary 布局 =====
    constexpr uintptr_t Dict_Entries             = 0x18;
    constexpr uintptr_t Dict_Count               = 0x20;
    constexpr uintptr_t Dict_FirstValue          = 0x30;
    constexpr uintptr_t Dict_EntryStride         = 0x18;

    // ===== BaseActor 字段 =====
    constexpr uintptr_t Actor_Character          = 0x20  ; // 角色Transform
    constexpr uintptr_t Actor_BoneRoot           = 0x28  ; // 骨骼根节点
    constexpr uintptr_t Actor_playerName         = 0xC0  ; // 玩家名称
    constexpr uintptr_t Actor_isAlive            = 0xD1  ; // 是否存活
    constexpr uintptr_t Actor_isRealSelf         = 0xb0  ; // 是否自己
    constexpr uintptr_t Actor_ID                 = 0xA8  ; // 玩家ID

    // ===== UGCActor 字段 =====
    constexpr uintptr_t Actor_HP                 = 0x2b8 ; // 血量
    constexpr uintptr_t Actor_MaxHP              = 0x2bC ; // 最大血量
    constexpr uintptr_t Actor_teamId     = 0x2F0;   // 改：0x2e4 → 0x2F0
    constexpr uintptr_t Actor_isAI               = 0x108 ; // 是否AI
    constexpr uintptr_t Actor_liftState          = 0x8E8 ; // 生命状态
    constexpr uintptr_t Actor_IsHide             = 0x120 ; // 是否隐藏
}

enum class PlayerRelation { Self, Teammate, Enemy, AI, Unknown };

struct PlayerDrawLabel { ImVec2 pos; std::string text; ImU32 color; };
struct PlayerDrawBone  { ImVec2 from, to; ImU32 color; float thickness; };
struct PlayerDrawHP    { ImVec2 pos; float ratio; std::string name; float distance; PlayerRelation relation; };

// =====================================================================
// Raycast 请求/结果（GLThread 写请求，Unity 主线程执行，GLThread 读结果）
// =====================================================================

struct RaycastRequest {
    float camX, camY, camZ;
    float boneX, boneY, boneZ;
    int boneIndex;  // 用于关联结果
};

struct RaycastResult {
    int boneIndex;
    bool hit;
};

class PlayerESP {
private:
    bool methods_cached;

    Il2CppClass_API* camera_class;
    Il2CppClass_API* transform_class;
    Il2CppClass_API* component_class;
    Il2CppClass_API* physics_class;
    Il2CppClass_API* object_class;

    const MethodInfo* camera_get_main;
    const MethodInfo* camera_world_to_screen;
    const MethodInfo* camera_get_pixel_width;
    const MethodInfo* camera_get_pixel_height;
    const MethodInfo* transform_get_position;
    const MethodInfo* transform_get_child_count;
    const MethodInfo* transform_get_child;
    const MethodInfo* component_get_transform;
    const MethodInfo* object_get_name;
    const MethodInfo* object_op_implicit;
    const MethodInfo* object_op_inequality;

    const MethodInfo* physics_raycast_method;
    bool raycast_cached;

    // op_Inequality hook
    static void* original_op_inequality_func;
    bool op_inequality_hooked;
    static bool hooked_op_inequality(void* x, void* y);
    bool hookOpInequality();

    // Raycast 队列（GLThread → Unity 主线程）
    std::mutex raycast_mtx;
    std::vector<RaycastRequest> raycast_requests;
    std::unordered_map<int, bool> raycast_results;  // boneIndex → hit
    std::atomic<int> raycast_generation;  // 用于判断结果是否是最新的

    void executeRaycastQueue();  // 在 op_Inequality hook 中调用

    void* core_instance;
    std::string core_class_name;
    bool core_found;

    // 骨骼缓存
    std::unordered_map<void*, std::unordered_map<std::string, void*>> bone_cache;

    std::vector<PlayerDrawLabel> frame_labels;
    std::vector<PlayerDrawBone>  frame_bones;
    std::vector<PlayerDrawHP>    frame_hpbars;

    struct DebugPlayerInfo {
        std::string name;
        const char* relation;
        int32_t hp, maxHp;
        uint32_t teamId;
        size_t boneCount;
    };
    std::vector<DebugPlayerInfo> debug_players;
    uint32_t debug_self_team;
    int debug_player_count;

    bool enabled;
    bool show_bones, show_names, show_health, show_distance;
    bool show_teammates, show_ai, show_self;
    bool occlusion_enabled;
    int  occlusion_layer_mask;
    float max_esp_distance;

    ImU32 color_enemy_visible, color_enemy_occluded;
    ImU32 color_teammate_visible, color_teammate_occluded;
    ImU32 color_ai, color_self;

    float screen_width, screen_height;

    struct Vec3 { float x, y, z; };

    bool cacheMethods();
    bool cacheRaycast();

    inline bool alive(void* obj) {
        if (!obj || (uintptr_t)obj < 0x10000) return false;
        void* k = *(void**)obj;
        if (!k || (uintptr_t)k < 0x10000) return false;
        uintptr_t cp = *(uintptr_t*)((uint8_t*)obj + 0x10);
        return cp != 0;
    }

    bool unityAlive(void* obj);

    template<typename T>
    inline T readMem(void* base, uintptr_t offset) {
        if (!base || (uintptr_t)base < 0x10000) return T{};
        return *(T*)((uint8_t*)base + offset);
    }

    inline void* readPtr(void* base, uintptr_t offset) {
        if (!base || (uintptr_t)base < 0x10000) return nullptr;
        return *(void**)((uint8_t*)base + offset);
    }

    std::string readIl2CppString(void* strPtr);
    Vec3 getPos(void* transform);
    bool w2s(void* cam, Vec3 w, ImVec2& out);
    float vec3Dist(Vec3 a, Vec3 b);

    bool findCoreByClassName();
    bool verifyCoreInstance(void* instance);

    void collectBonesSafe(void* boneRoot, std::unordered_map<std::string, void*>& bones);

    PlayerRelation calcRelation(bool isRealSelf, bool isAI, uint32_t teamId, uint32_t selfTeamId);
    ImU32 getColor(PlayerRelation rel, bool occluded);

    void doFrameUpdate();
    
    // 在 PlayerESP 类中添加：
    bool autoFindCoreFromDump(const std::string& dumpPath);


public:
    PlayerESP();
    ~PlayerESP();

    void setCoreClassName(const std::string& name) { core_class_name = name; }
    void setCoreInstance(void* inst) { core_instance = inst; core_found = (inst != nullptr); }
    void* getCoreInstance() const { return core_instance; }
    bool isCoreFound() const { return core_found; }
    void* getSelfActor();

    bool initialize();
    void update(float dt);
    void drawControlPanel();
    void drawESP();
    void refresh();
    bool isInitialized() const { return methods_cached; }
};

extern PlayerESP g_player_esp;
#endif