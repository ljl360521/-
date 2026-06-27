#pragma once

#include "il2cpp.h"
#include "imgui.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include <thread>

struct CachedObject {
    Il2CppObject_API* transform;
    Il2CppObject_API* component;   // 类实例指针（非Transform时有值）
    std::string objName;
    std::string className;
};

struct DrawLabel {
    ImVec2 pos;
    std::string text;
    uintptr_t transformAddr;
    uintptr_t componentAddr;
    float worldX, worldY, worldZ;
    float screenX, screenY;
};

struct DrawBoneLine {
    ImVec2 from, to;
    ImU32 color;
    float thickness;
};

class ESPSystem {
private:
    std::mutex mtx;
    bool methods_cached;

    Il2CppClass_API* camera_class;
    Il2CppClass_API* transform_class;
    Il2CppClass_API* component_class;
    Il2CppClass_API* object_class;
    Il2CppClass_API* monobehaviour_class;

    const MethodInfo* camera_get_main;
    const MethodInfo* camera_world_to_screen;
    const MethodInfo* camera_get_pixel_width;
    const MethodInfo* camera_get_pixel_height;
    const MethodInfo* transform_get_position;
    const MethodInfo* transform_get_child_count;
    const MethodInfo* transform_get_child;
    const MethodInfo* component_get_transform;
    const MethodInfo* object_find_objects_of_type;
    const MethodInfo* object_get_name;

    Il2CppClass_API* physics_class;
    const MethodInfo* physics_linecast;
    int physics_linecast_param_count;
    bool occlusion_methods_cached;
    bool occlusion_enabled;
    int  occlusion_layer_mask;
    ImU32 color_visible;
    ImU32 color_occluded;

    std::unordered_map<std::string, std::vector<CachedObject>> class_groups;
    std::vector<std::string> class_names_sorted;
    std::unordered_set<std::string> selected_classes;

    std::vector<std::string> tf_bone_names_sorted;
    std::vector<std::string> tf_other_names_sorted;
    std::unordered_set<std::string> selected_tf_bones;
    std::unordered_set<std::string> selected_tf_others;
    std::unordered_map<std::string, int> tf_bone_count;
    std::unordered_map<std::string, int> tf_other_count;

    struct BoneSet {
        std::unordered_map<std::string, Il2CppObject_API*> bones;
    };
    std::vector<BoneSet> cached_bone_sets;

    std::vector<DrawLabel> frame_labels;
    std::vector<DrawBoneLine> frame_bones;

    bool skeleton_enabled;
    float screen_width, screen_height;
    float scan_timer, scan_interval;

    std::atomic<bool> scanning;
    std::thread scan_thread;

    // ===== 显示开关 =====
    bool show_transform_addr;   // 显示 Transform 地址
    bool show_component_addr;   // 显示类实例地址
    bool show_world_pos;        // 显示世界坐标
    bool show_screen_pos;       // 显示屏幕坐标

    struct Vec3 { float x, y, z; };

    bool cacheMethods();
    bool cacheOcclusionMethods();
    
    inline bool alive(void* obj) {
        if (!obj || (uintptr_t)obj < 0x10000) return false;
        void* k = *(void**)obj;
        if (!k || (uintptr_t)k < 0x10000) return false;
        uintptr_t cp = *(uintptr_t*)((uint8_t*)obj + 0x10);
        return cp != 0;
    }

    Vec3 getPos(Il2CppObject_API* t);
    bool w2s(Il2CppObject_API* cam, Vec3 w, ImVec2& out);

    bool linecastSafe(Vec3 from, Vec3 to);

    static bool isBoneNode(const std::string& name);
    void doScanThread();
    void doFrameUpdate();

    void collectChildren(Il2CppObject_API* t,
        std::vector<std::pair<std::string, Il2CppObject_API*>>& out, int depth);

public:
    ESPSystem();
    ~ESPSystem();
    bool initialize();
    void update(float dt);
    void drawControlPanel();
    void drawESP();
    void refresh();
    bool isInitialized() const { return methods_cached; }
};

extern ESPSystem g_esp;