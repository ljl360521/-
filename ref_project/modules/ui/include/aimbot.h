#ifndef AIMBOT_H
#define AIMBOT_H

#include "il2cpp.h"
#include "imgui.h"
#include <string>
#include <vector>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace AimOffsets {
    constexpr uintptr_t Terrain_ActorDic = 0xb0;    // 改：和 PlayerESP 一致
    // Dict 布局不变
    constexpr uintptr_t Dict_Entries     = 0x18;
    constexpr uintptr_t Dict_Count       = 0x20;
    constexpr uintptr_t Dict_FirstValue  = 0x30;
    constexpr uintptr_t Dict_EntryStride = 0x18;

    constexpr uintptr_t Actor_BoneRoot   = 0x28;
    constexpr uintptr_t Actor_playerName = 0xC0;
    constexpr uintptr_t Actor_isRealSelf = 0xA8;
    constexpr uintptr_t Actor_HP         = 0x2b8;
    constexpr uintptr_t Actor_teamId     = 0x2F0;   // 改：0x2e4 → 0x2F0
    constexpr uintptr_t Actor_isAI       = 0x108;
    constexpr uintptr_t Actor_LiLi       = 0xCC8;
    constexpr uintptr_t Enemy_PosX       = 0x61c;
    constexpr uintptr_t Enemy_PosY       = 0x620;
    constexpr uintptr_t Enemy_PosZ       = 0x624;
    constexpr uintptr_t Self_PosX        = 0x61c;
    constexpr uintptr_t Self_PosY        = 0x620;
    constexpr uintptr_t Self_PosZ        = 0x624;
    constexpr uintptr_t Rot_X            = 0x640;   // 改：0x554 → 0x638
    constexpr uintptr_t Rot_Y            = 0x644;   // 改：0x558 → 0x63c
    constexpr uintptr_t Rot_Z            = 0x648;   // 改：0x55c → 0x640
}

enum class AimBone   { Head, Neck, Chest, Stomach, Pelvis };
enum class TargetSel { Crosshair, Distance };

struct AimTarget {
    void* actor;
    std::string name;
    float dist3D, screenDist, score;
    ImVec2 screenPos;
    float bx, by, bz;
    bool behind_cover;
};

class Aimbot {
private:
    bool methods_cached;

    Il2CppClass_API* camera_class;
    Il2CppClass_API* transform_class;
    Il2CppClass_API* component_class;
    Il2CppClass_API* object_class;
    Il2CppClass_API* physics_class;

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
    const MethodInfo* physics_raycast;

    void* core_instance;
    bool  core_found;

    bool      enabled;
    AimBone   aim_bone;
    TargetSel target_sel;
    bool      aim_enemy_only;
    bool      aim_ai;
    float     max_aim_distance;
    float     aim_fov;
    float     target_height;
    float     self_offset_y;

    std::unordered_map<void*, std::unordered_map<std::string, void*>> bone_cache;

    AimTarget current_target;
    bool has_target;
    void* self_actor;

    bool show_fov_circle;
    bool show_target_line;

    int debug_candidates;
    std::string debug_tgt_name;
    float debug_tgt_dist;
    bool  debug_tgt_cover;

    float screen_width, screen_height;

    struct Vec3 { float x, y, z; };

    // ===== 预判系统 =====
    bool  prediction_enabled;     // 预判总开关
    float bullet_speed;           // 子弹速度 (m/s)
    float max_predict_time;       // 最大预判时间 (s)
    int   prediction_iterations;  // 迭代次数
    float gravity;                // 重力补偿 (m/s²)，0=不补偿

    // 每个敌人的历史位置，用于计算速度
    struct EnemyHistory {
        Vec3  prev_pos;           // 上一帧位置
        Vec3  velocity;           // 计算出的速度
        bool  has_prev;           // 是否有上一帧数据
        float smooth_vx, smooth_vy, smooth_vz; // 平滑速度
    };
    std::unordered_map<void*, EnemyHistory> enemy_history;

    Vec3 computeEnemyVelocity(void* actor, Vec3 current_pos, float dt);
    Vec3 predictFuturePos(Vec3 self_pos, Vec3 enemy_pos, Vec3 enemy_vel, float dt);

    bool cacheMethods();

    inline bool alive(void* p) {
        if (!p || (uintptr_t)p < 0x10000) return false;
        if (!*(void**)p || (uintptr_t)(*(void**)p) < 0x10000) return false;
        return *(uintptr_t*)((uint8_t*)p + 0x10) != 0;
    }
    bool unityAlive(void* p);

    template<typename T> T rd(void* b, uintptr_t o) {
        return (b && (uintptr_t)b > 0x10000) ? *(T*)((uint8_t*)b + o) : T{};
    }
    void* rdp(void* b, uintptr_t o) {
        return (b && (uintptr_t)b > 0x10000) ? *(void**)((uint8_t*)b + o) : nullptr;
    }

    std::string readStr(void* s);
    Vec3 getPos(void* tf);
    bool w2s(void* cam, Vec3 w, ImVec2& out);
    float vdist(Vec3 a, Vec3 b);
    Vec3 vnorm(Vec3 v);

    const char* boneKey(AimBone b);
    bool getBonePos(void* actor, Vec3& out);
    void collectBones(void* root, std::unordered_map<std::string, void*>& bones);

    void doUpdate(float dt);

public:
    Aimbot();
    ~Aimbot();
    void setCoreInstance(void* inst) { core_instance = inst; core_found = (inst != nullptr); }
    bool initialize();
    void update(float dt);
    void drawESP();
    void drawControlPanel();
    bool isInitialized() const { return methods_cached; }
};

extern Aimbot g_aimbot;
#endif