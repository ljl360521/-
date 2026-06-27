#include "esp_system.h"
#include <algorithm>
#include <cstring>
#include <cctype>
#include <cinttypes>

ESPSystem g_esp;

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

static bool strCI(const std::string& s, const char* sub) {
    if (!sub) return false;
    std::string ls=s, lsub=sub;
    for(auto&c:ls) c=tolower(c);
    for(auto&c:lsub) c=tolower(c);
    return ls.find(lsub)!=std::string::npos;
}

bool ESPSystem::isBoneNode(const std::string& n) {
    if (n.size()<3) return false;
    if (n.compare(0,6,"Bip001")==0||n.compare(0,6,"Bip002")==0) return true;
    if (n.compare(0,5,"Bip01")==0||n.compare(0,5,"Bip02")==0) return true;
    if (n.compare(0,5,"Bone_")==0||n.compare(0,5,"Dummy")==0) return true;
    return false;
}


ESPSystem::ESPSystem()
    : methods_cached(false),
      camera_class(nullptr), transform_class(nullptr),
      component_class(nullptr), object_class(nullptr), monobehaviour_class(nullptr),
      camera_get_main(nullptr), camera_world_to_screen(nullptr),
      camera_get_pixel_width(nullptr), camera_get_pixel_height(nullptr),
      transform_get_position(nullptr), transform_get_child_count(nullptr),
      transform_get_child(nullptr), component_get_transform(nullptr),
      object_find_objects_of_type(nullptr), object_get_name(nullptr),
      physics_class(nullptr), physics_linecast(nullptr),
      physics_linecast_param_count(0), 
      occlusion_methods_cached(false),
      occlusion_enabled(true),
      occlusion_layer_mask(1),           
      color_visible(IM_COL32(0,255,100,200)),
      color_occluded(IM_COL32(255,40,40,200)),
      skeleton_enabled(false),
      screen_width(1920), screen_height(1080),  
      scan_timer(0), scan_interval(5.0f), scanning(false),
      // ===== 新增开关默认值 =====
      show_transform_addr(false),
      show_component_addr(false),
      show_world_pos(false),
      show_screen_pos(false) {}



ESPSystem::~ESPSystem() {
    if (scan_thread.joinable()) scan_thread.join();
}

bool ESPSystem::initialize() {
    if (methods_cached) return true;
    return cacheMethods();
}

bool ESPSystem::cacheMethods() {
    if (!g_il2cpp_tool||!g_il2cpp_tool->isInitialized()) return false;
    camera_class=g_il2cpp_tool->findClass("UnityEngine","Camera");
    transform_class=g_il2cpp_tool->findClass("UnityEngine","Transform");
    object_class=g_il2cpp_tool->findClass("UnityEngine","Object");
    component_class=g_il2cpp_tool->findClass("UnityEngine","Component");
    monobehaviour_class=g_il2cpp_tool->findClass("UnityEngine","MonoBehaviour");
    if (!camera_class||!transform_class||!object_class) return false;

    camera_get_main=g_il2cpp_tool->findMethod(camera_class,"get_main",0);
    camera_world_to_screen=g_il2cpp_tool->findMethod(camera_class,"WorldToScreenPoint",1);
    camera_get_pixel_width=g_il2cpp_tool->findMethod(camera_class,"get_pixelWidth",0);
    camera_get_pixel_height=g_il2cpp_tool->findMethod(camera_class,"get_pixelHeight",0);
    transform_get_position=g_il2cpp_tool->findMethod(transform_class,"get_position",0);
    transform_get_child_count=g_il2cpp_tool->findMethod(transform_class,"get_childCount",0);
    transform_get_child=g_il2cpp_tool->findMethod(transform_class,"GetChild",1);
    if (component_class)
        component_get_transform=g_il2cpp_tool->findMethod(component_class,"get_transform",0);
    object_find_objects_of_type=g_il2cpp_tool->findMethod(object_class,"FindObjectsOfType",1);
    object_get_name=g_il2cpp_tool->findMethod(object_class,"get_name",0);

    if (!camera_get_main||!camera_world_to_screen||!transform_get_position||
        !object_find_objects_of_type||!object_get_name) return false;
    methods_cached=true;

    cacheOcclusionMethods();

    LOGI("[ESP] Init OK (Occlusion: %s)", occlusion_methods_cached ? "Y" : "N");
    return true;
}


bool ESPSystem::cacheOcclusionMethods() {
    if (occlusion_methods_cached) return true;

    physics_class = g_il2cpp_tool->findClass("UnityEngine", "Physics");
    if (!physics_class) {
        LOGW("[ESP] Physics class not found");
        return false;
    }

    physics_linecast = g_il2cpp_tool->findMethod(physics_class, "Raycast", 4);
    if (physics_linecast && physics_linecast->methodPointer &&
        (uintptr_t)physics_linecast->methodPointer > 0x10000) {
        physics_linecast_param_count = 4;
        occlusion_methods_cached = true;
        LOGI("[ESP] Raycast(4) ptr=%p", physics_linecast->methodPointer);
        return true;
    }

    LOGW("[ESP] Raycast(4) not found, fallback disabled");
    return false;
}


bool ESPSystem::linecastSafe(Vec3 camPos, Vec3 bonePos) {
    if (!occlusion_methods_cached || !physics_linecast) return false;

    float dx = bonePos.x - camPos.x;
    float dy = bonePos.y - camPos.y;
    float dz = bonePos.z - camPos.z;
    float dist = sqrtf(dx*dx + dy*dy + dz*dz);
    if (dist < 0.5f) return false;

    Vec3 dir = { dx/dist, dy/dist, dz/dist };
    float checkDist = dist - 0.3f;
    if (checkDist < 0.1f) return false;

    try {
        typedef bool (*RaycastFn)(Vec3, Vec3, float, int, const MethodInfo*);
        bool hit = ((RaycastFn)physics_linecast->methodPointer)(
            camPos, dir, checkDist, occlusion_layer_mask, physics_linecast);

        static int logCount = 0;
        if (logCount < 10) {
            LOGI("[ESP] Raycast: hit=%d dist=%.1f dir=(%.2f,%.2f,%.2f) cam=(%.1f,%.1f,%.1f)",
                 hit, checkDist, dir.x, dir.y, dir.z,
                 camPos.x, camPos.y, camPos.z);
            logCount++;
        }

        return hit;
    } catch (...) {
        return false;
    }
    return false;
}

ESPSystem::Vec3 ESPSystem::getPos(Il2CppObject_API* t) {
    Vec3 p={0,0,0};
    if (!alive(t)||!transform_get_position) return p;
    typedef Vec3(*Fn)(Il2CppObject_API*,const MethodInfo*);
    return ((Fn)transform_get_position->methodPointer)(t,transform_get_position);
}

bool ESPSystem::w2s(Il2CppObject_API* cam, Vec3 w, ImVec2& out) {
    if (!alive(cam)||!camera_world_to_screen) return false;
    typedef Vec3(*Fn)(Il2CppObject_API*,Vec3,const MethodInfo*);
    Vec3 s=((Fn)camera_world_to_screen->methodPointer)(cam,w,camera_world_to_screen);
    if (s.z<0) return false;
    out.x=s.x; out.y=screen_height-s.y;
    return out.x>-500&&out.x<screen_width+500&&out.y>-500&&out.y<screen_height+500;
}


void ESPSystem::collectChildren(Il2CppObject_API* t,
    std::vector<std::pair<std::string,Il2CppObject_API*>>& out, int depth) {
    if (depth>10||!alive(t)) return;
    try {
        auto nameR=g_il2cpp_tool->invokeMethod(object_get_name,t,nullptr);
        if (!nameR) return;
        std::string name=g_il2cpp_tool->getString((Il2CppString_API*)nameR);
        if (name.empty()) return;
        out.push_back({name,t});

        typedef int(*Fn)(Il2CppObject_API*,const MethodInfo*);
        int cc=((Fn)transform_get_child_count->methodPointer)(t,transform_get_child_count);
        if (cc<=0||cc>150) return;

        typedef Il2CppObject_API*(*GCFn)(Il2CppObject_API*,int,const MethodInfo*);
        for (int i=0;i<cc;i++) {
            try {
                auto ch=((GCFn)transform_get_child->methodPointer)(t,i,transform_get_child);
                if (alive(ch)) collectChildren(ch,out,depth+1);
            } catch(...){ continue; }
        }
    } catch(...){ return; }
}


void ESPSystem::doScanThread() {
    auto thread = g_il2cpp_tool->attachCurrentThread();

    try {
        Il2CppClass_API* scanClass = monobehaviour_class ? monobehaviour_class : component_class;
        auto typeObj = g_il2cpp_tool->getClassTypeObject(scanClass);
        if (!typeObj) { scanning.store(false); if(thread) g_il2cpp_tool->detachThread(thread); return; }

        void* args1[1]={typeObj};
        auto arr1 = g_il2cpp_tool->invokeMethod(object_find_objects_of_type,nullptr,args1);

        std::unordered_map<std::string,std::vector<CachedObject>> newGroups;

        if (arr1) {
            auto* a=(Il2CppArray_Dump*)arr1;
            size_t cnt=a->max_length;
            for (size_t i=0;i<cnt&&i<10000;i++) {
                try {
                    auto* comp=(Il2CppObject_API*)a->vector[i];
                    if (!alive(comp)) continue;
                    auto* klass=(Il2CppClass_API*)comp->klass;
                    const char* cn=g_il2cpp_tool->getClassName(klass);
                    if (!cn||!cn[0]) continue;

                    const char* ns=g_il2cpp_tool->getClassNamespace(klass);
                    if (ns&&strncmp(ns,"UnityEngine",11)==0) continue;

                    std::string className=cn;
                    typedef Il2CppObject_API*(*Fn)(Il2CppObject_API*,const MethodInfo*);
                    auto tf=component_get_transform?
                        ((Fn)component_get_transform->methodPointer)(comp,component_get_transform):nullptr;
                    if (!alive(tf)) continue;

                    auto nameR=g_il2cpp_tool->invokeMethod(object_get_name,comp,nullptr);
                    std::string objName=nameR?g_il2cpp_tool->getString((Il2CppString_API*)nameR):"?";

                    CachedObject co;
                    co.transform = tf;
                    co.component = comp;    // 保存类实例指针
                    co.objName = objName;
                    co.className = className;
                    newGroups[className].push_back(std::move(co));
                } catch(...){ continue; }
            }
        }

        std::unordered_map<std::string,int> newBoneCount, newOtherCount;
        std::vector<CachedObject> tfObjects;

        auto tfTypeObj = g_il2cpp_tool->getClassTypeObject(transform_class);
        if (tfTypeObj) {
            void* args2[1]={tfTypeObj};
            auto arr2 = g_il2cpp_tool->invokeMethod(object_find_objects_of_type,nullptr,args2);
            if (arr2) {
                auto* a=(Il2CppArray_Dump*)arr2;
                size_t cnt=a->max_length;
                for (size_t i=0;i<cnt&&i<50000;i++) {
                    try {
                        auto* tf=(Il2CppObject_API*)a->vector[i];
                        if (!alive(tf)) continue;
                        auto nameR=g_il2cpp_tool->invokeMethod(object_get_name,tf,nullptr);
                        if (!nameR) continue;
                        std::string name=g_il2cpp_tool->getString((Il2CppString_API*)nameR);
                        if (name.empty()) continue;

                        if (isBoneNode(name)) newBoneCount[name]++;
                        else newOtherCount[name]++;

                        CachedObject co;
                        co.transform = tf;
                        co.component = nullptr;  // Transform本身没有额外类实例
                        co.objName = name;
                        co.className = "Transform";
                        tfObjects.push_back(std::move(co));
                    } catch(...){ continue; }
                }
            }
        }
        if (!tfObjects.empty()) {
            newGroups["Transform"] = std::move(tfObjects);
        }

        std::vector<BoneSet> newBoneSets;
        if (skeleton_enabled) {
            try {
                auto animClass=g_il2cpp_tool->findClass("UnityEngine","Animator");
                if (animClass) {
                    auto animType=g_il2cpp_tool->getClassTypeObject(animClass);
                    if (animType) {
                        void* bArgs[1]={animType};
                        auto bArr=g_il2cpp_tool->invokeMethod(object_find_objects_of_type,nullptr,bArgs);
                        if (bArr) {
                            auto* ba=(Il2CppArray_Dump*)bArr;
                            for (size_t i=0;i<ba->max_length&&i<100;i++) {
                                try {
                                    auto* anim=(Il2CppObject_API*)ba->vector[i];
                                    if (!alive(anim)) continue;
                                    typedef Il2CppObject_API*(*Fn)(Il2CppObject_API*,const MethodInfo*);
                                    auto rootTf=component_get_transform?
                                        ((Fn)component_get_transform->methodPointer)(anim,component_get_transform):nullptr;
                                    if (!alive(rootTf)) continue;

                                    std::vector<std::pair<std::string,Il2CppObject_API*>> children;
                                    children.reserve(80);
                                    collectChildren(rootTf,children,0);

                                    BoneSet bs;
                                    for (int m=0;m<g_bone_match_count;m++) {
                                        for (auto&ch:children) {
                                            for (int k=0;g_bone_match[m].names[k];k++) {
                                                if (ch.first==g_bone_match[m].names[k]) {
                                                    bs.bones[g_bone_match[m].key]=ch.second;
                                                    goto next_bone;
                                                }
                                            }
                                        }
                                        next_bone:;
                                    }
                                    if (bs.bones.size()>=6) newBoneSets.push_back(std::move(bs));
                                } catch(...){ continue; }
                            }
                        }
                    }
                }
            } catch(...){}
        }

        {
            std::lock_guard<std::mutex> lock(mtx);
            class_groups=std::move(newGroups);
            cached_bone_sets=std::move(newBoneSets);
            class_names_sorted.clear();
            for (auto&p:class_groups) class_names_sorted.push_back(p.first);
            std::sort(class_names_sorted.begin(),class_names_sorted.end());
            tf_bone_count=std::move(newBoneCount);
            tf_other_count=std::move(newOtherCount);
            tf_bone_names_sorted.clear();
            tf_other_names_sorted.clear();
            for (auto&p:tf_bone_count) tf_bone_names_sorted.push_back(p.first);
            for (auto&p:tf_other_count) tf_other_names_sorted.push_back(p.first);
            std::sort(tf_bone_names_sorted.begin(),tf_bone_names_sorted.end());
            std::sort(tf_other_names_sorted.begin(),tf_other_names_sorted.end());
        }

        LOGI("[ESP] Scan done: %zu classes, %zu bones",
             class_names_sorted.size(), cached_bone_sets.size());

    } catch(...){
        LOGW("[ESP] Scan exception");
    }

    if (thread) g_il2cpp_tool->detachThread(thread);
    scanning.store(false);
}

void ESPSystem::doFrameUpdate() {
    auto cam = g_il2cpp_tool->invokeMethod(camera_get_main,nullptr,nullptr);
    if (!alive(cam)) {
        std::lock_guard<std::mutex> lock(mtx);
        class_groups.clear();
        cached_bone_sets.clear();
        class_names_sorted.clear();
        tf_bone_names_sorted.clear();
        tf_other_names_sorted.clear();
        frame_labels.clear();
        frame_bones.clear();
        scan_timer=0;
        return;
    }

    if (camera_get_pixel_width&&camera_get_pixel_height) {
        typedef int(*Fn)(Il2CppObject_API*,const MethodInfo*);
        screen_width=(float)((Fn)camera_get_pixel_width->methodPointer)(cam,camera_get_pixel_width);
        screen_height=(float)((Fn)camera_get_pixel_height->methodPointer)(cam,camera_get_pixel_height);
    }

    Vec3 camWorldPos = {0, 0, 0};
    if (component_get_transform) {
        typedef Il2CppObject_API*(*TFn)(Il2CppObject_API*, const MethodInfo*);
        auto camTf = ((TFn)component_get_transform->methodPointer)(cam, component_get_transform);
        if (alive(camTf)) {
            camWorldPos = getPos(camTf);
        }
    }

    std::vector<DrawLabel> labels;
    std::vector<DrawBoneLine> bones;

    {
        std::lock_guard<std::mutex> lock(mtx);
        bool tfSel = selected_classes.count("Transform")>0;

        for (auto&cn:class_names_sorted) {
            if (!selected_classes.count(cn)) continue;
            auto it=class_groups.find(cn);
            if (it==class_groups.end()) continue;

            for (auto&co:it->second) {
                if (!alive(co.transform)) continue;

                if (cn=="Transform"&&tfSel) {
                    bool show=isBoneNode(co.objName)?
                        selected_tf_bones.count(co.objName)>0:
                        selected_tf_others.count(co.objName)>0;
                    if (!show) continue;
                }

                Vec3 pos=getPos(co.transform);
                ImVec2 sp;
                if (w2s(cam,pos,sp)) {
                    DrawLabel lb;
                    lb.pos = sp;
                    lb.text = co.objName;
                    lb.transformAddr = (uintptr_t)co.transform;
                    lb.componentAddr = (uintptr_t)co.component;
                    lb.worldX = pos.x;
                    lb.worldY = pos.y;
                    lb.worldZ = pos.z;
                    lb.screenX = sp.x;
                    lb.screenY = sp.y;
                    labels.push_back(std::move(lb));
                }
            }
        }

        if (skeleton_enabled) {
            bool doOcclusion = occlusion_enabled && occlusion_methods_cached;

            for (auto& bs : cached_bone_sets) {
                struct BoneState {
                    Vec3 worldPos;
                    ImVec2 screenPos;
                    bool onScreen;
                    bool occluded;
                };
                std::unordered_map<std::string, BoneState> boneStates;

                for (auto& bp : bs.bones) {
                    if (!alive(bp.second)) continue;
                    BoneState state;
                    state.worldPos = getPos(bp.second);
                    state.onScreen = w2s(cam, state.worldPos, state.screenPos);
                    state.occluded = false;

                    if (doOcclusion && state.onScreen) {
                        state.occluded = linecastSafe(camWorldPos, state.worldPos);
                    }

                    boneStates[bp.first] = state;
                }

                for (int c = 0; c < g_conn_count; c++) {
                    auto itF = boneStates.find(g_conns[c].from);
                    auto itT = boneStates.find(g_conns[c].to);
                    if (itF == boneStates.end() || itT == boneStates.end()) continue;
                    if (!itF->second.onScreen || !itT->second.onScreen) continue;

                    ImU32 lineColor;
                    float lineThickness;

                    if (!doOcclusion) {
                        lineColor = color_visible;
                        lineThickness = 2.0f;
                    } else {
                        bool fromOcc = itF->second.occluded;
                        bool toOcc   = itT->second.occluded;

                        if (!fromOcc && !toOcc) {
                            lineColor = color_visible;
                            lineThickness = 2.0f;
                        } else {
                            lineColor = color_occluded;
                            lineThickness = 2.0f;
                        }
                    }

                    bones.push_back({
                        itF->second.screenPos,
                        itT->second.screenPos,
                        lineColor,
                        lineThickness
                    });
                }
            }
        }
    }

    frame_labels=std::move(labels);
    frame_bones=std::move(bones);
}

void ESPSystem::update(float dt) {
    if (!methods_cached){initialize();return;}

    scan_timer+=dt;
    if (scan_timer>=scan_interval && !scanning.load()) {
        scan_timer=0;
        scanning.store(true);
        if (scan_thread.joinable()) scan_thread.join();
        scan_thread = std::thread(&ESPSystem::doScanThread, this);
    }

    doFrameUpdate();
}

void ESPSystem::refresh() { scan_timer=scan_interval; }

void ESPSystem::drawESP() {
    auto* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;

    // ===== 简单防重叠：屏幕分桶 =====
    const float cellW = 120.0f;
    const float cellH = 18.0f;

    const int maxPerCell = 3; // 每个格子最多显示几条

    std::unordered_map<uint64_t, int> cellCounter;

    for (auto& lb : frame_labels) {

        float x = lb.pos.x;
        float y = lb.pos.y;

        // ===== 计算所在网格 =====
        int cx = (int)(x / cellW);
        int cy = (int)(y / cellH);

        uint64_t key = ((uint64_t)cx << 32) | (uint32_t)cy;

        int& count = cellCounter[key];

        // 超过限制就跳过（避免爆炸）
        if (count >= maxPerCell) continue;

        // ===== 向下偏移，避免重叠 =====
        float offsetY = count * 14.0f;
        count++;

        ImVec2 pos(x, y + offsetY);

        // ===== 主文字（白色）=====
        dl->AddText(pos, IM_COL32(255, 255, 255, 220), lb.text.c_str());

        // ===== 额外信息（可选）=====
        if (show_transform_addr || show_component_addr ||
            show_world_pos || show_screen_pos) {

            char buf[256];
            int off = 0;

            if (show_transform_addr) {
                off += snprintf(buf + off, sizeof(buf) - off,
                    " TF:0x%" PRIXPTR, lb.transformAddr);
            }
            if (show_component_addr && lb.componentAddr) {
                off += snprintf(buf + off, sizeof(buf) - off,
                    " Obj:0x%" PRIXPTR, lb.componentAddr);
            }
            if (show_world_pos) {
                off += snprintf(buf + off, sizeof(buf) - off,
                    " W:(%.0f,%.0f,%.0f)", lb.worldX, lb.worldY, lb.worldZ);
            }
            if (show_screen_pos) {
                off += snprintf(buf + off, sizeof(buf) - off,
                    " S:(%.0f,%.0f)", lb.screenX, lb.screenY);
            }

            if (off > 0) {
                dl->AddText(
                    ImVec2(pos.x, pos.y + 12),
                    IM_COL32(100, 255, 100, 220),
                    buf
                );
            }
        }
    }

    // ===== 骨骼（保持不变）=====
    for (auto& bl : frame_bones) {
        dl->AddLine(bl.from, bl.to, bl.color, bl.thickness);

        ImU32 dotColor = (bl.color == color_occluded) ?
            IM_COL32(255, 80, 80, 200) :
            IM_COL32(255, 255, 0, 200);

        dl->AddCircleFilled(bl.from, 2.5f, dotColor);
        dl->AddCircleFilled(bl.to,   2.5f, dotColor);
    }
}

void ESPSystem::drawControlPanel() {
    std::lock_guard<std::mutex> lock(mtx);
    if (!methods_cached) {
        ImGui::TextColored(ImVec4(1,.3f,.3f,1),"ESP 未初始化");
        if (ImGui::Button("初始化")) initialize();
        return;
    }

    ImGui::SliderFloat("扫描间隔(秒)",&scan_interval,2.f,30.f,"%.0f");
    if (ImGui::Button("刷新")) refresh();
    ImGui::SameLine();
    ImGui::Text("类:%zu 标签:%zu 骨骼:%zu",
        class_names_sorted.size(),frame_labels.size(),frame_bones.size());
    ImGui::Separator();

    // ===== 显示信息开关 =====
    ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "标签显示选项");
    ImGui::Checkbox("Transform 地址", &show_transform_addr);
    ImGui::SameLine();
    ImGui::Checkbox("类实例地址", &show_component_addr);
    ImGui::Checkbox("世界坐标", &show_world_pos);
    ImGui::SameLine();
    ImGui::Checkbox("屏幕坐标", &show_screen_pos);
    ImGui::Separator();

    if (ImGui::Button("全选"))
        for(auto&n:class_names_sorted) selected_classes.insert(n);
    ImGui::SameLine();
    if (ImGui::Button("全不选")) selected_classes.clear();
    ImGui::SameLine();
    static char cf[128]="";
    ImGui::SetNextItemWidth(180);
    ImGui::InputText("##cf",cf,sizeof(cf));

    ImGui::BeginChild("CL",ImVec2(0,180),true);
    for (auto&cn:class_names_sorted) {
        if (cf[0]&&!strCI(cn,cf)) continue;
        int cnt=class_groups.count(cn)?(int)class_groups[cn].size():0;
        bool sel=selected_classes.count(cn)>0;
        char lb[256]; snprintf(lb,sizeof(lb),"%s (%d)",cn.c_str(),cnt);
        if (ImGui::Checkbox(lb,&sel)){
            if(sel) selected_classes.insert(cn);
            else selected_classes.erase(cn);
        }
    }
    ImGui::EndChild();

    if (selected_classes.count("Transform")>0) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1,1,0,1),"Transform 子选择");
        static char tf[128]="";
        ImGui::SetNextItemWidth(180);
        ImGui::InputText("##tf",tf,sizeof(tf));

        float hw=ImGui::GetContentRegionAvail().x*.5f-4;

        ImGui::BeginChild("BN",ImVec2(hw,180),true);
        ImGui::TextColored(ImVec4(.5f,1,.5f,1),"角色 (%zu)",tf_bone_names_sorted.size());
        if (ImGui::SmallButton("全选##b"))
            for(auto&n:tf_bone_names_sorted) selected_tf_bones.insert(n);
        ImGui::SameLine();
        if (ImGui::SmallButton("清##b")) selected_tf_bones.clear();
        for (auto&name:tf_bone_names_sorted) {
            if (tf[0]&&!strCI(name,tf)) continue;
            int cnt=tf_bone_count.count(name)?tf_bone_count[name]:0;
            bool sel=selected_tf_bones.count(name)>0;
            char lb[256]; snprintf(lb,sizeof(lb),"%s(%d)##b",name.c_str(),cnt);
            if (ImGui::Checkbox(lb,&sel)){
                if(sel) selected_tf_bones.insert(name);
                else selected_tf_bones.erase(name);
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("ON",ImVec2(hw,180),true);
        ImGui::TextColored(ImVec4(.5f,.8f,1,1),"其他 (%zu)",tf_other_names_sorted.size());
        if (ImGui::SmallButton("全选##o"))
            for(auto&n:tf_other_names_sorted) selected_tf_others.insert(n);
        ImGui::SameLine();
        if (ImGui::SmallButton("清##o")) selected_tf_others.clear();
        for (auto&name:tf_other_names_sorted) {
            if (tf[0]&&!strCI(name,tf)) continue;
            int cnt=tf_other_count.count(name)?tf_other_count[name]:0;
            bool sel=selected_tf_others.count(name)>0;
            char lb[256]; snprintf(lb,sizeof(lb),"%s(%d)##o",name.c_str(),cnt);
            if (ImGui::Checkbox(lb,&sel)){
                if(sel) selected_tf_others.insert(name);
                else selected_tf_others.erase(name);
            }
        }
        ImGui::EndChild();
    }

    ImGui::Separator();
    ImGui::Checkbox("骨骼连线",&skeleton_enabled);
    if (skeleton_enabled) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(.5f,1,.5f,1),"角色:%zu",cached_bone_sets.size());

        ImGui::Indent(16);

        if (!occlusion_methods_cached) {
            ImGui::TextColored(ImVec4(1,.5f,0,1),"Linecast 未缓存");
            ImGui::SameLine();
            if (ImGui::SmallButton("重试##occ")) cacheOcclusionMethods();
        } else {
            ImGui::Checkbox("掩体检测 (Linecast)", &occlusion_enabled);

            if (occlusion_enabled) {
                ImVec4 cVis = ImGui::ColorConvertU32ToFloat4(color_visible);
                ImVec4 cOcc = ImGui::ColorConvertU32ToFloat4(color_occluded);

                ImGui::Text("可见:");
                ImGui::SameLine();
                if (ImGui::ColorEdit4("##cvis", &cVis.x, ImGuiColorEditFlags_NoInputs))
                    color_visible = ImGui::ColorConvertFloat4ToU32(cVis);
                ImGui::SameLine();
                ImGui::Text("遮挡:");
                ImGui::SameLine();
                if (ImGui::ColorEdit4("##cocc", &cOcc.x, ImGuiColorEditFlags_NoInputs))
                    color_occluded = ImGui::ColorConvertFloat4ToU32(cOcc);

                ImGui::Text("LayerMask: Default (1)");
            }
        }
        ImGui::Unindent(16);
    }
}