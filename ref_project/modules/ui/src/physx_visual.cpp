#include "physx_visual.h"
#include "gl_bindless_renderer.h"

#include <algorithm>
#include <cstring>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

PhysXVisualSystem g_physx_visual;

// =====================================================================
// 构造 / 析构
// =====================================================================

PhysXVisualSystem::PhysXVisualSystem()
    : enabled(true),
      methods_cached(false),
      camera_class(nullptr), transform_class(nullptr),
      component_class(nullptr), object_class(nullptr),
      box_collider_class(nullptr), sphere_collider_class(nullptr),
      capsule_collider_class(nullptr),
      camera_get_main(nullptr), camera_world_to_screen(nullptr),
      camera_get_pixel_width(nullptr), camera_get_pixel_height(nullptr),
      transform_get_position(nullptr), transform_get_rotation(nullptr),
      transform_get_lossyScale(nullptr), transform_localToWorldMatrix(nullptr),
      component_get_transform(nullptr),
      object_find_objects_of_type(nullptr), object_get_name(nullptr),
      box_get_center(nullptr), box_get_size(nullptr),
      sphere_get_center(nullptr), sphere_get_radius(nullptr),
      capsule_get_center(nullptr), capsule_get_radius(nullptr),
      capsule_get_height(nullptr), capsule_get_direction(nullptr),
      collider_get_enabled(nullptr),
      stat_box_count(0), stat_sphere_count(0), stat_capsule_count(0),
      show_box(true), show_sphere(true), show_capsule(true),
      only_show_enabled(true), max_distance(100.0f),
      wireframe_segments(16),
      color_box(IM_COL32(0, 255, 0, 180)),
      color_sphere(IM_COL32(0, 180, 255, 180)),
      color_capsule(IM_COL32(255, 180, 0, 180)),
      screen_width(1920), screen_height(1080),
      scan_timer(0), scan_interval(5.0f),
      scanning(false) {}

PhysXVisualSystem::~PhysXVisualSystem() {
    if (scan_thread.joinable()) scan_thread.join();
}

// =====================================================================
// 初始化 — 缓存所有 IL2CPP 方法
// =====================================================================

bool PhysXVisualSystem::initialize() {
    if (methods_cached) return true;
    return cacheMethods();
}

bool PhysXVisualSystem::cacheMethods() {
    if (!g_il2cpp_tool || !g_il2cpp_tool->isInitialized()) return false;

    // 基础类
    camera_class    = g_il2cpp_tool->findClass("UnityEngine", "Camera");
    transform_class = g_il2cpp_tool->findClass("UnityEngine", "Transform");
    object_class    = g_il2cpp_tool->findClass("UnityEngine", "Object");
    component_class = g_il2cpp_tool->findClass("UnityEngine", "Component");
    if (!camera_class || !transform_class || !object_class) return false;

    // Collider 类
    box_collider_class     = g_il2cpp_tool->findClass("UnityEngine", "BoxCollider");
    sphere_collider_class  = g_il2cpp_tool->findClass("UnityEngine", "SphereCollider");
    capsule_collider_class = g_il2cpp_tool->findClass("UnityEngine", "CapsuleCollider");
    // 至少要有一种 collider
    if (!box_collider_class && !sphere_collider_class && !capsule_collider_class) {
        LOGW("[PhysX] No collider classes found");
        return false;
    }

    // Camera 方法
    camera_get_main         = g_il2cpp_tool->findMethod(camera_class, "get_main", 0);
    camera_world_to_screen  = g_il2cpp_tool->findMethod(camera_class, "WorldToScreenPoint", 1);
    camera_get_pixel_width  = g_il2cpp_tool->findMethod(camera_class, "get_pixelWidth", 0);
    camera_get_pixel_height = g_il2cpp_tool->findMethod(camera_class, "get_pixelHeight", 0);

    // Transform 方法
    transform_get_position     = g_il2cpp_tool->findMethod(transform_class, "get_position", 0);
    transform_get_rotation     = g_il2cpp_tool->findMethod(transform_class, "get_rotation", 0);
    transform_get_lossyScale   = g_il2cpp_tool->findMethod(transform_class, "get_lossyScale", 0);
    transform_localToWorldMatrix = g_il2cpp_tool->findMethod(transform_class, "get_localToWorldMatrix", 0);

    // Component
    if (component_class)
        component_get_transform = g_il2cpp_tool->findMethod(component_class, "get_transform", 0);

    // Object
    object_find_objects_of_type = g_il2cpp_tool->findMethod(object_class, "FindObjectsOfType", 1);
    object_get_name             = g_il2cpp_tool->findMethod(object_class, "get_name", 0);

    // BoxCollider 属性
    if (box_collider_class) {
        box_get_center = g_il2cpp_tool->findMethod(box_collider_class, "get_center", 0);
        box_get_size   = g_il2cpp_tool->findMethod(box_collider_class, "get_size", 0);
    }

    // SphereCollider 属性
    if (sphere_collider_class) {
        sphere_get_center = g_il2cpp_tool->findMethod(sphere_collider_class, "get_center", 0);
        sphere_get_radius = g_il2cpp_tool->findMethod(sphere_collider_class, "get_radius", 0);
    }

    // CapsuleCollider 属性
    if (capsule_collider_class) {
        capsule_get_center    = g_il2cpp_tool->findMethod(capsule_collider_class, "get_center", 0);
        capsule_get_radius    = g_il2cpp_tool->findMethod(capsule_collider_class, "get_radius", 0);
        capsule_get_height    = g_il2cpp_tool->findMethod(capsule_collider_class, "get_height", 0);
        capsule_get_direction = g_il2cpp_tool->findMethod(capsule_collider_class, "get_direction", 0);
    }

    // Collider.enabled（Collider 继承自 Behaviour）
    auto behaviour_class = g_il2cpp_tool->findClass("UnityEngine", "Behaviour");
    if (behaviour_class)
        collider_get_enabled = g_il2cpp_tool->findMethod(behaviour_class, "get_enabled", 0);

    if (!camera_get_main || !camera_world_to_screen || !transform_get_position ||
        !object_find_objects_of_type) {
        LOGW("[PhysX] Missing critical methods");
        return false;
    }

    methods_cached = true;
    LOGI("[PhysX] Init OK (Box:%s Sphere:%s Capsule:%s)",
         box_collider_class ? "Y" : "N",
         sphere_collider_class ? "Y" : "N",
         capsule_collider_class ? "Y" : "N");
    return true;
}

// =====================================================================
// 基础数学 / IL2CPP 直接调用
// =====================================================================

PhysXVisualSystem::Vec3 PhysXVisualSystem::getPos(Il2CppObject_API* t) {
    Vec3 p = {0, 0, 0};
    if (!alive(t) || !transform_get_position) return p;
    typedef Vec3(*Fn)(Il2CppObject_API*, const MethodInfo*);
    return ((Fn)transform_get_position->methodPointer)(t, transform_get_position);
}

bool PhysXVisualSystem::w2s(Il2CppObject_API* cam, Vec3 w, ImVec2& out) {
    if (!alive(cam) || !camera_world_to_screen) return false;
    typedef Vec3(*Fn)(Il2CppObject_API*, Vec3, const MethodInfo*);
    Vec3 s = ((Fn)camera_world_to_screen->methodPointer)(cam, w, camera_world_to_screen);
    if (s.z < 0) return false;
    out.x = s.x;
    out.y = screen_height - s.y;
    return out.x > -500 && out.x < screen_width + 500 &&
           out.y > -500 && out.y < screen_height + 500;
}

float PhysXVisualSystem::vec3Dist(Vec3 a, Vec3 b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrtf(dx*dx + dy*dy + dz*dz);
}

PhysXVisualSystem::Vec3 PhysXVisualSystem::callGetVec3(const MethodInfo* m, Il2CppObject_API* obj) {
    Vec3 r = {0, 0, 0};
    if (!m || !alive(obj)) return r;
    typedef Vec3(*Fn)(Il2CppObject_API*, const MethodInfo*);
    return ((Fn)m->methodPointer)(obj, m);
}

float PhysXVisualSystem::callGetFloat(const MethodInfo* m, Il2CppObject_API* obj) {
    if (!m || !alive(obj)) return 0;
    typedef float(*Fn)(Il2CppObject_API*, const MethodInfo*);
    return ((Fn)m->methodPointer)(obj, m);
}

int PhysXVisualSystem::callGetInt(const MethodInfo* m, Il2CppObject_API* obj) {
    if (!m || !alive(obj)) return 0;
    typedef int(*Fn)(Il2CppObject_API*, const MethodInfo*);
    return ((Fn)m->methodPointer)(obj, m);
}

bool PhysXVisualSystem::callGetBool(const MethodInfo* m, Il2CppObject_API* obj) {
    if (!m || !alive(obj)) return false;
    typedef bool(*Fn)(Il2CppObject_API*, const MethodInfo*);
    return ((Fn)m->methodPointer)(obj, m);
}

PhysXVisualSystem::Mat4x4 PhysXVisualSystem::getLocalToWorldMatrix(Il2CppObject_API* transform) {
    Mat4x4 mat{};
    // 单位矩阵默认
    mat.m[0] = mat.m[5] = mat.m[10] = mat.m[15] = 1.0f;

    if (!alive(transform) || !transform_localToWorldMatrix) return mat;

    typedef Mat4x4(*Fn)(Il2CppObject_API*, const MethodInfo*);
    return ((Fn)transform_localToWorldMatrix->methodPointer)(transform, transform_localToWorldMatrix);
}

// =====================================================================
// doScanThread — 异步扫描所有 Collider
// =====================================================================

void PhysXVisualSystem::doScanThread() {
    auto thread = g_il2cpp_tool->attachCurrentThread();

    try {
        std::vector<CachedCollider> newColliders;
        int boxCnt = 0, sphereCnt = 0, capsuleCnt = 0;

        // 扫描三种 Collider
        struct ScanTarget {
            Il2CppClass_API* klass;
            ColliderType type;
            bool enabled;
        };
        ScanTarget targets[] = {
            { box_collider_class,     ColliderType::Box,     show_box },
            { sphere_collider_class,  ColliderType::Sphere,  show_sphere },
            { capsule_collider_class, ColliderType::Capsule, show_capsule },
        };

        for (auto& target : targets) {
            if (!target.klass || !target.enabled) continue;

            auto typeObj = g_il2cpp_tool->getClassTypeObject(target.klass);
            if (!typeObj) continue;

            void* args[1] = {typeObj};
            auto arr = g_il2cpp_tool->invokeMethod(object_find_objects_of_type, nullptr, args);
            if (!arr) continue;

            auto* a = (Il2CppArray_Dump*)arr;
            size_t cnt = a->max_length;

            for (size_t i = 0; i < cnt && i < 10000; i++) {
                try {
                    auto* comp = (Il2CppObject_API*)a->vector[i];
                    if (!alive(comp)) continue;

                    // 检查 enabled
                    if (only_show_enabled && collider_get_enabled) {
                        if (!callGetBool(collider_get_enabled, comp)) continue;
                    }

                    // 获取 Transform
                    typedef Il2CppObject_API*(*TFn)(Il2CppObject_API*, const MethodInfo*);
                    auto tf = component_get_transform ?
                        ((TFn)component_get_transform->methodPointer)(comp, component_get_transform) : nullptr;
                    if (!alive(tf)) continue;

                    // 获取名字
                    auto nameR = g_il2cpp_tool->invokeMethod(object_get_name, comp, nullptr);
                    std::string objName = nameR ?
                        g_il2cpp_tool->getString((Il2CppString_API*)nameR) : "?";

                    CachedCollider cc;
                    cc.collider  = comp;
                    cc.transform = tf;
                    cc.type      = target.type;
                    cc.objName   = objName;
                    cc.center[0] = cc.center[1] = cc.center[2] = 0;
                    cc.size[0] = cc.size[1] = cc.size[2] = 1;
                    cc.radius = 0.5f;
                    cc.height = 2.0f;
                    cc.direction = 1;

                    // 读取 collider 属性
                    switch (target.type) {
                    case ColliderType::Box: {
                        if (box_get_center) {
                            Vec3 c = callGetVec3(box_get_center, comp);
                            cc.center[0] = c.x; cc.center[1] = c.y; cc.center[2] = c.z;
                        }
                        if (box_get_size) {
                            Vec3 s = callGetVec3(box_get_size, comp);
                            cc.size[0] = s.x; cc.size[1] = s.y; cc.size[2] = s.z;
                        }
                        boxCnt++;
                        break;
                    }
                    case ColliderType::Sphere: {
                        if (sphere_get_center) {
                            Vec3 c = callGetVec3(sphere_get_center, comp);
                            cc.center[0] = c.x; cc.center[1] = c.y; cc.center[2] = c.z;
                        }
                        if (sphere_get_radius) {
                            cc.radius = callGetFloat(sphere_get_radius, comp);
                        }
                        sphereCnt++;
                        break;
                    }
                    case ColliderType::Capsule: {
                        if (capsule_get_center) {
                            Vec3 c = callGetVec3(capsule_get_center, comp);
                            cc.center[0] = c.x; cc.center[1] = c.y; cc.center[2] = c.z;
                        }
                        if (capsule_get_radius)
                            cc.radius = callGetFloat(capsule_get_radius, comp);
                        if (capsule_get_height)
                            cc.height = callGetFloat(capsule_get_height, comp);
                        if (capsule_get_direction)
                            cc.direction = callGetInt(capsule_get_direction, comp);
                        capsuleCnt++;
                        break;
                    }
                    }

                    newColliders.push_back(std::move(cc));
                } catch (...) { continue; }
            }
        }

        // 写入缓存
        {
            std::lock_guard<std::mutex> lock(mtx);
            cached_colliders = std::move(newColliders);
            stat_box_count     = boxCnt;
            stat_sphere_count  = sphereCnt;
            stat_capsule_count = capsuleCnt;
        }

        LOGI("[PhysX] Scan done: Box=%d Sphere=%d Capsule=%d",
             boxCnt, sphereCnt, capsuleCnt);

    } catch (...) {
        LOGW("[PhysX] Scan exception");
    }

    if (thread) g_il2cpp_tool->detachThread(thread);
    scanning.store(false);
}

// =====================================================================
// 线框生成
// =====================================================================

// 投影一个圆弧到屏幕空间
void PhysXVisualSystem::projectCircle(Il2CppObject_API* cam, Vec3 worldCenter,
                                       Vec3 axis1, Vec3 axis2, float radius,
                                       int segments, ImU32 color,
                                       std::vector<DrawWireLine>& out) {
    ImVec2 prevSP;
    bool prevOK = false;
    float step = 2.0f * (float)M_PI / segments;

    for (int i = 0; i <= segments; i++) {
        float angle = step * i;
        float cs = cosf(angle), sn = sinf(angle);
        Vec3 wp = {
            worldCenter.x + (axis1.x * cs + axis2.x * sn) * radius,
            worldCenter.y + (axis1.y * cs + axis2.y * sn) * radius,
            worldCenter.z + (axis1.z * cs + axis2.z * sn) * radius
        };
        ImVec2 sp;
        bool ok = w2s(cam, wp, sp);
        if (ok && prevOK) {
            out.push_back({prevSP, sp, color});
        }
        prevSP = sp;
        prevOK = ok;
    }
}

void PhysXVisualSystem::generateBoxWireframe(Il2CppObject_API* cam,
                                              const CachedCollider& c,
                                              Il2CppObject_API* tf,
                                              std::vector<DrawWireLine>& out) {
    Mat4x4 ltw = getLocalToWorldMatrix(tf);

    // Box 在 local space 的 8 个角点
    float hx = c.size[0] * 0.5f;
    float hy = c.size[1] * 0.5f;
    float hz = c.size[2] * 0.5f;
    float cx = c.center[0], cy = c.center[1], cz = c.center[2];

    Vec3 localCorners[8] = {
        {cx - hx, cy - hy, cz - hz},
        {cx + hx, cy - hy, cz - hz},
        {cx + hx, cy + hy, cz - hz},
        {cx - hx, cy + hy, cz - hz},
        {cx - hx, cy - hy, cz + hz},
        {cx + hx, cy - hy, cz + hz},
        {cx + hx, cy + hy, cz + hz},
        {cx - hx, cy + hy, cz + hz},
    };

    // 转到世界坐标
    ImVec2 screenPts[8];
    bool visible[8];
    for (int i = 0; i < 8; i++) {
        Vec3 wp = ltw.multiplyPoint(localCorners[i]);
        visible[i] = w2s(cam, wp, screenPts[i]);
    }

    // 12 条边
    static const int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},   // bottom face
        {4,5},{5,6},{6,7},{7,4},   // top face
        {0,4},{1,5},{2,6},{3,7},   // vertical edges
    };

    for (auto& e : edges) {
        if (visible[e[0]] && visible[e[1]]) {
            out.push_back({screenPts[e[0]], screenPts[e[1]], color_box});
        }
    }
}

void PhysXVisualSystem::generateSphereWireframe(Il2CppObject_API* cam,
                                                  const CachedCollider& c,
                                                  Il2CppObject_API* tf,
                                                  std::vector<DrawWireLine>& out) {
    Mat4x4 ltw = getLocalToWorldMatrix(tf);

    // Sphere center in world
    Vec3 localCenter = {c.center[0], c.center[1], c.center[2]};
    Vec3 worldCenter = ltw.multiplyPoint(localCenter);

    // 取 lossyScale 最大分量乘以 radius
    Vec3 scale = {1, 1, 1};
    if (transform_get_lossyScale && alive(tf)) {
        typedef Vec3(*Fn)(Il2CppObject_API*, const MethodInfo*);
        scale = ((Fn)transform_get_lossyScale->methodPointer)(tf, transform_get_lossyScale);
    }
    float maxScale = std::max({fabsf(scale.x), fabsf(scale.y), fabsf(scale.z)});
    float worldRadius = c.radius * maxScale;

    // 3 个正交圆环
    Vec3 axX = {1, 0, 0}, axY = {0, 1, 0}, axZ = {0, 0, 1};
    projectCircle(cam, worldCenter, axX, axY, worldRadius, wireframe_segments, color_sphere, out);
    projectCircle(cam, worldCenter, axX, axZ, worldRadius, wireframe_segments, color_sphere, out);
    projectCircle(cam, worldCenter, axY, axZ, worldRadius, wireframe_segments, color_sphere, out);
}

void PhysXVisualSystem::generateCapsuleWireframe(Il2CppObject_API* cam,
                                                   const CachedCollider& c,
                                                   Il2CppObject_API* tf,
                                                   std::vector<DrawWireLine>& out) {
    Mat4x4 ltw = getLocalToWorldMatrix(tf);

    Vec3 localCenter = {c.center[0], c.center[1], c.center[2]};

    // direction: 0=X, 1=Y, 2=Z
    Vec3 dirVec = {0, 1, 0};
    Vec3 perp1  = {1, 0, 0};
    Vec3 perp2  = {0, 0, 1};
    if (c.direction == 0) {
        dirVec = {1, 0, 0}; perp1 = {0, 1, 0}; perp2 = {0, 0, 1};
    } else if (c.direction == 2) {
        dirVec = {0, 0, 1}; perp1 = {1, 0, 0}; perp2 = {0, 1, 0};
    }

    float halfH = c.height * 0.5f;
    float r = c.radius;
    // Capsule 的实际半高是 max(halfH, r)
    float cylinderHalf = std::max(halfH - r, 0.0f);

    // 上下两个球心（local space）
    Vec3 topLocal = {
        localCenter.x + dirVec.x * cylinderHalf,
        localCenter.y + dirVec.y * cylinderHalf,
        localCenter.z + dirVec.z * cylinderHalf
    };
    Vec3 botLocal = {
        localCenter.x - dirVec.x * cylinderHalf,
        localCenter.y - dirVec.y * cylinderHalf,
        localCenter.z - dirVec.z * cylinderHalf
    };

    // 取 scale
    Vec3 scale = {1, 1, 1};
    if (transform_get_lossyScale && alive(tf)) {
        typedef Vec3(*Fn)(Il2CppObject_API*, const MethodInfo*);
        scale = ((Fn)transform_get_lossyScale->methodPointer)(tf, transform_get_lossyScale);
    }

    // 对于 Capsule，radius 受垂直于 direction 的两个轴 scale 的最大值影响
    float scaleR, scaleH;
    if (c.direction == 0) {
        scaleR = std::max(fabsf(scale.y), fabsf(scale.z));
        scaleH = fabsf(scale.x);
    } else if (c.direction == 2) {
        scaleR = std::max(fabsf(scale.x), fabsf(scale.y));
        scaleH = fabsf(scale.z);
    } else { // Y
        scaleR = std::max(fabsf(scale.x), fabsf(scale.z));
        scaleH = fabsf(scale.y);
    }

    float worldR = r * scaleR;

    // 世界坐标
    Vec3 topWorld = ltw.multiplyPoint(topLocal);
    Vec3 botWorld = ltw.multiplyPoint(botLocal);

    // 世界空间的轴方向（用 matrix 变换方向向量）
    Vec3 wDir   = ltw.multiplyVector(dirVec);
    Vec3 wPerp1 = ltw.multiplyVector(perp1);
    Vec3 wPerp2 = ltw.multiplyVector(perp2);

    // 归一化
    auto normalize = [](Vec3& v) {
        float len = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
        if (len > 0.0001f) { v.x /= len; v.y /= len; v.z /= len; }
    };
    normalize(wDir);
    normalize(wPerp1);
    normalize(wPerp2);

    // 上下圆环
    projectCircle(cam, topWorld, wPerp1, wPerp2, worldR, wireframe_segments, color_capsule, out);
    projectCircle(cam, botWorld, wPerp1, wPerp2, worldR, wireframe_segments, color_capsule, out);

    // 中间圆环
    Vec3 midWorld = {
        (topWorld.x + botWorld.x) * 0.5f,
        (topWorld.y + botWorld.y) * 0.5f,
        (topWorld.z + botWorld.z) * 0.5f
    };
    projectCircle(cam, midWorld, wPerp1, wPerp2, worldR, wireframe_segments, color_capsule, out);

    // 4 条纵向线
    for (int i = 0; i < 4; i++) {
        float angle = (float)M_PI * 0.5f * i;
        float cs = cosf(angle), sn = sinf(angle);
        Vec3 offset = {
            (wPerp1.x * cs + wPerp2.x * sn) * worldR,
            (wPerp1.y * cs + wPerp2.y * sn) * worldR,
            (wPerp1.z * cs + wPerp2.z * sn) * worldR
        };
        Vec3 p1 = {topWorld.x + offset.x, topWorld.y + offset.y, topWorld.z + offset.z};
        Vec3 p2 = {botWorld.x + offset.x, botWorld.y + offset.y, botWorld.z + offset.z};
        ImVec2 s1, s2;
        if (w2s(cam, p1, s1) && w2s(cam, p2, s2)) {
            out.push_back({s1, s2, color_capsule});
        }
    }

    // 半球弧线（上下各 2 条弧）
    int halfSeg = wireframe_segments / 2;
    // 上半球
    for (int axis = 0; axis < 2; axis++) {
        Vec3 arcPerp = (axis == 0) ? wPerp1 : wPerp2;
        ImVec2 prevSP;
        bool prevOK = false;
        float step = (float)M_PI / halfSeg;
        for (int i = 0; i <= halfSeg; i++) {
            float angle = step * i;
            float cs = cosf(angle), sn = sinf(angle);
            Vec3 wp = {
                topWorld.x + (arcPerp.x * cs + wDir.x * sn) * worldR,
                topWorld.y + (arcPerp.y * cs + wDir.y * sn) * worldR,
                topWorld.z + (arcPerp.z * cs + wDir.z * sn) * worldR
            };
            ImVec2 sp;
            bool ok = w2s(cam, wp, sp);
            if (ok && prevOK) out.push_back({prevSP, sp, color_capsule});
            prevSP = sp; prevOK = ok;
        }
    }
    // 下半球
    for (int axis = 0; axis < 2; axis++) {
        Vec3 arcPerp = (axis == 0) ? wPerp1 : wPerp2;
        ImVec2 prevSP;
        bool prevOK = false;
        float step = (float)M_PI / halfSeg;
        for (int i = 0; i <= halfSeg; i++) {
            float angle = step * i;
            float cs = cosf(angle), sn = sinf(angle);
            Vec3 wp = {
                botWorld.x + (arcPerp.x * cs - wDir.x * sn) * worldR,
                botWorld.y + (arcPerp.y * cs - wDir.y * sn) * worldR,
                botWorld.z + (arcPerp.z * cs - wDir.z * sn) * worldR
            };
            ImVec2 sp;
            bool ok = w2s(cam, wp, sp);
            if (ok && prevOK) out.push_back({prevSP, sp, color_capsule});
            prevSP = sp; prevOK = ok;
        }
    }
}

// =====================================================================
// doFrameUpdate — 每帧纯内存读取，生成线段
// =====================================================================

void PhysXVisualSystem::doFrameUpdate() {
    auto cam = g_il2cpp_tool->invokeMethod(camera_get_main, nullptr, nullptr);
    if (!alive(cam)) {
        std::lock_guard<std::mutex> lock(mtx);
        cached_colliders.clear();
        frame_lines.clear();
        scan_timer = 0;
        return;
    }

    // 屏幕尺寸
    if (camera_get_pixel_width && camera_get_pixel_height) {
        typedef int(*Fn)(Il2CppObject_API*, const MethodInfo*);
        screen_width  = (float)((Fn)camera_get_pixel_width->methodPointer)(cam, camera_get_pixel_width);
        screen_height = (float)((Fn)camera_get_pixel_height->methodPointer)(cam, camera_get_pixel_height);
    }

    // 相机位置（用于距离裁剪）
    Vec3 camPos = {0, 0, 0};
    if (component_get_transform) {
        typedef Il2CppObject_API*(*TFn)(Il2CppObject_API*, const MethodInfo*);
        auto camTf = ((TFn)component_get_transform->methodPointer)(cam, component_get_transform);
        if (alive(camTf)) camPos = getPos(camTf);
    }

    std::vector<DrawWireLine> lines;

    {
        std::lock_guard<std::mutex> lock(mtx);

        for (auto& c : cached_colliders) {
            if (!alive(c.transform) || !alive(c.collider)) continue;

            // 类型过滤
            if (c.type == ColliderType::Box     && !show_box)     continue;
            if (c.type == ColliderType::Sphere   && !show_sphere)  continue;
            if (c.type == ColliderType::Capsule  && !show_capsule) continue;

            // 距离裁剪
            Vec3 objPos = getPos(c.transform);
            if (vec3Dist(camPos, objPos) > max_distance) continue;

            switch (c.type) {
            case ColliderType::Box:
                generateBoxWireframe(cam, c, c.transform, lines);
                break;
            case ColliderType::Sphere:
                generateSphereWireframe(cam, c, c.transform, lines);
                break;
            case ColliderType::Capsule:
                generateCapsuleWireframe(cam, c, c.transform, lines);
                break;
            }
        }
    }

    frame_lines = std::move(lines);
}

// =====================================================================
// update / refresh / draw
// =====================================================================

void PhysXVisualSystem::update(float dt) {
    if (!enabled) {  // 不启用时清除数据并返回
        {
            std::lock_guard<std::mutex> lock(mtx);
            cached_colliders.clear();
            frame_lines.clear();
        }
        return;
    }
    
    if (!methods_cached) { initialize(); return; }

    scan_timer += dt;
    if (scan_timer >= scan_interval && !scanning.load()) {
        scan_timer = 0;
        scanning.store(true);
        if (scan_thread.joinable()) scan_thread.join();
        scan_thread = std::thread(&PhysXVisualSystem::doScanThread, this);
    }

    doFrameUpdate();
}


void PhysXVisualSystem::refresh() { scan_timer = scan_interval; }

void PhysXVisualSystem::drawColliders() {
    if (!enabled) return;  // 不启用时不绘制
    
    auto* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;

    for (auto& ln : frame_lines) {
        // 提取坐标
        float x1 = ln.from.x;
        float y1 = ln.from.y;
        float x2 = ln.to.x;
        float y2 = ln.to.y;
        
        // 从 ImU32 提取 RGBA
        ImVec4 color = ImGui::ColorConvertU32ToFloat4(ln.color);
        float r = color.x;  // R
        float g = color.y;  // G
        float b = color.z;  // B
        float a = color.w;  // A
        
        // 调用 drawLine
        GLBindlessRenderer::instance().drawLine(x1, y1, x2, y2, r, g, b, a, 1.5f);

    }
}


// =====================================================================
// 控制面板
// =====================================================================

void PhysXVisualSystem::drawControlPanel() {
    std::lock_guard<std::mutex> lock(mtx);

    // 在主面板最上方添加启用/禁用开关
    if (ImGui::Checkbox("启用 PhysX 可视化", &enabled)) {
        if (!enabled) {
            // 关闭时清除缓存
            cached_colliders.clear();
            frame_lines.clear();
        }
    }
    ImGui::Separator();

    if (!enabled) {
        ImGui::Text("PhysX 可视化已禁用");
        return;  // 禁用时不显示其他选项
    }
    
    if (!methods_cached) {
        ImGui::TextColored(ImVec4(1, .3f, .3f, 1), "PhysX 可视化未初始化");
        if (ImGui::Button("初始化##physx")) initialize();
        return;
    }

    
    ImGui::Separator();

    ImGui::SliderFloat("扫描间隔##px", &scan_interval, 2.f, 30.f, "%.0f s");
    ImGui::SliderFloat("最大距离##px",  &max_distance, 10.f, 500.f, "%.0f m");
    ImGui::SliderInt("圆弧精度##px",    &wireframe_segments, 8, 32);

    if (ImGui::Button("刷新##px")) refresh();
    ImGui::SameLine();
    ImGui::Text("总计: %zu", cached_colliders.size());
    ImGui::Separator();

    // 类型开关 + 颜色
    {
        ImGui::Checkbox("BoxCollider",     &show_box);
        ImGui::SameLine();
        ImGui::Text("(%d)", stat_box_count);
        ImGui::SameLine();
        ImVec4 cbox = ImGui::ColorConvertU32ToFloat4(color_box);
        if (ImGui::ColorEdit4("##cbox", &cbox.x, ImGuiColorEditFlags_NoInputs))
            color_box = ImGui::ColorConvertFloat4ToU32(cbox);
    }
    {
        ImGui::Checkbox("SphereCollider",  &show_sphere);
        ImGui::SameLine();
        ImGui::Text("(%d)", stat_sphere_count);
        ImGui::SameLine();
        ImVec4 csph = ImGui::ColorConvertU32ToFloat4(color_sphere);
        if (ImGui::ColorEdit4("##csph", &csph.x, ImGuiColorEditFlags_NoInputs))
            color_sphere = ImGui::ColorConvertFloat4ToU32(csph);
    }
    {
        ImGui::Checkbox("CapsuleCollider", &show_capsule);
        ImGui::SameLine();
        ImGui::Text("(%d)", stat_capsule_count);
        ImGui::SameLine();
        ImVec4 ccap = ImGui::ColorConvertU32ToFloat4(color_capsule);
        if (ImGui::ColorEdit4("##ccap", &ccap.x, ImGuiColorEditFlags_NoInputs))
            color_capsule = ImGui::ColorConvertFloat4ToU32(ccap);
    }

    ImGui::Separator();
    ImGui::Checkbox("仅显示已启用", &only_show_enabled);
}