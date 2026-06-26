#include "player_esp.h"
#include "ImGuiInputIME.h"
#include "dobby.h"
#include "item_esp.h"
#include "aimbot.h"
#include "gl_bindless_renderer.h"
#include "t3manager.h"

#include <algorithm>
#include <cstring>
#include <cctype>
#include <unordered_set>
 
PlayerESP g_player_esp;
void* PlayerESP::original_op_inequality_func = nullptr;

struct BoneConn { const char* from; const char* to; };
static const BoneConn g_conns[] = {
    {"Head","Neck"},{"Neck","Spine2"},{"Spine2","Spine1"},{"Spine1","Spine"},{"Spine","Pelvis"},
    {"Spine2","L_Clavicle"},{"L_Clavicle","L_UpperArm"},
    {"L_UpperArm","L_LowerArm"},{"L_LowerArm","L_Hand"},
    {"Spine2","R_Clavicle"},{"R_Clavicle","R_UpperArm"},
    {"R_UpperArm","R_LowerArm"},{"R_LowerArm","R_Hand"},
    {"Pelvis","L_Thigh"},{"L_Thigh","L_Calf"},{"L_Calf","L_Foot"},
    {"Pelvis","R_Thigh"},{"R_Thigh","R_Calf"},{"R_Calf","R_Foot"},
};
static const int g_conn_count = sizeof(g_conns)/sizeof(g_conns[0]);

static const struct { const char* key; const char* names[4]; } g_bone_match[] = {
    {"Head",       {"Bip001 Head","Bip002 Head",nullptr}},
    {"Neck",       {"Bip001 Neck","Bip002 Neck",nullptr}},
    {"Spine2",     {"Bip001 Spine2","Bip002 Spine2",nullptr}},
    {"Spine1",     {"Bip001 Spine1","Bip002 Spine1",nullptr}},
    {"Spine",      {"Bip001 Spine","Bip002 Spine",nullptr}},
    {"Pelvis",     {"Bip001 Pelvis","Bip002 Pelvis",nullptr}},
    {"L_Clavicle", {"Bip001 L Clavicle","Bip002 L Clavicle",nullptr}},
    {"L_UpperArm", {"Bip001 L UpperArm","Bip002 L UpperArm",nullptr}},
    {"L_LowerArm", {"Bip001 L Forearm","Bip002 L Forearm",nullptr}},
    {"L_Hand",     {"Bip001 L Hand","Bip002 L Hand",nullptr}},
    {"R_Clavicle", {"Bip001 R Clavicle","Bip002 R Clavicle",nullptr}},
    {"R_UpperArm", {"Bip001 R UpperArm","Bip002 R UpperArm",nullptr}},
    {"R_LowerArm", {"Bip001 R Forearm","Bip002 R Forearm",nullptr}},
    {"R_Hand",     {"Bip001 R Hand","Bip002 R Hand",nullptr}},
    {"L_Thigh",    {"Bip001 L Thigh","Bip002 L Thigh",nullptr}},
    {"L_Calf",     {"Bip001 L Calf","Bip002 L Calf",nullptr}},
    {"L_Foot",     {"Bip001 L Foot","Bip002 L Foot",nullptr}},
    {"R_Thigh",    {"Bip001 R Thigh","Bip002 R Thigh",nullptr}},
    {"R_Calf",     {"Bip001 R Calf","Bip002 R Calf",nullptr}},
    {"R_Foot",     {"Bip001 R Foot","Bip002 R Foot",nullptr}},
};
static const int g_bone_match_count = sizeof(g_bone_match)/sizeof(g_bone_match[0]);

// =====================================================================

PlayerESP::PlayerESP()
    : methods_cached(false),
      camera_class(nullptr), transform_class(nullptr),
      component_class(nullptr), physics_class(nullptr), object_class(nullptr),
      camera_get_main(nullptr), camera_world_to_screen(nullptr),
      camera_get_pixel_width(nullptr), camera_get_pixel_height(nullptr),
      transform_get_position(nullptr), transform_get_child_count(nullptr),
      transform_get_child(nullptr), component_get_transform(nullptr),
      object_get_name(nullptr), object_op_implicit(nullptr), object_op_inequality(nullptr),
      physics_raycast_method(nullptr), raycast_cached(false),
      op_inequality_hooked(false),
      core_instance(nullptr), core_found(false),
      debug_self_team(0), debug_player_count(0),
      enabled(true),
      show_bones(true), show_names(true), show_health(true), show_distance(true),
      show_teammates(false), show_ai(false), show_self(false),
      occlusion_enabled(false), occlusion_layer_mask(1),
      max_esp_distance(200.0f),
      color_enemy_visible(IM_COL32(255,40,40,220)),
      color_enemy_occluded(IM_COL32(255,140,0,180)),
      color_teammate_visible(IM_COL32(40,140,255,220)),
      color_teammate_occluded(IM_COL32(40,80,160,150)),
      color_ai(IM_COL32(255,255,0,180)),
      color_self(IM_COL32(255,255,255,150)),
      screen_width(1920), screen_height(1080),
      raycast_generation(0) {}

PlayerESP::~PlayerESP() {
    if (op_inequality_hooked && object_op_inequality) {
        void* addr = object_op_inequality->methodPointer;
        if (addr) DobbyDestroy(addr);
        op_inequality_hooked = false;
    }
}

// =====================================================================
// op_Inequality hook — Raycast 在这里执行（Unity 主线程，安全）
// =====================================================================

bool PlayerESP::hooked_op_inequality(void* x, void* y) {
    // 先执行 raycast 队列
    g_player_esp.executeRaycastQueue();

    // 调用原始 op_Inequality
    if (original_op_inequality_func) {
        typedef bool (*OrigFn)(void*, void*);
        return ((OrigFn)original_op_inequality_func)(x, y);
    }
    return x != y;
}

bool PlayerESP::hookOpInequality() {
    if (op_inequality_hooked) return true;
    if (!object_op_inequality || !object_op_inequality->methodPointer) return false;

    void* addr = object_op_inequality->methodPointer;
    int ret = DobbyHook(addr, (void*)hooked_op_inequality, &original_op_inequality_func);
    if (ret == 0) {
        op_inequality_hooked = true;
        LOGI("[PlayerESP] op_Inequality hooked at %p", addr);
        return true;
    }
    LOGW("[PlayerESP] op_Inequality hook failed: %d", ret);
    return false;
}

// ★ 在 Unity 主线程执行所有 raycast 请求
void PlayerESP::executeRaycastQueue() {
    if (!raycast_cached || !physics_raycast_method || !occlusion_enabled) return;

    std::vector<RaycastRequest> requests;
    {
        std::lock_guard<std::mutex> lock(raycast_mtx);
        if (raycast_requests.empty()) return;
        requests = std::move(raycast_requests);
        raycast_requests.clear();
    }

    typedef bool(*RaycastFn)(Vec3,Vec3,float,int,const MethodInfo*);
    auto fn = (RaycastFn)physics_raycast_method->methodPointer;

    std::unordered_map<int, bool> newResults;

    for (auto& req : requests) {
        Vec3 cam = {req.camX, req.camY, req.camZ};
        Vec3 bone = {req.boneX, req.boneY, req.boneZ};

        float dx = bone.x-cam.x, dy = bone.y-cam.y, dz = bone.z-cam.z;
        float dist = sqrtf(dx*dx+dy*dy+dz*dz);
        if (dist < 0.5f || dist > 500.f) { newResults[req.boneIndex] = false; continue; }

        Vec3 dir = {dx/dist, dy/dist, dz/dist};
        float checkDist = dist - 0.3f;
        if (checkDist < 0.1f) { newResults[req.boneIndex] = false; continue; }

        bool hit = fn(cam, dir, checkDist, occlusion_layer_mask, physics_raycast_method);
        newResults[req.boneIndex] = hit;
    }

    {
        std::lock_guard<std::mutex> lock(raycast_mtx);
        raycast_results = std::move(newResults);
        raycast_generation.fetch_add(1);
    }
}


bool PlayerESP::autoFindCoreFromDump(const std::string& dumpPath) {
    std::ifstream file(dumpPath);
    if (!file.is_open()) {
        LOGW("[PlayerESP] 无法打开dump文件: %s", dumpPath.c_str());
        return false;
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) lines.push_back(line);
    file.close();

    LOGI("[PlayerESP] dump文件共 %zu 行，开始搜索...", lines.size());

    auto trim = [](std::string& s) {
        auto notSpace = [](unsigned char c) { return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
        s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    };

    auto contains = [](const std::string& s, const char* sub) {
        return s.find(sub) != std::string::npos;
    };

    int matchCount = 0;

    for (size_t i = 1; i < lines.size(); i++) {
        if (!contains(lines[i], "Dictionary.Enumerator")) continue;
        matchCount++;

        const auto& enumLine = lines[i];
        const auto& dictLine = lines[i - 1];

        // 上一行必须是Dictionary定义（不含Enumerator）
        if (!contains(dictLine, "Dictionary") || contains(dictLine, "Enumerator")) continue;

        // 提取 Enumerator<K, V> 中的 V
        size_t comma = enumLine.find(',');
        size_t end = comma != std::string::npos ? enumLine.find('>', comma) : std::string::npos;
        if (comma == std::string::npos || end == std::string::npos) continue;

        std::string secondParam = enumLine.substr(comma + 1, end - comma - 1);
        trim(secondParam);

        if (!contains(dictLine, secondParam.c_str())) continue;

        LOGD("[PlayerESP] 匹配%d：行%zu, 参数:%s", matchCount, i, secondParam.c_str());

        // 向上查找继承 System.Object 的类定义
        // 向上查找继承 System.Object 的类定义
        for (size_t j = i - 2; j > 0; j--) {
            if (!contains(lines[j], "class ")) continue;  // 不是class行，继续往上找

            bool hasObject = contains(lines[j], "System.Object") ||
                             (j + 1 < lines.size() && contains(lines[j + 1], "System.Object"));
            if (!hasObject) break;  // 找到class但不继承System.Object，放弃这个匹配

            size_t nameStart = lines[j].find("class ") + 6;
            while (nameStart < lines[j].size() && std::isspace(lines[j][nameStart])) nameStart++;
            size_t nameEnd = nameStart;
            while (nameEnd < lines[j].size() && !std::isspace(lines[j][nameEnd]) && lines[j][nameEnd] != ':') nameEnd++;

            if (nameEnd <= nameStart) break;

            core_class_name = lines[j].substr(nameStart, nameEnd - nameStart);
            trim(core_class_name);

            LOGI("[PlayerESP] 候选核心类: %s (行%zu)", core_class_name.c_str(), j);

            if (findCoreByClassName()) {
                LOGI("[PlayerESP] 核心类验证通过: %s", core_class_name.c_str());
                return true;
            }
            break;  // 找到class定义处理完毕，跳出内层循环
        }
    }

    LOGW("[PlayerESP] 共检查%d个匹配项，未找到有效核心类", matchCount);
    return false;
}


// =====================================================================
// 初始化
// =====================================================================

bool PlayerESP::initialize() {
    if (methods_cached) return true;
    
    if (!cacheMethods()) return false;
    
    // ★ 自动查找核心类
    if (!core_found) {
        // 尝试从dump文件自动查找
        // dump.cs 路径，根据你的项目调整
        const char* dumpPath = "/storage/emulated/0/Android/data/com.pi.czrxdfirst/dump.cs";
        if (!g_il2cpp_tool->dumpGameData(dumpPath)) {
        LOGE("[AutoFind] dumpGameData failed");
        return false;
    }
        // 检查dump文件是否存在
        std::ifstream testFile(dumpPath);
        if (testFile.good()) {
            testFile.close();
            LOGI("[PlayerESP] 开始从dump.cs自动查找核心类...");
            
            if (!autoFindCoreFromDump(dumpPath)) {
                // 自动查找失败，使用默认类名
                LOGW("[PlayerESP] 自动查找失败，使用默认类名");
                core_class_name = "BMBBPEKMDLI";
                findCoreByClassName();
            }
        } else {
            // dump文件不存在，使用默认类名
            LOGW("[PlayerESP] dump.cs不存在，使用默认类名");
            core_class_name = "BMBBPEKMDLI";
            findCoreByClassName();
        }
    }
    
    LOGI("[PlayerESP] Init OK (Raycast:%s op_Implicit:%s op_Inequality_hook:%s)",
         raycast_cached ? "Y" : "N",
         object_op_implicit ? "Y" : "N",
         op_inequality_hooked ? "Y" : "N");
    return true;
}

bool PlayerESP::cacheMethods() {
    if (!g_il2cpp_tool || !g_il2cpp_tool->isInitialized()) return false;

    camera_class    = g_il2cpp_tool->findClass("UnityEngine", "Camera");
    transform_class = g_il2cpp_tool->findClass("UnityEngine", "Transform");
    component_class = g_il2cpp_tool->findClass("UnityEngine", "Component");
    object_class    = g_il2cpp_tool->findClass("UnityEngine", "Object");
    if (!camera_class || !transform_class || !object_class) return false;

    camera_get_main         = g_il2cpp_tool->findMethod(camera_class, "get_main", 0);
    camera_world_to_screen  = g_il2cpp_tool->findMethod(camera_class, "WorldToScreenPoint", 1);
    camera_get_pixel_width  = g_il2cpp_tool->findMethod(camera_class, "get_pixelWidth", 0);
    camera_get_pixel_height = g_il2cpp_tool->findMethod(camera_class, "get_pixelHeight", 0);
    transform_get_position  = g_il2cpp_tool->findMethod(transform_class, "get_position", 0);
    transform_get_child_count = g_il2cpp_tool->findMethod(transform_class, "get_childCount", 0);
    transform_get_child     = g_il2cpp_tool->findMethod(transform_class, "GetChild", 1);
    if (component_class)
        component_get_transform = g_il2cpp_tool->findMethod(component_class, "get_transform", 0);
    object_get_name = g_il2cpp_tool->findMethod(object_class, "get_name", 0);
    object_op_implicit = g_il2cpp_tool->findMethod(object_class, "op_Implicit", 1);
    object_op_inequality = g_il2cpp_tool->findMethod(object_class, "op_Inequality", 2);

    if (!camera_get_main || !camera_world_to_screen || !transform_get_position) return false;

    methods_cached = true;
    cacheRaycast();

    // Hook op_Inequality 用于安全执行 Raycast
    if (object_op_inequality) {
        hookOpInequality();
    }

    LOGI("[PlayerESP] Init OK (Raycast:%s op_Implicit:%s op_Inequality_hook:%s)",
         raycast_cached ? "Y" : "N",
         object_op_implicit ? "Y" : "N",
         op_inequality_hooked ? "Y" : "N");
    return true;
}

bool PlayerESP::cacheRaycast() {
    physics_class = g_il2cpp_tool->findClass("UnityEngine", "Physics");
    if (!physics_class) return false;
    physics_raycast_method = g_il2cpp_tool->findMethod(physics_class, "Raycast", 4);
    if (physics_raycast_method && physics_raycast_method->methodPointer &&
        (uintptr_t)physics_raycast_method->methodPointer > 0x10000) {
        raycast_cached = true;
        return true;
    }
    return false;
}

bool PlayerESP::unityAlive(void* obj) {
    if (!alive(obj)) return false;
    if (!object_op_implicit || !object_op_implicit->methodPointer) return true;
    typedef bool (*OpImplicitFn)(void*, const MethodInfo*);
    return ((OpImplicitFn)object_op_implicit->methodPointer)(obj, object_op_implicit);
}

bool PlayerESP::verifyCoreInstance(void* instance) {
    if (!instance || (uintptr_t)instance < 0x10000) return false;
    if (!alive(instance)) return false;
    float valA = readMem<float>(instance, PlayerOffsets::Feature_Offset_A);
    float valB = readMem<float>(instance, PlayerOffsets::Feature_Offset_B);
    return fabsf(valA - PlayerOffsets::Feature_Value_A) < 0.001f &&
           fabsf(valB - PlayerOffsets::Feature_Value_B) < 0.001f;
}

bool PlayerESP::findCoreByClassName() {
    if (core_class_name.empty()) return false;
    auto klass = g_il2cpp_tool->findClass("", core_class_name);
    if (!klass) return false;
    void* iter = nullptr;
    FieldInfo* field = nullptr;
    while ((field = g_il2cpp_tool->getFields(klass, &iter)) != nullptr) {
        uint32_t flags = g_il2cpp_tool->getFieldFlags(field);
        if (!(flags & 0x10)) continue;
        const char* fname = g_il2cpp_tool->getFieldName(field);
        void* instance = g_il2cpp_tool->getStaticFieldValue<void*>(klass, std::string(fname ? fname : ""));
        if (!instance || (uintptr_t)instance < 0x10000) return false;
        core_instance = instance;
        core_found = true;
        g_item_esp.setCoreInstance(core_instance);
        //g_aimbot.setCoreInstance(g_player_esp.getCoreInstance());

        LOGI("[PlayerESP] Core: %p", instance);
        return true;
    }
    return false;
}

// =====================================================================
// 基础工具
// =====================================================================

std::string PlayerESP::readIl2CppString(void* strPtr) {
    if (!strPtr || (uintptr_t)strPtr < 0x10000) return "";
    int32_t len = *(int32_t*)((uint8_t*)strPtr + 0x10);
    if (len <= 0 || len > 256) return "";
    char16_t* chars = (char16_t*)((uint8_t*)strPtr + 0x14);
    std::string result;
    result.reserve(len * 3);
    for (int i = 0; i < len; i++) {
        char16_t c = chars[i];
        if (c < 0x80) result += (char)c;
        else if (c < 0x800) { result += (char)(0xC0|(c>>6)); result += (char)(0x80|(c&0x3F)); }
        else { result += (char)(0xE0|(c>>12)); result += (char)(0x80|((c>>6)&0x3F)); result += (char)(0x80|(c&0x3F)); }
    }
    return result;
}

PlayerESP::Vec3 PlayerESP::getPos(void* transform) {
    Vec3 p = {0,0,0};
    if (!alive(transform) || !transform_get_position) return p;
    typedef Vec3(*Fn)(void*, const MethodInfo*);
    p = ((Fn)transform_get_position->methodPointer)(transform, transform_get_position);
    if (std::isnan(p.x) || std::isnan(p.y) || std::isnan(p.z)) return {0,0,0};
    return p;
}

bool PlayerESP::w2s(void* cam, Vec3 w, ImVec2& out) {
    if (!alive(cam) || !camera_world_to_screen) return false;
    typedef Vec3(*Fn)(void*, Vec3, const MethodInfo*);
    Vec3 s = ((Fn)camera_world_to_screen->methodPointer)(cam, w, camera_world_to_screen);
    if (s.z < 0 || std::isnan(s.x) || std::isnan(s.y)) return false;
    out.x = s.x; out.y = screen_height - s.y;
    return out.x > -500 && out.x < screen_width+500 && out.y > -500 && out.y < screen_height+500;
}

float PlayerESP::vec3Dist(Vec3 a, Vec3 b) {
    float dx=a.x-b.x, dy=a.y-b.y, dz=a.z-b.z;
    return sqrtf(dx*dx+dy*dy+dz*dz);
}

PlayerRelation PlayerESP::calcRelation(bool isRealSelf, bool isAI, uint32_t teamId, uint32_t selfTeamId) {
    if (isRealSelf) return PlayerRelation::Self;
    if (isAI) return PlayerRelation::AI;
    if (selfTeamId != 0 && teamId == selfTeamId) return PlayerRelation::Teammate;
    return PlayerRelation::Enemy;
}

ImU32 PlayerESP::getColor(PlayerRelation rel, bool occluded) {
    switch (rel) {
        case PlayerRelation::Self:     return color_self;
        case PlayerRelation::Teammate: return occluded ? color_teammate_occluded : color_teammate_visible;
        case PlayerRelation::AI:       return color_ai;
        default:                       return occluded ? color_enemy_occluded : color_enemy_visible;
    }
}

// =====================================================================
// 安全骨骼收集（只在首次发现角色时调用）
// =====================================================================

void PlayerESP::collectBonesSafe(void* boneRoot, std::unordered_map<std::string, void*>& bones) {
    if (!boneRoot || !object_get_name || !transform_get_child_count || !transform_get_child)
        return;

    struct StackItem { void* node; int depth; };
    std::vector<StackItem> stack;
    std::unordered_set<void*> visited;
    int totalVisited = 0;

    stack.push_back({boneRoot, 0});

    typedef int(*GCCFn)(void*, const MethodInfo*);
    typedef void*(*GCFn)(void*, int, const MethodInfo*);

    while (!stack.empty() && totalVisited < 200 && bones.size() < 20) {
        auto item = stack.back();
        stack.pop_back();

        if (!item.node || item.depth > 12) continue;
        if (!alive(item.node)) continue;
        if (!visited.insert(item.node).second) continue;
        totalVisited++;

        auto nameR = g_il2cpp_tool->invokeMethod(object_get_name, (Il2CppObject_API*)item.node, nullptr);
        if (!nameR) continue;
        std::string name = g_il2cpp_tool->getString((Il2CppString_API*)nameR);

        for (int m = 0; m < g_bone_match_count; m++) {
            for (int k = 0; g_bone_match[m].names[k]; k++) {
                if (name == g_bone_match[m].names[k]) {
                    // ★ 已匹配过的 key 不覆盖
                    if (bones.find(g_bone_match[m].key) == bones.end()) {
                        bones[g_bone_match[m].key] = item.node;
                    }
                    goto matched;
                }
            }
        }
        matched:;

        int cc = ((GCCFn)transform_get_child_count->methodPointer)(item.node, transform_get_child_count);
        if (cc <= 0 || cc > 150) continue;

        for (int i = 0; i < cc; i++) {
            auto ch = ((GCFn)transform_get_child->methodPointer)(item.node, i, transform_get_child);
            if (ch && alive(ch) && visited.find(ch) == visited.end())
                stack.push_back({ch, item.depth + 1});
        }
    }
}

// =====================================================================
// 每帧更新
// Raycast 请求在这里提交，结果从上一帧读取
// =====================================================================

void PlayerESP::doFrameUpdate() {
    if (!enabled) return;

    void* cam = g_il2cpp_tool->invokeMethod(camera_get_main, nullptr, nullptr);
    if (!alive(cam)) {
        frame_labels.clear(); frame_bones.clear(); frame_hpbars.clear();
        debug_players.clear(); debug_player_count = 0;
        // ★ 不要清空 core_found 和 core_instance
        bone_cache.clear();
        return;
    }

    if (camera_get_pixel_width && camera_get_pixel_height) {
        typedef int(*Fn)(void*,const MethodInfo*);
        screen_width  = (float)((Fn)camera_get_pixel_width->methodPointer)(cam, camera_get_pixel_width);
        screen_height = (float)((Fn)camera_get_pixel_height->methodPointer)(cam, camera_get_pixel_height);
    }

    Vec3 camPos = {0,0,0};
    if (component_get_transform) {
        typedef void*(*TFn)(void*,const MethodInfo*);
        auto camTf = ((TFn)component_get_transform->methodPointer)(cam, component_get_transform);
        if (alive(camTf)) camPos = getPos(camTf);
    }

    // ★ 改为：只检查实例是否有效，不用 alive()
    if (!core_found || !core_instance || (uintptr_t)core_instance < 0x10000) {
        frame_labels.clear(); frame_bones.clear(); frame_hpbars.clear();
        return;
    }

    void* actorDic = readPtr(core_instance, PlayerOffsets::Terrain_ActorDic);
    if (!actorDic || (uintptr_t)actorDic < 0x10000) {
        frame_labels.clear(); frame_bones.clear(); frame_hpbars.clear();
        return;
    }
    

    void* entries = readPtr(actorDic, PlayerOffsets::Dict_Entries);
    int count = readMem<int>(actorDic, PlayerOffsets::Dict_Count);
    if (!entries || count <= 0 || count > 200) {
        frame_labels.clear(); frame_bones.clear(); frame_hpbars.clear();
        return;
    }

    uint32_t selfTeamId = 0;
    for (int i = 0; i < count; i++) {
        void* actor = readPtr(entries, PlayerOffsets::Dict_FirstValue + (i * PlayerOffsets::Dict_EntryStride));
        if (!actor || !alive(actor)) continue;
        if (readMem<bool>(actor, PlayerOffsets::Actor_isRealSelf)) {
            selfTeamId = readMem<uint32_t>(actor, PlayerOffsets::Actor_teamId);
            break;
        }
    }

    // 读取上一帧的 raycast 结果
    std::unordered_map<int, bool> prevRaycastResults;
    bool doOcc = occlusion_enabled && raycast_cached && op_inequality_hooked;
    if (doOcc) {
        std::lock_guard<std::mutex> lock(raycast_mtx);
        prevRaycastResults = raycast_results;
    }

    // 本帧新的 raycast 请求
    std::vector<RaycastRequest> newRequests;
    int boneGlobalIndex = 0;

    std::unordered_set<void*> activeActors;
    std::vector<PlayerDrawLabel> labels;
    std::vector<PlayerDrawBone>  boneLines;
    std::vector<PlayerDrawHP>    hpbars;
    std::vector<DebugPlayerInfo> debugList;

    for (int i = 0; i < count; i++) {
        void* actor = readPtr(entries, PlayerOffsets::Dict_FirstValue + (i * PlayerOffsets::Dict_EntryStride));
        if (!actor || !alive(actor)) continue;

        bool isRealSelf = readMem<bool>(actor, PlayerOffsets::Actor_isRealSelf);
        bool isAI       = readMem<bool>(actor, PlayerOffsets::Actor_isAI);
        uint32_t teamId = readMem<uint32_t>(actor, PlayerOffsets::Actor_teamId);
        int32_t hp      = readMem<int32_t>(actor, PlayerOffsets::Actor_HP);
        int32_t maxHp   = readMem<int32_t>(actor, PlayerOffsets::Actor_MaxHP);

        PlayerRelation relation = calcRelation(isRealSelf, isAI, teamId, selfTeamId);
        if (relation == PlayerRelation::Self && !show_self) continue;
        if (relation == PlayerRelation::Teammate && !show_teammates) continue;
        if (relation == PlayerRelation::AI && !show_ai) continue;

        void* characterTf = readPtr(actor, PlayerOffsets::Actor_Character);
        if (!unityAlive(characterTf)) continue;

        Vec3 pos = getPos(characterTf);
        if (pos.x == 0 && pos.y == 0 && pos.z == 0) continue;
        float dist = vec3Dist(camPos, pos);
        if (dist > max_esp_distance) continue;

        ImVec2 screenPos;
        if (!w2s(cam, pos, screenPos)) continue;

        activeActors.insert(actor);

        void* nameStr = readPtr(actor, PlayerOffsets::Actor_playerName);
        std::string name = readIl2CppString(nameStr);
        ImU32 baseColor = getColor(relation, false);

        Vec3 headPos = {pos.x, pos.y + 2.0f, pos.z};
        ImVec2 headScreen;
        bool headOK = w2s(cam, headPos, headScreen);

        if (show_names && headOK && !name.empty()) {
            std::string label = name;
            if (show_distance) { char buf[32]; snprintf(buf,sizeof(buf)," [%.0fm]",dist); label+=buf; }
            labels.push_back({headScreen, label, baseColor});
        }

        if (show_health && headOK && maxHp > 0)
            hpbars.push_back({ImVec2(headScreen.x, headScreen.y-18), (float)hp/(float)maxHp, name, dist, relation});

        // 骨骼
        size_t boneCount = 0;
        if (show_bones) {
            auto cacheIt = bone_cache.find(actor);
            if (cacheIt == bone_cache.end()) {
                void* boneRootTf = readPtr(actor, PlayerOffsets::Actor_BoneRoot);
                if (alive(boneRootTf)) {
                    std::unordered_map<std::string, void*> nb;
                    collectBonesSafe(boneRootTf, nb);
                    bone_cache[actor] = std::move(nb);
                    cacheIt = bone_cache.find(actor);
                } else {
                    bone_cache[actor] = {};
                    continue;
                }
            }

            if (cacheIt != bone_cache.end() && cacheIt->second.size() >= 6) {
                auto& cachedBones = cacheIt->second;
                boneCount = cachedBones.size();

                struct NS { Vec3 wp; ImVec2 sp; bool on; bool occ; int idx; };
                std::unordered_map<std::string, NS> ns;

                for (auto& bp : cachedBones) {
                    if (!unityAlive(bp.second)) continue;
                    NS n;
                    n.wp = getPos(bp.second);
                    if (n.wp.x == 0 && n.wp.y == 0 && n.wp.z == 0) continue;
                    if (vec3Dist(n.wp, pos) > 20.0f) continue;
                    n.on = w2s(cam, n.wp, n.sp);
                    n.idx = boneGlobalIndex++;

                    // 从上一帧结果读遮挡状态
                    n.occ = false;
                    if (doOcc) {
                        auto rIt = prevRaycastResults.find(n.idx);
                        if (rIt != prevRaycastResults.end()) n.occ = rIt->second;

                        // 提交本帧 raycast 请求
                        if (n.on) {
                            newRequests.push_back({camPos.x, camPos.y, camPos.z,
                                                   n.wp.x, n.wp.y, n.wp.z, n.idx});
                        }
                    }

                    ns[bp.first] = n;
                }

                // 在连线循环之前，先算出角色骨架的参考高度
                float skeletonHeight = 0.0f;
                {
                    auto headIt = ns.find("Head");
                    auto footIt = ns.find("L_Foot");
                    if (footIt == ns.end()) footIt = ns.find("R_Foot");
                    if (headIt != ns.end() && footIt != ns.end()) {
                        skeletonHeight = vec3Dist(headIt->second.wp, footIt->second.wp);
                    }
                }
                // 如果算不出来就给个默认值
                if (skeletonHeight < 0.5f) skeletonHeight = 1.8f;
                
                // 相邻骨骼间距不应超过骨架高度的 40%
                float maxBoneDist = skeletonHeight * 0.4f;
                
                for (int c = 0; c < g_conn_count; c++) {
                    auto f = ns.find(g_conns[c].from), t = ns.find(g_conns[c].to);
                    if (f == ns.end() || t == ns.end()) continue;
                    if (!f->second.on || !t->second.on) continue;
                
                    // ★ 用动态阈值过滤异常连线
                    float boneDist3D = vec3Dist(f->second.wp, t->second.wp);
                    if (boneDist3D > maxBoneDist) continue;
                
                    float sdx = f->second.sp.x - t->second.sp.x;
                    float sdy = f->second.sp.y - t->second.sp.y;
                    if (sqrtf(sdx*sdx + sdy*sdy) > screen_height * 0.5f) continue;
                
                    bool occ = f->second.occ || t->second.occ;
                    boneLines.push_back({f->second.sp, t->second.sp, getColor(relation, occ), 2.0f});
                }
            }
        }

        const char* relStr = "?";
        switch(relation) {
            case PlayerRelation::Self: relStr="自己"; break;
            case PlayerRelation::Teammate: relStr="队友"; break;
            case PlayerRelation::Enemy: relStr="敌人"; break;
            case PlayerRelation::AI: relStr="AI"; break;
            default: break;
        }
        debugList.push_back({name, relStr, hp, maxHp, teamId, boneCount});
    }

    // 提交 raycast 请求给 op_Inequality hook 执行
    if (doOcc && !newRequests.empty()) {
        std::lock_guard<std::mutex> lock(raycast_mtx);
        raycast_requests = std::move(newRequests);
    }

    // 清理过期缓存
    for (auto it = bone_cache.begin(); it != bone_cache.end(); )
        if (activeActors.find(it->first) == activeActors.end()) it = bone_cache.erase(it); else ++it;

    frame_labels  = std::move(labels);
    frame_bones   = std::move(boneLines);
    frame_hpbars  = std::move(hpbars);
    debug_players = std::move(debugList);
    debug_self_team = selfTeamId;
    debug_player_count = count;
}

void PlayerESP::update(float dt) {
    if (!methods_cached) { initialize(); return; }
    if (!enabled) return;
    doFrameUpdate();
}

void PlayerESP::refresh() { bone_cache.clear(); }

// =====================================================================
// drawESP / drawControlPanel
// =====================================================================

void PlayerESP::drawESP() {
    if (!enabled) return;
    auto* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;

    for (auto& hp : frame_hpbars) {
        float bW=50, bH=5;
        ImVec2 tl(hp.pos.x-bW*.5f, hp.pos.y), br(hp.pos.x+bW*.5f, hp.pos.y+bH);
        dl->AddRectFilled(tl, br, IM_COL32(0,0,0,160));
        ImU32 hc = hp.ratio>.6f ? IM_COL32(0,255,0,220) : hp.ratio>.3f ? IM_COL32(255,255,0,220) : IM_COL32(255,0,0,220);
        dl->AddRectFilled(tl, ImVec2(tl.x+bW*hp.ratio, br.y), hc);
        dl->AddRect(tl, br, IM_COL32(255,255,255,100));
    }
    for (auto& lb : frame_labels) {
        ImVec2 ts=ImGui::CalcTextSize(lb.text.c_str()), tp(lb.pos.x-ts.x*.5f, lb.pos.y-ts.y-22);
        dl->AddText(ImVec2(tp.x+1,tp.y+1), IM_COL32(0,0,0,200), lb.text.c_str());
        dl->AddText(tp, lb.color, lb.text.c_str());
    }
    for (auto& bl : frame_bones) {
        // 提取坐标
        float x1 = bl.from.x;
        float y1 = bl.from.y;
        float x2 = bl.to.x;
        float y2 = bl.to.y;
        
        // 从 ImU32 提取 RGBA
        ImVec4 color = ImGui::ColorConvertU32ToFloat4(bl.color);
        float r = color.x;  // R
        float g = color.y;  // G
        float b = color.z;  // B
        float a = color.w;  // A
        
        // 调用 drawLine
        //GLBindlessRenderer::instance().drawLine(x1, y1, x2, y2, r, g, b, a, 1.5f);

        dl->AddLine(bl.from, bl.to, bl.color, bl.thickness);
        dl->AddCircleFilled(bl.from, 2.0f, bl.color);
        dl->AddCircleFilled(bl.to, 2.0f, bl.color);
    }
}

void PlayerESP::drawControlPanel() {
    if (!methods_cached) {
        ImGui::TextColored(ImVec4(1,.3f,.3f,1), "PlayerESP 未初始化");
        if (ImGui::Button("初始化##pesp")) initialize();
        return;
    }
    ImGui::Checkbox("启用##pesp", &enabled);
    if (!enabled) return;

    ImGui::Separator();
    if (core_found && core_instance) {
        ImGui::TextColored(ImVec4(0,1,0,1), "核心类: %p", core_instance);
        if (!core_class_name.empty())
            ImGui::SameLine(), ImGui::TextColored(ImVec4(.7f,.7f,.7f,1), "(%s)", core_class_name.c_str());
        if (ImGui::Button("重新查找##core")) { core_found=false; core_instance=nullptr; bone_cache.clear(); }
    } else {
        ImGui::TextColored(ImVec4(1,.3f,.3f,1), "核心类未找到");
        static char clsName[128]="BMBBPEKMDLI";
        ImGui::SetNextItemWidth(200);
        ImGuiIME::InputText("类名##cls", clsName, sizeof(clsName), 400);
        ImGui::SameLine();
        if (ImGui::Button("查找##cls")) { core_class_name=clsName; findCoreByClassName(); }
        static char addrBuf[32]="";
        ImGui::SetNextItemWidth(180);
        ImGuiIME::InputText("地址##addr", addrBuf, sizeof(addrBuf), 400);
        ImGui::SameLine();
        if (ImGui::Button("设置##addr")) {
            uintptr_t addr=strtoull(addrBuf,nullptr,16);
            if (addr>0x10000) { core_instance=(void*)addr; core_found=true; }
        }
    }

    ImGui::Separator();
    ImGui::Text("玩家:%d 队伍:%u 骨骼缓存:%zu", debug_player_count, debug_self_team, bone_cache.size());
    ImGui::SliderFloat("最大距离##pesp", &max_esp_distance, 20.f, 500.f, "%.0f m");

    if (ImGui::Button("刷新骨骼##pesp")) bone_cache.clear();

    ImGui::Separator();
    ImGui::Checkbox("骨骼##pesp", &show_bones); ImGui::SameLine();
    ImGui::Checkbox("名称##pesp", &show_names); ImGui::SameLine();
    ImGui::Checkbox("血条##pesp", &show_health);
    ImGui::Checkbox("距离##pesp", &show_distance); ImGui::SameLine();
    ImGui::Checkbox("队友##pesp", &show_teammates); ImGui::SameLine();
    ImGui::Checkbox("AI##pesp", &show_ai); ImGui::SameLine();
    ImGui::Checkbox("自己##pesp", &show_self);

    // 掩体检测
    if (raycast_cached && op_inequality_hooked) {
        ImGui::Separator();
        ImGui::Checkbox("掩体检测##occ", &occlusion_enabled);
        if (occlusion_enabled) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0,1,0,1), "(通过op_Inequality安全执行)");
        }
    } else if (raycast_cached && !op_inequality_hooked) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1,.5f,0,1), "op_Inequality hook 未就绪");
        if (ImGui::SmallButton("重试Hook##occ")) hookOpInequality();
    }

    ImGui::Separator();
    auto cBtn = [](const char* l, ImU32& c) {
        ImVec4 v=ImGui::ColorConvertU32ToFloat4(c);
        if (ImGui::ColorEdit4(l,&v.x,ImGuiColorEditFlags_NoInputs)) c=ImGui::ColorConvertFloat4ToU32(v);
    };
    cBtn("敌人可见##c", color_enemy_visible); ImGui::SameLine(); cBtn("敌人遮挡##c", color_enemy_occluded);
    cBtn("队友可见##c", color_teammate_visible); ImGui::SameLine(); cBtn("队友遮挡##c", color_teammate_occluded);
    cBtn("AI##c", color_ai); ImGui::SameLine(); cBtn("自己##c", color_self);

    if (ImGui::CollapsingHeader("调试##pesp")) {
        if (core_instance) {
            void* dic=readPtr(core_instance, PlayerOffsets::Terrain_ActorDic);
            ImGui::Text("ActorDic: %p", dic);
            if (dic && (uintptr_t)dic>0x10000)
                ImGui::Text("  entries:%p count:%d", readPtr(dic,PlayerOffsets::Dict_Entries), readMem<int>(dic,PlayerOffsets::Dict_Count));
        }
        ImGui::Separator();
        for (size_t i=0; i<debug_players.size()&&i<20; i++) {
            auto& p=debug_players[i];
            ImGui::Text("[%zu] %s|%s|HP:%d/%d|T:%u|B:%zu", i, p.name.c_str(), p.relation, p.hp, p.maxHp, p.teamId, p.boneCount);
        }
    }
}

// =====================================================================
// 获取自己的 actor 指针
// =====================================================================

void* PlayerESP::getSelfActor() {
    if (!core_found || !core_instance || (uintptr_t)core_instance < 0x10000)
        return nullptr;

    // 获取 ActorDic
    void* actorDic = readPtr(core_instance, PlayerOffsets::Terrain_ActorDic);
    if (!actorDic || (uintptr_t)actorDic < 0x10000)
        return nullptr;

    // 遍历 Dictionary entries
    void* entries = readPtr(actorDic, PlayerOffsets::Dict_Entries);
    int count = readMem<int>(actorDic, PlayerOffsets::Dict_Count);
    if (!entries || count <= 0 || count > 200)
        return nullptr;

    for (int i = 0; i < count; i++) {
        void* actor = readPtr(entries, PlayerOffsets::Dict_FirstValue + (i * PlayerOffsets::Dict_EntryStride));
        
        if (!actor || !alive(actor))
            continue;

        // 检查 isRealSelf 标记 (offset 0xA8)
        bool isRealSelf = readMem<bool>(actor, PlayerOffsets::Actor_isRealSelf);
        if (isRealSelf)
            return actor;
    }

    return nullptr;
}