#ifndef ITEM_ESP_H
#define ITEM_ESP_H

#include "il2cpp.h"
#include "imgui.h"
#include <string>
#include <vector>
#include <cmath>

// =====================================================================
// 物品偏移
// =====================================================================
namespace ItemOffsets {
    constexpr uintptr_t Core_ItemDic            = 0xe0;

    constexpr uintptr_t Dict_Entries            = 0x18;
    constexpr uintptr_t Dict_Count              = 0x20;
    constexpr uintptr_t Dict_FirstValue         = 0x30;
    constexpr uintptr_t Dict_EntryStride        = 0x18;

    constexpr uintptr_t Item_ID                 = 0x10;
    constexpr uintptr_t Item_SelfTF             = 0x28;
    constexpr uintptr_t Item_InfoPtr            = 0xe8;
    constexpr uintptr_t ItemInfo_NameStr        = 0x18;
    constexpr uintptr_t Item_Price              = 0x1BC;
}

// =====================================================================
// 物品分类 / 价格等级
// =====================================================================
enum class ItemCategory { Loot, Mechanism };

enum class PriceLevel { Common, Uncommon, Rare, Epic, Mechanism };

// =====================================================================
// 绘制数据
// =====================================================================
struct ItemDrawInfo {
    ImVec2 screenPos;
    std::string name;
    int32_t itemId;
    int32_t price;
    float distance;
    ItemCategory category;
    PriceLevel priceLevel;
};

// =====================================================================
// ItemESP 主类
// =====================================================================
class ItemESP {
private:
    bool methods_cached;

    Il2CppClass_API* camera_class;
    Il2CppClass_API* transform_class;
    Il2CppClass_API* component_class;
    Il2CppClass_API* object_class;

    const MethodInfo* camera_get_main;
    const MethodInfo* camera_world_to_screen;
    const MethodInfo* camera_get_pixel_width;
    const MethodInfo* camera_get_pixel_height;
    const MethodInfo* transform_get_position;
    const MethodInfo* component_get_transform;
    const MethodInfo* object_op_implicit;

    void* core_instance;
    bool  core_found;

    std::vector<ItemDrawInfo> frame_items;

    // ===== 开关 =====
    bool enabled;
    bool show_loot;
    bool show_mechanism;
    bool show_price;
    bool show_distance;
    bool show_item_id;
    float max_distance;

    // ===== 价格过滤 =====
    bool filter_price_enabled;
    bool show_common;
    bool show_uncommon;
    bool show_rare;
    bool show_epic;
    bool show_mechanism_price;

    // ===== 机关白名单（逗号分隔字符串） =====
    // 直接在代码中填写，例如: "door,chest,trap"
    std::string mechanism_whitelist_str;
    bool filter_mechanism_enabled;

    // ===== 颜色 =====
    ImU32 color_common;
    ImU32 color_uncommon;
    ImU32 color_rare;
    ImU32 color_epic;
    ImU32 color_mechanism;

    float screen_width, screen_height;

    // ===== 内部方法 =====
    struct Vec3 { float x, y, z; };

    bool cacheMethods();

    inline bool alive(void* obj) {
        if (!obj || (uintptr_t)obj < 0x1000000) return false;
        void* k = *(void**)obj;
        if (!k || (uintptr_t)k < 0x1000000) return false;
        return *(uintptr_t*)((uint8_t*)obj + 0x10) != 0;
    }

    bool unityAlive(void* obj);

    template<typename T>
    inline T readMem(void* base, uintptr_t offset) {
        if (!base || (uintptr_t)base < 0x1000000) return T{};
        return *(T*)((uint8_t*)base + offset);
    }

    inline void* readPtr(void* base, uintptr_t offset) {
        if (!base || (uintptr_t)base < 0x1000000) return nullptr;
        return *(void**)((uint8_t*)base + offset);
    }

    std::string readIl2CppString(void* strPtr);
    Vec3 getPos(void* transform);
    bool w2s(void* cam, Vec3 w, ImVec2& out);
    float vec3Dist(Vec3 a, Vec3 b);

    PriceLevel getPriceLevel(int32_t price, ItemCategory category);
    ImU32 getLevelColor(PriceLevel level, float dist);
    bool isMechanismAllowed(const std::string& name);

    void doFrameUpdate();

public:
    ItemESP();
    ~ItemESP() {}

    void setCoreInstance(void* inst) { core_instance = inst; core_found = (inst != nullptr); }
    void* getCoreInstance() const { return core_instance; }
    bool isCoreFound() const { return core_found; }

    bool initialize();
    void update(float dt);
    void drawESP();
    void drawControlPanel();
    void refresh();
    bool isInitialized() const { return methods_cached; }

    // 导出全部物品原始数据（不过滤，不管是否在视野中）
    void exportAllItems(const std::string& filepath);
};

extern ItemESP g_item_esp;

#endif // ITEM_ESP_H

