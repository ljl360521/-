#include "item_esp.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <fstream>
#include <sstream>

ItemESP g_item_esp;

// =====================================================================
// 构造
// =====================================================================

ItemESP::ItemESP()
    : methods_cached(false),
      camera_class(nullptr), transform_class(nullptr),
      component_class(nullptr), object_class(nullptr),
      camera_get_main(nullptr), camera_world_to_screen(nullptr),
      camera_get_pixel_width(nullptr), camera_get_pixel_height(nullptr),
      transform_get_position(nullptr), component_get_transform(nullptr),
      object_op_implicit(nullptr),
      core_instance(nullptr), core_found(false),
      enabled(true),
      show_loot(true), show_mechanism(true),
      show_price(true), show_distance(true), show_item_id(false),
      max_distance(150.0f),
      filter_price_enabled(false),
      show_common(true), show_uncommon(true),
      show_rare(true), show_epic(true), show_mechanism_price(true),
      // ========== 机关白名单：逗号分隔，填入需要显示的机关名称 ==========
      // 例如: "door,chest,trap,switch"
      // 留空 = 不过滤（显示全部机关）
      mechanism_whitelist_str("古蜀宝椁,遗迹入口,遗迹出口,射出子弹（不可拾取）"),
      filter_mechanism_enabled(false),
      color_common(IM_COL32(200, 200, 200, 220)),
      color_uncommon(IM_COL32(160, 100, 200, 220)),
      color_rare(IM_COL32(255, 200, 0, 220)),
      color_epic(IM_COL32(255, 50, 50, 220)),
      color_mechanism(IM_COL32(255, 160, 0, 220)),
      screen_width(1920), screen_height(1080) {}

// =====================================================================
// 初始化
// =====================================================================

bool ItemESP::initialize() {
    if (methods_cached) return true;
    return cacheMethods();
}

bool ItemESP::cacheMethods() {
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
    if (component_class)
        component_get_transform = g_il2cpp_tool->findMethod(component_class, "get_transform", 0);
    object_op_implicit = g_il2cpp_tool->findMethod(object_class, "op_Implicit", 1);

    if (!camera_get_main || !camera_world_to_screen || !transform_get_position) return false;

    methods_cached = true;
    LOGI("[ItemESP] Init OK");
    return true;
}

// =====================================================================
// 基础工具
// =====================================================================

bool ItemESP::unityAlive(void* obj) {
    if (!alive(obj)) return false;
    if (!object_op_implicit || !object_op_implicit->methodPointer) return true;
    typedef bool (*Fn)(void*, const MethodInfo*);
    return ((Fn)object_op_implicit->methodPointer)(obj, object_op_implicit);
}

std::string ItemESP::readIl2CppString(void* strPtr) {
    if (!strPtr || (uintptr_t)strPtr < 0x10000) return "";
    int32_t len = *(int32_t*)((uint8_t*)strPtr + 0x10);
    if (len <= 0 || len > 256) return "";
    char16_t* chars = (char16_t*)((uint8_t*)strPtr + 0x14);
    std::string result;
    result.reserve(len * 3);
    for (int i = 0; i < len; i++) {
        char16_t c = chars[i];
        if (c < 0x80) result += (char)c;
        else if (c < 0x800) {
            result += (char)(0xC0 | (c >> 6));
            result += (char)(0x80 | (c & 0x3F));
        } else {
            result += (char)(0xE0 | (c >> 12));
            result += (char)(0x80 | ((c >> 6) & 0x3F));
            result += (char)(0x80 | (c & 0x3F));
        }
    }
    return result;
}

ItemESP::Vec3 ItemESP::getPos(void* transform) {
    Vec3 p = {0, 0, 0};
    if (!alive(transform) || !transform_get_position) return p;
    typedef Vec3(*Fn)(void*, const MethodInfo*);
    p = ((Fn)transform_get_position->methodPointer)(transform, transform_get_position);
    if (std::isnan(p.x) || std::isnan(p.y) || std::isnan(p.z)) return {0, 0, 0};
    return p;
}

bool ItemESP::w2s(void* cam, Vec3 w, ImVec2& out) {
    if (!alive(cam) || !camera_world_to_screen) return false;
    typedef Vec3(*Fn)(void*, Vec3, const MethodInfo*);
    Vec3 s = ((Fn)camera_world_to_screen->methodPointer)(cam, w, camera_world_to_screen);
    if (s.z < 0 || std::isnan(s.x) || std::isnan(s.y)) return false;
    out.x = s.x;
    out.y = screen_height - s.y;
    return out.x > -500 && out.x < screen_width + 500 &&
           out.y > -500 && out.y < screen_height + 500;
}

float ItemESP::vec3Dist(Vec3 a, Vec3 b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

PriceLevel ItemESP::getPriceLevel(int32_t price, ItemCategory category) {
    if (category == ItemCategory::Mechanism) return PriceLevel::Mechanism;
    if (price < 1000)  return PriceLevel::Common;
    if (price < 5000)  return PriceLevel::Uncommon;
    if (price < 10000) return PriceLevel::Rare;
    return PriceLevel::Epic;
}

ImU32 ItemESP::getLevelColor(PriceLevel level, float dist) {
    float fadeDist = max_distance * 0.7f;
    bool isFar = dist > fadeDist;
    switch (level) {
        case PriceLevel::Common:    return isFar ? IM_COL32(200,200,200,100) : color_common;
        case PriceLevel::Uncommon:  return isFar ? IM_COL32(160,100,200,100) : color_uncommon;
        case PriceLevel::Rare:      return isFar ? IM_COL32(255,200,0,100)   : color_rare;
        case PriceLevel::Epic:      return isFar ? IM_COL32(255,50,50,100)   : color_epic;
        case PriceLevel::Mechanism: return isFar ? IM_COL32(255,160,0,100)   : color_mechanism;
        default: return IM_COL32(255,255,255,220);
    }
}

// =====================================================================
// 机关白名单匹配（从逗号分隔字符串中匹配）
// =====================================================================

bool ItemESP::isMechanismAllowed(const std::string& name) {
    // 未启用过滤 或 白名单字符串为空 → 显示全部
    if (!filter_mechanism_enabled || mechanism_whitelist_str.empty()) return true;

    // 解析逗号分隔的白名单，逐个包含匹配
    std::istringstream ss(mechanism_whitelist_str);
    std::string token;
    while (std::getline(ss, token, ',')) {
        // 去除首尾空格
        size_t start = token.find_first_not_of(" \t");
        size_t end   = token.find_last_not_of(" \t");
        if (start == std::string::npos) continue;
        token = token.substr(start, end - start + 1);
        if (token.empty()) continue;

        // 包含匹配
        if (name.find(token) != std::string::npos) return true;
    }
    return false;
}

// =====================================================================
// 每帧更新
// =====================================================================

void ItemESP::doFrameUpdate() {
    if (!enabled) return;

    void* cam = g_il2cpp_tool->invokeMethod(camera_get_main, nullptr, nullptr);
    if (!alive(cam)) { frame_items.clear(); return; }

    if (camera_get_pixel_width && camera_get_pixel_height) {
        typedef int(*Fn)(void*, const MethodInfo*);
        screen_width  = (float)((Fn)camera_get_pixel_width->methodPointer)(cam, camera_get_pixel_width);
        screen_height = (float)((Fn)camera_get_pixel_height->methodPointer)(cam, camera_get_pixel_height);
    }

    Vec3 camPos = {0, 0, 0};
    if (component_get_transform) {
        typedef void*(*TFn)(void*, const MethodInfo*);
        auto camTf = ((TFn)component_get_transform->methodPointer)(cam, component_get_transform);
        if (alive(camTf)) camPos = getPos(camTf);
    }

    if (!core_found || !core_instance || !alive(core_instance)) { frame_items.clear(); return; }

    void* itemDic = readPtr(core_instance, ItemOffsets::Core_ItemDic);
    if (!itemDic || (uintptr_t)itemDic < 0x10000) { frame_items.clear(); return; }

    void* entries = readPtr(itemDic, ItemOffsets::Dict_Entries);
    int count = readMem<int>(itemDic, ItemOffsets::Dict_Count);
    if (!entries || count <= 0 || count > 2000) { frame_items.clear(); return; }

    std::vector<ItemDrawInfo> items;
    items.reserve(count);

    for (int i = 0; i < count; i++) {
        void* item = readPtr(entries, ItemOffsets::Dict_FirstValue + (i * ItemOffsets::Dict_EntryStride));
        if (!item || !alive(item)) continue;

        void* selfTF = readPtr(item, ItemOffsets::Item_SelfTF);
        if (!unityAlive(selfTF)) continue;

        Vec3 pos = getPos(selfTF);
        if (pos.x == 0 && pos.y == 0 && pos.z == 0) continue;

        float dist = vec3Dist(camPos, pos);
        if (dist > max_distance) continue;

        int32_t itemId = readMem<int32_t>(item, ItemOffsets::Item_ID);
        int32_t price  = readMem<int32_t>(item, ItemOffsets::Item_Price);
        ItemCategory category = (price == 0) ? ItemCategory::Mechanism : ItemCategory::Loot;

        if (category == ItemCategory::Loot && !show_loot) continue;
        if (category == ItemCategory::Mechanism && !show_mechanism) continue;

        PriceLevel priceLevel = getPriceLevel(price, category);

        // 价格过滤
        if (filter_price_enabled) {
            if (priceLevel == PriceLevel::Common   && !show_common)         continue;
            if (priceLevel == PriceLevel::Uncommon  && !show_uncommon)       continue;
            if (priceLevel == PriceLevel::Rare      && !show_rare)           continue;
            if (priceLevel == PriceLevel::Epic      && !show_epic)           continue;
            if (priceLevel == PriceLevel::Mechanism && !show_mechanism_price) continue;
        }

        // 读取名称
        std::string name;
        void* infoPtr = readPtr(item, ItemOffsets::Item_InfoPtr);
        if (infoPtr && (uintptr_t)infoPtr > 0x10000) {
            void* nameStr = readPtr(infoPtr, ItemOffsets::ItemInfo_NameStr);
            if (nameStr && (uintptr_t)nameStr > 0x10000)
                name = readIl2CppString(nameStr);
        }
        if (name.empty()) continue;

        // 机关白名单过滤
        if (category == ItemCategory::Mechanism && !isMechanismAllowed(name)) continue;

        ImVec2 screenPos;
        if (!w2s(cam, pos, screenPos)) continue;

        items.push_back({screenPos, name, itemId, price, dist, category, priceLevel});
    }

    std::sort(items.begin(), items.end(),
        [](const ItemDrawInfo& a, const ItemDrawInfo& b) { return a.distance > b.distance; });

    frame_items = std::move(items);
}

void ItemESP::update(float dt) {
    if (!methods_cached) { initialize(); return; }
    if (!enabled) return;
    doFrameUpdate();
}

void ItemESP::refresh() { frame_items.clear(); }

// =====================================================================
// 导出全部物品原始数据（遍历字典，不过滤，不管视野）
// =====================================================================

void ItemESP::exportAllItems(const std::string& filepath) {
    if (!methods_cached || !core_found || !core_instance || !alive(core_instance)) {
        LOGI("[ItemESP] Export failed: not ready");
        return;
    }

    void* itemDic = readPtr(core_instance, ItemOffsets::Core_ItemDic);
    if (!itemDic || (uintptr_t)itemDic < 0x10000) return;

    void* entries = readPtr(itemDic, ItemOffsets::Dict_Entries);
    int count = readMem<int>(itemDic, ItemOffsets::Dict_Count);
    if (!entries || count <= 0 || count > 5000) return;

    std::ofstream file(filepath);
    if (!file.is_open()) {
        LOGI("[ItemESP] Failed to open: %s", filepath.c_str());
        return;
    }

    // 简单表头
    file << "名称|ID|价格|类型\n";

    int exported = 0;
    for (int i = 0; i < count; i++) {
        void* item = readPtr(entries, ItemOffsets::Dict_FirstValue + (i * ItemOffsets::Dict_EntryStride));
        if (!item || !alive(item)) continue;

        int32_t itemId = readMem<int32_t>(item, ItemOffsets::Item_ID);
        int32_t price  = readMem<int32_t>(item, ItemOffsets::Item_Price);
        const char* type = (price == 0) ? "Mechanism" : "Loot";

        // 读取名称
        std::string name;
        void* infoPtr = readPtr(item, ItemOffsets::Item_InfoPtr);
        if (infoPtr && (uintptr_t)infoPtr > 0x10000) {
            void* nameStr = readPtr(infoPtr, ItemOffsets::ItemInfo_NameStr);
            if (nameStr && (uintptr_t)nameStr > 0x10000)
                name = readIl2CppString(nameStr);
        }
        if (name.empty()) name = "???";

        file << name << "|" << itemId << "|" << price << "|" << type << "\n";
        exported++;
    }

    file.close();
    LOGI("[ItemESP] Exported %d items to: %s", exported, filepath.c_str());
}

// =====================================================================
// 绘制 ESP
// =====================================================================

void ItemESP::drawESP() {
    if (!enabled || !methods_cached) return;
    auto* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;

    for (auto& item : frame_items) {
        ImU32 color = getLevelColor(item.priceLevel, item.distance);

        std::string label = item.name;

        if (show_price && item.category == ItemCategory::Loot && item.price > 0) {
            char buf[32]; snprintf(buf, sizeof(buf), " $%d", item.price);
            label += buf;
        }

        if (item.category == ItemCategory::Mechanism) label += "";

        if (show_distance) {
            char buf[32]; snprintf(buf, sizeof(buf), " [%.0fm]", item.distance);
            label += buf;
        }

        if (show_item_id) {
            char buf[32]; snprintf(buf, sizeof(buf), " #%d", item.itemId);
            label += buf;
        }

        float dotR = (item.distance < 30.0f) ? 4.0f : 3.0f;
        dl->AddCircleFilled(item.screenPos, dotR, color);
        dl->AddCircle(item.screenPos, dotR + 1.0f, IM_COL32(0,0,0,150));

        ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        ImVec2 tp(item.screenPos.x - ts.x * 0.5f, item.screenPos.y - ts.y - 6.0f);

        dl->AddRectFilled({tp.x - 2, tp.y - 1}, {tp.x + ts.x + 2, tp.y + ts.y + 1},
                          IM_COL32(0,0,0,120), 2.0f);
        dl->AddText(tp, color, label.c_str());
    }
}

// =====================================================================
// 控制面板（精简版）
// =====================================================================

void ItemESP::drawControlPanel() {
    if (!methods_cached) {
        ImGui::TextColored(ImVec4(1,.3f,.3f,1), "ItemESP 未初始化");
        if (ImGui::Button("初始化##iesp")) initialize();
        return;
    }

    ImGui::Checkbox("启用##iesp", &enabled);
    if (!enabled) return;

    // 核心状态
    if (core_found && core_instance)
        ImGui::TextColored(ImVec4(0,1,0,1), "核心类: %p", core_instance);
    else
        ImGui::TextColored(ImVec4(1,.3f,.3f,1), "核心类未设置");

    ImGui::Separator();
    ImGui::Text("显示: %zu 个物品", frame_items.size());
    ImGui::SliderFloat("最大距离##iesp", &max_distance, 10.f, 500.f, "%.0f m");

    ImGui::Separator();
    ImGui::Checkbox("普通物品##iesp", &show_loot);
    ImGui::SameLine();
    ImGui::Checkbox("机关##iesp", &show_mechanism);
    ImGui::Checkbox("价格##iesp", &show_price);
    ImGui::SameLine();
    ImGui::Checkbox("距离##iesp", &show_distance);
    ImGui::SameLine();
    ImGui::Checkbox("物品ID##iesp", &show_item_id);

    ImGui::Separator();

    // 价格过滤
    ImGui::Checkbox("价格过滤##iesp", &filter_price_enabled);
    if (filter_price_enabled) {
        ImGui::Indent();
        ImGui::Checkbox("普通(0-1k)##iesp",   &show_common);
        ImGui::SameLine();
        ImGui::Checkbox("少见(1k-5k)##iesp",   &show_uncommon);
        ImGui::Checkbox("稀有(5k-10k)##iesp",  &show_rare);
        ImGui::SameLine();
        ImGui::Checkbox("史诗(10k+)##iesp",    &show_epic);
        ImGui::Checkbox("机关##iesp_mp",        &show_mechanism_price);
        ImGui::Unindent();
    }

    ImGui::Separator();

    // 机关白名单（逗号分隔字符串）
    ImGui::Checkbox("机关白名单过滤##iesp_mf", &filter_mechanism_enabled);
    if (filter_mechanism_enabled) {
        ImGui::Indent();
        ImGui::TextColored(ImVec4(0.7f,0.7f,0.7f,1), "逗号分隔，包含匹配 (留空=全部显示)");

        // 用 InputText 编辑白名单字符串
        static char wl_buf[512] = "";
        // 同步一次
        static bool synced = false;
        if (!synced) {
            strncpy(wl_buf, mechanism_whitelist_str.c_str(), sizeof(wl_buf) - 1);
            synced = true;
        }
        if (ImGui::InputText("白名单##iesp_wl", wl_buf, sizeof(wl_buf))) {
            mechanism_whitelist_str = wl_buf;
        }
        ImGui::Unindent();
    }

    ImGui::Separator();

    // 导出按钮
    if (ImGui::Button("导出全部物品数据##export")) {
        exportAllItems("/storage/emulated/0/Android/data/com.pi.czrxdfirst/item_export.txt");
    }

    ImGui::Separator();

    // 调试
    if (ImGui::CollapsingHeader("调试##iesp")) {
        for (size_t i = 0; i < frame_items.size() && i < 20; i++) {
            auto& it = frame_items[i];
            ImGui::Text("[%zu] %s | ID:%d | $%d | %.0fm",
                        i, it.name.c_str(), it.itemId, it.price, it.distance);
        }
    }
}
