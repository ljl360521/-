#ifndef PHYSX_VISUAL_H
#define PHYSX_VISUAL_H

#include "il2cpp.h"
#include "imgui.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <thread>
#include <cmath>

// =====================================================================
// PhysX Collider 可视化系统
// 支持: BoxCollider / SphereCollider / CapsuleCollider 线框绘制
// =====================================================================

enum class ColliderType {
    Box,
    Sphere,
    Capsule
};

struct CachedCollider {
    Il2CppObject_API* collider;      // Collider 组件指针
    Il2CppObject_API* transform;     // 所属 Transform
    ColliderType type;
    std::string objName;

    // BoxCollider: center + size (local space)
    // SphereCollider: center + radius
    // CapsuleCollider: center + radius + height + direction
    float center[3];
    float size[3];      // Box: size xyz
    float radius;       // Sphere / Capsule
    float height;       // Capsule
    int   direction;    // Capsule: 0=X, 1=Y, 2=Z
};

// 屏幕空间绘制基元
struct DrawWireLine   { ImVec2 from, to; ImU32 color; };
struct DrawWireCircle { ImVec2 center; float radius; ImU32 color; };

class PhysXVisualSystem {
private:

    std::mutex mtx;
    bool enabled;
    bool methods_cached;

    // ===== IL2CPP 类/方法缓存 =====
    Il2CppClass_API* camera_class;
    Il2CppClass_API* transform_class;
    Il2CppClass_API* component_class;
    Il2CppClass_API* object_class;

    // Collider 类
    Il2CppClass_API* box_collider_class;
    Il2CppClass_API* sphere_collider_class;
    Il2CppClass_API* capsule_collider_class;

    // Camera
    const MethodInfo* camera_get_main;
    const MethodInfo* camera_world_to_screen;
    const MethodInfo* camera_get_pixel_width;
    const MethodInfo* camera_get_pixel_height;

    // Transform
    const MethodInfo* transform_get_position;
    const MethodInfo* transform_get_rotation;
    const MethodInfo* transform_get_lossyScale;
    const MethodInfo* transform_localToWorldMatrix;

    // Component
    const MethodInfo* component_get_transform;

    // Object
    const MethodInfo* object_find_objects_of_type;
    const MethodInfo* object_get_name;

    // BoxCollider
    const MethodInfo* box_get_center;
    const MethodInfo* box_get_size;

    // SphereCollider
    const MethodInfo* sphere_get_center;
    const MethodInfo* sphere_get_radius;

    // CapsuleCollider
    const MethodInfo* capsule_get_center;
    const MethodInfo* capsule_get_radius;
    const MethodInfo* capsule_get_height;
    const MethodInfo* capsule_get_direction;

    // Collider 通用
    const MethodInfo* collider_get_enabled;

    // ===== 扫描缓存 =====
    std::vector<CachedCollider> cached_colliders;
    int stat_box_count, stat_sphere_count, stat_capsule_count;

    // ===== 每帧绘制数据 =====
    std::vector<DrawWireLine> frame_lines;

    // ===== 控制 =====
    bool show_box;
    bool show_sphere;
    bool show_capsule;
    bool only_show_enabled;
    float max_distance;         // 最大显示距离
    int   wireframe_segments;   // 圆弧分段数

    ImU32 color_box;
    ImU32 color_sphere;
    ImU32 color_capsule;

    float screen_width, screen_height;
    float scan_timer, scan_interval;

    // 异步扫描
    std::atomic<bool> scanning;
    std::thread scan_thread;

    // ===== 数学类型 =====
    struct Vec3 { float x, y, z; };
    struct Vec4 { float x, y, z, w; };  // quaternion
    struct Mat4x4 {
        float m[16]; // column-major (Unity 风格)
        Vec3 multiplyPoint(Vec3 p) const {
            // Unity Matrix4x4.MultiplyPoint3x4
            return {
                m[0]*p.x + m[4]*p.y + m[8]*p.z  + m[12],
                m[1]*p.x + m[5]*p.y + m[9]*p.z  + m[13],
                m[2]*p.x + m[6]*p.y + m[10]*p.z + m[14]
            };
        }
        Vec3 multiplyVector(Vec3 v) const {
            return {
                m[0]*v.x + m[4]*v.y + m[8]*v.z,
                m[1]*v.x + m[5]*v.y + m[9]*v.z,
                m[2]*v.x + m[6]*v.y + m[10]*v.z
            };
        }
    };

    // ===== 内部方法 =====
    bool cacheMethods();

    inline bool alive(void* obj) {
        if (!obj || (uintptr_t)obj < 0x10000) return false;
        void* k = *(void**)obj;
        if (!k || (uintptr_t)k < 0x10000) return false;
        uintptr_t cp = *(uintptr_t*)((uint8_t*)obj + 0x10);
        return cp != 0;
    }

    Vec3 getPos(Il2CppObject_API* t);
    bool w2s(Il2CppObject_API* cam, Vec3 w, ImVec2& out);
    float vec3Dist(Vec3 a, Vec3 b);

    // 通过 methodPointer 直接调用获取 collider 属性
    Vec3  callGetVec3(const MethodInfo* m, Il2CppObject_API* obj);
    float callGetFloat(const MethodInfo* m, Il2CppObject_API* obj);
    int   callGetInt(const MethodInfo* m, Il2CppObject_API* obj);
    bool  callGetBool(const MethodInfo* m, Il2CppObject_API* obj);
    Mat4x4 getLocalToWorldMatrix(Il2CppObject_API* transform);

    // 扫描 + 帧更新
    void doScanThread();
    void doFrameUpdate();

    // 线框生成（世界坐标 → 屏幕线段）
    void generateBoxWireframe(Il2CppObject_API* cam, const CachedCollider& c,
                              Il2CppObject_API* tf, std::vector<DrawWireLine>& out);
    void generateSphereWireframe(Il2CppObject_API* cam, const CachedCollider& c,
                                 Il2CppObject_API* tf, std::vector<DrawWireLine>& out);
    void generateCapsuleWireframe(Il2CppObject_API* cam, const CachedCollider& c,
                                  Il2CppObject_API* tf, std::vector<DrawWireLine>& out);

    // 辅助：在世界空间画一个圆弧（投影到屏幕）
    void projectCircle(Il2CppObject_API* cam, Vec3 worldCenter,
                       Vec3 axis1, Vec3 axis2, float radius,
                       int segments, ImU32 color,
                       std::vector<DrawWireLine>& out);

public:

    PhysXVisualSystem();
    ~PhysXVisualSystem();
    bool initialize();
    void setEnabled(bool state) { enabled = state; }
    bool isEnabled() const { return enabled; }
    void update(float dt);
    void drawControlPanel();
    void drawColliders();
    void refresh();
    bool isInitialized() const { return methods_cached; }
};

extern PhysXVisualSystem g_physx_visual;

#endif // PHYSX_VISUAL_H