// =============================================================================
// ESP.cpp — ESP 透视绘制系统完整实现
// 基于 libhook.so 反编译重建 (Rust/egui 版) 移植到 ImGui + IL2CPP C++ 模板
//
// 反编译实现对应:
//   draw_circle.rs  → ESPSystem::Render() + DrawObjectESP()
//   data_reader.rs  → ESPSystem::Tick() + ReadGameObjects() + ReadObjectName()
//   game_loop.rs    → ESP_Tick() / ESP_Render() 调度
//
// IL2CPP 调用链 (从 il2cpp/mod.rs 还原):
//   il2cpp_domain_get → il2cpp_domain_get_assemblies →
//   il2cpp_assembly_get_image → il2cpp_class_from_name →
//   il2cpp_class_get_fields (遍历字段获取偏移) →
//   il2cpp_class_get_method_from_name → il2cpp_runtime_invoke
// =============================================================================

#include "ESP.h"

// =============================================================================
// 单例
// =============================================================================

ESPSystem& ESPSystem::Instance() {
    static ESPSystem instance;
    return instance;
}

ESPSystem::ESPSystem()
    : m_il2cppHandle(nullptr), m_il2cppLoaded(false),
      m_fn_domain_get(nullptr), m_fn_domain_get_assemblies(nullptr),
      m_fn_assembly_get_image(nullptr), m_fn_class_from_name(nullptr),
      m_fn_class_get_fields(nullptr), m_fn_field_get_name(nullptr),
      m_fn_field_get_offset(nullptr), m_fn_class_get_method(nullptr),
      m_fn_runtime_invoke(nullptr), m_fn_array_length(nullptr),
      m_fn_array_get(nullptr), m_fn_object_get_class(nullptr),
      m_fn_string_to_utf8(nullptr), m_fn_string_new(nullptr),
      m_fn_class_get_name(nullptr), m_fn_image_get_name(nullptr),
      m_classGameCoreCenter(nullptr), m_classPlayerBase(nullptr),
      m_classCamera(nullptr),
      m_methodGetInstance(nullptr), m_methodGetCurrent(nullptr),
      m_methodGetName(nullptr),
      m_gameReady(false), m_counter(0), m_pcounter(0),
      m_gameInstance(nullptr) {}

// =============================================================================
// IL2CPP 初始化 — dlopen libil2cpp.so + 解析函数符号
// 对应 il2cpp/mod.rs 的 Il2CppApi::load()
// =============================================================================

bool ESPSystem::InitIL2CPP() {
    if (m_il2cppLoaded) return true;

    ESP_LOGI("ESP: 初始化 IL2CPP API...");

    m_il2cppHandle = dlopen("libil2cpp.so", RTLD_NOW);
    if (!m_il2cppHandle) {
        ESP_LOGE("ESP: 无法加载 libil2cpp.so: %s", dlerror());
        return false;
    }

    if (!ResolveIL2CPPFunctions()) {
        ESP_LOGE("ESP: 解析 IL2CPP 函数失败");
        dlclose(m_il2cppHandle);
        m_il2cppHandle = nullptr;
        return false;
    }

    if (!FindGameClasses()) {
        ESP_LOGE("ESP: 查找游戏类失败");
        return false;
    }

    if (!InitFieldOffsets()) {
        ESP_LOGE("ESP: 初始化字段偏移失败");
        return false;
    }

    m_il2cppLoaded = true;
    ESP_LOGI("ESP: IL2CPP API 初始化成功!");
    return true;
}

// =============================================================================
// 解析 IL2CPP 函数符号
// 对应 il2cpp/mod.rs 的 Il2CppApi::resolve()
// =============================================================================

bool ESPSystem::ResolveIL2CPPFunctions() {
    #define ESP_RESOLVE(name, type) \
        m_fn_##name = (type)dlsym(m_il2cppHandle, "il2cpp_" #name); \
        if (!m_fn_##name) { \
            ESP_LOGE("ESP: 无法解析 il2cpp_" #name ": %s", dlerror()); \
            return false; \
        } \
        ESP_LOGD("ESP: + il2cpp_" #name " = %p", m_fn_##name);

    ESP_RESOLVE(domain_get,            il2cpp_domain_get_t);
    ESP_RESOLVE(domain_get_assemblies, il2cpp_domain_get_assemblies_t);
    ESP_RESOLVE(assembly_get_image,    il2cpp_assembly_get_image_t);
    ESP_RESOLVE(class_from_name,       il2cpp_class_from_name_t);
    ESP_RESOLVE(class_get_fields,      il2cpp_class_get_fields_t);
    ESP_RESOLVE(field_get_name,        il2cpp_field_get_name_t);
    ESP_RESOLVE(field_get_offset,      il2cpp_field_get_offset_t);
    ESP_RESOLVE(class_get_method, il2cpp_class_get_method_from_name_t);
    ESP_RESOLVE(runtime_invoke,        il2cpp_runtime_invoke_t);
    ESP_RESOLVE(array_length,          il2cpp_array_length_t);
    ESP_RESOLVE(array_get,             il2cpp_array_get_t);
    ESP_RESOLVE(object_get_class,      il2cpp_object_get_class_t);
    ESP_RESOLVE(string_to_utf8,        il2cpp_string_to_utf8_t);
    ESP_RESOLVE(string_new,            il2cpp_string_new_t);
    ESP_RESOLVE(class_get_name,        il2cpp_class_get_name_t);
    ESP_RESOLVE(image_get_name,        il2cpp_image_get_name_t);

    #undef ESP_RESOLVE
    return true;
}

// =============================================================================
// 查找游戏类 — 遍历程序集查找 GameCoreCenter / PlayerBase / Camera
// 对应 data_reader.rs 的 init_field_offsets() 中的类查找逻辑
// =============================================================================

bool ESPSystem::FindGameClasses() {
    ESP_LOGI("ESP: 查找游戏类...");

    Il2CppDomain* domain = m_fn_domain_get();
    if (!domain) {
        ESP_LOGE("ESP: il2cpp_domain_get 返回 null");
        return false;
    }

    size_t count = 0;
    const Il2CppAssembly** assemblies = m_fn_domain_get_assemblies(domain, &count);
    if (!assemblies || count == 0) {
        ESP_LOGE("ESP: 无可用程序集");
        return false;
    }

    ESP_LOGI("ESP: 共 %zu 个程序集, 正在查找 Assembly-CSharp...", count);

    Il2CppImage* targetImage = nullptr;
    for (size_t i = 0; i < count; i++) {
        if (!assemblies[i]) continue;
        Il2CppImage* img = m_fn_assembly_get_image(assemblies[i]);
        if (!img) continue;

        // 通过 class_from_name 直接尝试查找目标类 (与 data_reader.rs 一致)
        Il2CppClass* testClass = m_fn_class_from_name(img, "", "GameCoreCenter");
        if (testClass) {
            targetImage = img;
            m_classGameCoreCenter = testClass;
            ESP_LOGI("ESP: + 找到 GameCoreCenter (程序集 #%zu)", i);
            break;
        }
    }

    if (!targetImage) {
        ESP_LOGE("ESP: 未找到 Assembly-CSharp 程序集");
        return false;
    }

    // 查找其他游戏类
    m_classPlayerBase = m_fn_class_from_name(targetImage, "", "PlayerBase");
    if (m_classPlayerBase) {
        ESP_LOGI("ESP: + 找到 PlayerBase");
    } else {
        ESP_LOGE("ESP: 未找到 PlayerBase");
        return false;
    }

    m_classCamera = m_fn_class_from_name(targetImage, "", "Camera");
    if (m_classCamera) {
        ESP_LOGI("ESP: + 找到 Camera");
    } else {
        ESP_LOGW("ESP: 未找到 Camera (世界坐标转换将不可用)");
    }

    // 查找方法 (逆向字符串: "get_instance", "get_current", "get_name")
    if (m_classGameCoreCenter) {
        m_methodGetInstance = m_fn_class_get_method(m_classGameCoreCenter, "get_instance", 0);
        if (m_methodGetInstance) ESP_LOGI("ESP: + 找到 GameCoreCenter.get_instance");
    }

    if (m_classCamera) {
        m_methodGetCurrent = m_fn_class_get_method(m_classCamera, "get_current", 0);
        if (m_methodGetCurrent) ESP_LOGI("ESP: + 找到 Camera.get_current");
    }

    if (m_classPlayerBase) {
        m_methodGetName = m_fn_class_get_method(m_classPlayerBase, "get_name", 0);
        if (m_methodGetName) ESP_LOGI("ESP: + 找到 PlayerBase.get_name");
    }

    return true;
}

// =============================================================================
// 初始化字段偏移 — 遍历类字段获取内存偏移量
// 对应 data_reader.rs 的 init_field_offsets()
//
// IL2CPP 调用链:
//   il2cpp_class_get_fields(klass, &iter) → 返回 FieldInfo*
//   il2cpp_field_get_name(field)  → 字段名
//   il2cpp_field_get_offset(field) → 字段偏移量
// =============================================================================

bool ESPSystem::InitFieldOffsets() {
    ESP_LOGI("ESP: 初始化字段偏移...");

    // --- 遍历 GameCoreCenter 字段 ---
    if (m_classGameCoreCenter) {
        void* iter = nullptr;
        FieldInfo* field;
        while ((field = m_fn_class_get_fields(m_classGameCoreCenter, &iter)) != nullptr) {
            const char* fname = m_fn_field_get_name(field);
            size_t offset = m_fn_field_get_offset(field);
            if (!fname) continue;

            // 匹配可能的字段名 (不同游戏版本字段名可能不同, 与 data_reader.rs 一致)
            if (strcmp(fname, "players") == 0 || strcmp(fname, "playerList") == 0 ||
                strcmp(fname, "player_list") == 0 || strcmp(fname, "allPlayers") == 0) {
                m_offsets.gcc_player_list = offset;
                ESP_LOGD("ESP:   GameCoreCenter.%s @ offset 0x%zx", fname, offset);
            } else if (strcmp(fname, "fishList") == 0 || strcmp(fname, "fish_list") == 0 ||
                       strcmp(fname, "fishes") == 0) {
                m_offsets.gcc_fish_list = offset;
                ESP_LOGD("ESP:   GameCoreCenter.%s @ offset 0x%zx", fname, offset);
            } else if (strcmp(fname, "cells") == 0 || strcmp(fname, "cellList") == 0 ||
                       strcmp(fname, "cell_list") == 0) {
                m_offsets.gcc_cell_list = offset;
                ESP_LOGD("ESP:   GameCoreCenter.%s @ offset 0x%zx", fname, offset);
            } else if (strcmp(fname, "selfPlayer") == 0 || strcmp(fname, "self_player") == 0 ||
                       strcmp(fname, "localPlayer") == 0 || strcmp(fname, "myPlayer") == 0) {
                m_offsets.gcc_self_player = offset;
                ESP_LOGD("ESP:   GameCoreCenter.%s @ offset 0x%zx", fname, offset);
            }
        }
    }

    // --- 遍历 PlayerBase 字段 ---
    if (m_classPlayerBase) {
        void* iter = nullptr;
        FieldInfo* field;
        while ((field = m_fn_class_get_fields(m_classPlayerBase, &iter)) != nullptr) {
            const char* fname = m_fn_field_get_name(field);
            size_t offset = m_fn_field_get_offset(field);
            if (!fname) continue;

            // 逆向字符串: "name", "x", "y", "z", "radius", "score", "rankId"
            if (strcmp(fname, "name") == 0 || strcmp(fname, "playerName") == 0 ||
                strcmp(fname, "nickName") == 0 || strcmp(fname, "userName") == 0) {
                m_offsets.pb_name = offset;
                ESP_LOGD("ESP:   PlayerBase.%s @ offset 0x%zx", fname, offset);
            } else if (strcmp(fname, "x") == 0 || strcmp(fname, "posX") == 0 ||
                       strcmp(fname, "positionX") == 0) {
                m_offsets.pb_x = offset;
            } else if (strcmp(fname, "y") == 0 || strcmp(fname, "posY") == 0 ||
                       strcmp(fname, "positionY") == 0) {
                m_offsets.pb_y = offset;
            } else if (strcmp(fname, "z") == 0 || strcmp(fname, "posZ") == 0 ||
                       strcmp(fname, "positionZ") == 0) {
                m_offsets.pb_z = offset;
            } else if (strcmp(fname, "radius") == 0 || strcmp(fname, "size") == 0 ||
                       strcmp(fname, "mass") == 0) {
                m_offsets.pb_radius = offset;
            } else if (strcmp(fname, "id") == 0 || strcmp(fname, "playerId") == 0 ||
                       strcmp(fname, "objectId") == 0) {
                m_offsets.pb_id = offset;
            } else if (strcmp(fname, "rankId") == 0 || strcmp(fname, "rank_id") == 0 ||
                       strcmp(fname, "rank") == 0) {
                m_offsets.pb_rank_id = offset;
            } else if (strcmp(fname, "score") == 0 || strcmp(fname, "weight") == 0 ||
                       strcmp(fname, "exp") == 0) {
                m_offsets.pb_score = offset;
            } else if (strcmp(fname, "isAlive") == 0 || strcmp(fname, "is_alive") == 0 ||
                       strcmp(fname, "alive") == 0) {
                m_offsets.pb_is_alive = offset;
            } else if (strcmp(fname, "color") == 0 || strcmp(fname, "playerColor") == 0) {
                m_offsets.pb_color = offset;
            }
        }
    }

    m_offsets.initialized = true;
    ESP_LOGI("ESP: 字段偏移初始化完成");
    ESP_LOGI("ESP:   player_list=%zu fish_list=%zu cell_list=%zu self_player=%zu",
         m_offsets.gcc_player_list, m_offsets.gcc_fish_list,
         m_offsets.gcc_cell_list, m_offsets.gcc_self_player);
    ESP_LOGI("ESP:   name=%zu x=%zu y=%zu radius=%zu rank_id=%zu score=%zu",
         m_offsets.pb_name, m_offsets.pb_x, m_offsets.pb_y,
         m_offsets.pb_radius, m_offsets.pb_rank_id, m_offsets.pb_score);

    return true;
}

// =============================================================================
// 每帧更新 — 读取游戏数据
// 对应 data_reader.rs 的 tick() + game_loop.rs 的 tick()
// =============================================================================

void ESPSystem::Tick() {
    // 更新帧计数器 (逆向字符串: "counter: %d", "pcounter: %d")
    uint32_t c = m_counter.fetch_add(1);
    m_pcounter.store(c);

    // 首次调用时初始化 IL2CPP
    if (!m_il2cppLoaded) {
        if (!InitIL2CPP()) return;
    }

    // 获取 GameCoreCenter 单例 (逆向: GameCoreCenter.get_instance())
    if (!m_gameInstance) {
        if (m_methodGetInstance) {
            Il2CppException* exc = nullptr;
            Il2CppObject* result = m_fn_runtime_invoke(m_methodGetInstance, nullptr, nullptr, &exc);
            if (result && !exc) {
                m_gameInstance = result;
                // 逆向字符串: "game_settings: GameCoreCenter.get_instance detected, game ready!"
                m_gameReady = true;
                ESP_LOGI("ESP: + GameCoreCenter.get_instance 检测到, 游戏就绪!");
                ESP_LOGI("ESP:   data_reader auto-enabled, direct_call init requested");
            }
        }
    }

    if (!m_gameReady || !m_gameInstance) return;

    // 读取相机信息
    ReadCameraInfo();

    // 读取游戏对象
    ReadGameObjects();
}

// =============================================================================
// 读取相机信息 — 用于世界坐标 → 屏幕坐标转换
// 逆向: Camera.get_current
// =============================================================================

void ESPSystem::ReadCameraInfo() {
    if (!m_methodGetCurrent) return;

    // 调用 Camera.get_current()
    Il2CppException* exc = nullptr;
    Il2CppObject* camObj = m_fn_runtime_invoke(m_methodGetCurrent, nullptr, nullptr, &exc);
    if (!camObj || exc) return;

    // 读取相机字段 (位置、缩放)
    // 在 2D IO 游戏中, 相机通常有 x, y 位置和 orthographicSize/zoom
    Il2CppClass* camClass = m_fn_object_get_class(camObj);
    if (!camClass) return;

    void* iter = nullptr;
    FieldInfo* field;
    while ((field = m_fn_class_get_fields(camClass, &iter)) != nullptr) {
        const char* fname = m_fn_field_get_name(field);
        size_t offset = m_fn_field_get_offset(field);
        if (!fname || offset == 0) continue;

        // 读取相机位置
        if (strcmp(fname, "x") == 0 || strcmp(fname, "posX") == 0) {
            m_camera.cam_x = *(float*)((char*)camObj + offset);
        } else if (strcmp(fname, "y") == 0 || strcmp(fname, "posY") == 0) {
            m_camera.cam_y = *(float*)((char*)camObj + offset);
        } else if (strcmp(fname, "orthographicSize") == 0 || strcmp(fname, "zoom") == 0 ||
                   strcmp(fname, "halfHeight") == 0) {
            m_camera.zoom = *(float*)((char*)camObj + offset);
        }
    }

    // 设置屏幕尺寸 (从 ImGui IO 获取)
    ImGuiIO& io = ImGui::GetIO();
    m_camera.screen_w = io.DisplaySize.x;
    m_camera.screen_h = io.DisplaySize.y;

    // 如果没有读到 zoom, 使用默认值
    if (m_camera.zoom <= 0) {
        m_camera.zoom = 1.0f;
    }

    m_camera.valid = true;
}

// =============================================================================
// 读取游戏对象 — 遍历 GameCoreCenter 的对象列表
// 对应 data_reader.rs 的 read_objects()
//
// IL2CPP 调用链:
//   GameCoreCenter 实例 → 读取 player_list 字段 → Il2CppArray*
//   il2cpp_array_length(array) → 元素数量
//   il2cpp_array_get(array, i) → PlayerBase*
//   读取 PlayerBase 的各字段 (通过偏移直接读内存)
// =============================================================================

void ESPSystem::ReadGameObjects() {
    if (!m_offsets.initialized || !m_gameInstance) return;

    std::vector<GameObjectInfo> newObjects;

    // --- 读取玩家/鱼/细胞列表 ---
    auto readArray = [&](size_t fieldOffset) {
        if (fieldOffset == 0) return;

        Il2CppArray* array = *(Il2CppArray**)((char*)m_gameInstance + fieldOffset);
        if (!array) return;

        size_t len = m_fn_array_length(array);
        for (size_t i = 0; i < len; i++) {
            Il2CppObject* obj = m_fn_array_get(array, i);
            if (!obj) continue;

            GameObjectInfo info;

            // 读取名称 (逆向字符串: "get_name", "name=%s")
            info.name = ReadObjectName(obj);
            if (!info.name.empty()) {
                ESP_LOGD("ESP: name=%s", info.name.c_str());
            }

            // 读取坐标 (通过偏移直接读内存)
            if (m_offsets.pb_x) info.world_x = *(float*)((char*)obj + m_offsets.pb_x);
            if (m_offsets.pb_y) info.world_y = *(float*)((char*)obj + m_offsets.pb_y);
            if (m_offsets.pb_z) info.world_z = *(float*)((char*)obj + m_offsets.pb_z);

            // 读取半径
            if (m_offsets.pb_radius) info.radius = *(float*)((char*)obj + m_offsets.pb_radius);

            // 读取 ID
            if (m_offsets.pb_id) info.object_id = *(int*)((char*)obj + m_offsets.pb_id);

            // 读取排名 ID (逆向字符串: "self_rank_id")
            if (m_offsets.pb_rank_id) info.rank_id = *(int*)((char*)obj + m_offsets.pb_rank_id);

            // 读取分数
            if (m_offsets.pb_score) info.score = *(int*)((char*)obj + m_offsets.pb_score);

            // 读取存活状态 (逆向字符串: "Die")
            if (m_offsets.pb_is_alive) info.is_alive = *(bool*)((char*)obj + m_offsets.pb_is_alive);
            else info.is_alive = true;

            // 识别是否是自己 (逆向字符串: "self_rank_id")
            info.is_self = (config.self_rank_id >= 0 && info.rank_id == config.self_rank_id);

            // 设置颜色
            info.color = GetObjectColor(info);

            newObjects.push_back(info);
        }
    };

    // 读取玩家列表
    readArray(m_offsets.gcc_player_list);

    // 读取鱼列表
    readArray(m_offsets.gcc_fish_list);

    // 读取细胞列表
    readArray(m_offsets.gcc_cell_list);

    // 更新缓存
    {
        std::lock_guard<std::mutex> lock(m_objectMutex);
        m_objects = std::move(newObjects);
    }
}

// =============================================================================
// 读取对象名称 — 通过 get_name 方法或字段偏移
// 逆向字符串: "get_name", "name=", "name=%s"
//
// 优先通过 il2cpp_runtime_invoke 调用 get_name(),
// 因为名称可能存储为 IL2CPP 托管字符串 (需要 il2cpp_string_to_utf8 转换)
// =============================================================================

std::string ESPSystem::ReadObjectName(Il2CppObject* obj) {
    if (!obj) return "";

    // 方法 1: 通过 runtime_invoke 调用 get_name() (逆向字符串: "get_name")
    if (m_methodGetName) {
        Il2CppException* exc = nullptr;
        Il2CppObject* nameObj = m_fn_runtime_invoke(m_methodGetName, obj, nullptr, &exc);
        if (nameObj && !exc) {
            // 将 IL2CPP 托管字符串转换为 C 字符串
            const char* utf8 = m_fn_string_to_utf8((Il2CppString*)nameObj);
            if (utf8) {
                return std::string(utf8);
            }
        }
    }

    // 方法 2: 通过字段偏移读取名称字符串引用
    if (m_offsets.pb_name) {
        Il2CppString* nameStr = *(Il2CppString**)((char*)obj + m_offsets.pb_name);
        if (nameStr) {
            const char* utf8 = m_fn_string_to_utf8(nameStr);
            if (utf8) {
                return std::string(utf8);
            }
        }
    }

    return "";
}

// =============================================================================
// 世界坐标 → 屏幕坐标转换
// 对应 draw_circle.rs 的 world_to_screen()
//
// 在 2D IO 游戏 (大鱼吃小鱼) 中, 世界坐标到屏幕坐标的转换是:
//   screen_x = (world_x - cam_x) * scale + screen_width / 2
//   screen_y = (cam_y - world_y) * scale + screen_height / 2  (Y 翻转)
//
// 其中 scale = screen_height / (2 * orthographicSize)
// =============================================================================

bool ESPSystem::WorldToScreen(float worldX, float worldY, float& screenX, float& screenY) {
    if (!m_camera.valid || m_camera.screen_w <= 0 || m_camera.screen_h <= 0) {
        return false;
    }

    // 计算缩放比例: 屏幕高度 / (2 * 正交大小)
    float scale = m_camera.screen_h / (2.0f * m_camera.zoom);

    // 世界坐标 → 屏幕坐标
    screenX = (worldX - m_camera.cam_x) * scale + m_camera.screen_w / 2.0f;
    // Y 轴翻转 (世界坐标 Y 向上, 屏幕坐标 Y 向下)
    screenY = (m_camera.cam_y - worldY) * scale + m_camera.screen_h / 2.0f;

    // 检查是否在屏幕范围内 (允许一定边距, 因为对象可能有半径)
    float margin = 100.0f;
    if (screenX < -margin || screenX > m_camera.screen_w + margin ||
        screenY < -margin || screenY > m_camera.screen_h + margin) {
        return false;
    }

    return true;
}

// =============================================================================
// 获取对象颜色
// 对应 draw_circle.rs 的颜色判断逻辑
// =============================================================================

ImU32 ESPSystem::GetObjectColor(const GameObjectInfo& obj) {
    if (!obj.is_alive) return config.dead_color;
    if (obj.is_self) return config.self_color;
    return config.enemy_color;
}

// =============================================================================
// 渲染 ESP — ImGui 绘制
// 对应 draw_circle.rs 的 render() 方法
//
// 使用 ImGui 的背景绘制层 (GetBackgroundDrawList),
// 在游戏画面上方绘制 ESP 元素。
//
// 绘制内容:
// 1. 圆圈 (circle_stroke) — 标记对象位置和半径
// 2. 名称 (AddText) — 显示对象名称
// 3. 追踪线 (AddLine) — 从屏幕中心到对象
// 4. ID/分数/半径/距离 — 附加信息
// 5. 自身额外标记 (is_self 时多画一圈)
// =============================================================================

void ESPSystem::Render() {
    if (!config.draw_enabled || !m_gameReady) return;

    ImDrawList* bgDraw = ImGui::GetBackgroundDrawList();
    if (!bgDraw) return;

    // 获取当前相机信息
    CameraInfo cam = m_camera;
    if (!cam.valid) return;

    // 获取对象列表 (线程安全拷贝)
    std::vector<GameObjectInfo> objects;
    {
        std::lock_guard<std::mutex> lock(m_objectMutex);
        objects = m_objects;
    }

    // 遍历所有对象, 绘制 ESP
    for (const auto& obj : objects) {
        DrawObjectESP(obj, cam);
    }
}

// =============================================================================
// 绘制单个对象的 ESP
// 对应 draw_circle.rs 的 render() 中对每个 GameObjectInfo 的绘制逻辑
// =============================================================================

void ESPSystem::DrawObjectESP(const GameObjectInfo& obj, const CameraInfo& cam) {
    ImDrawList* bgDraw = ImGui::GetBackgroundDrawList();
    if (!bgDraw) return;

    // 世界坐标 → 屏幕坐标
    float screenX, screenY;
    if (!WorldToScreen(obj.world_x, obj.world_y, screenX, screenY)) {
        return; // 对象不在屏幕范围内
    }

    // 计算屏幕上的半径 (世界半径 × 缩放)
    float scale = cam.screen_h / (2.0f * cam.zoom);
    float screenRadius = obj.radius * scale;
    if (screenRadius < 2.0f) screenRadius = 2.0f;    // 最小半径
    if (screenRadius > 500.0f) screenRadius = 500.0f; // 最大半径

    // 获取对象颜色
    ImU32 color = GetObjectColor(obj);

    // --- 1. 绘制圆圈 (对应 egui 的 circle_stroke) ---
    // 逆向字符串: "draw_color"
    if (config.show_circle) {
        bgDraw->AddCircle(
            ImVec2(screenX, screenY),
            screenRadius,
            color,
            config.circle_segments,
            config.circle_thickness
        );

        // 内部填充 (半透明)
        bgDraw->AddCircleFilled(
            ImVec2(screenX, screenY),
            screenRadius,
            IM_COL32(
                IM_COL32_R(color), IM_COL32_G(color),
                IM_COL32_B(color), 30  // 低透明度填充
            )
        );
    }

    // --- 2. 自身额外标记 (对应 draw_circle.rs 的 is_self 圈) ---
    if (config.show_self_marker && obj.is_self) {
        bgDraw->AddCircle(
            ImVec2(screenX, screenY),
            screenRadius + 3.0f,
            IM_COL32(255, 128, 0, 255),
            config.circle_segments,
            3.0f
        );
    }

    // --- 3. 绘制名称 (对应 egui 的 text) ---
    // 逆向字符串: "name_color", "name_prefix"
    if (config.show_name && !obj.name.empty()) {
        std::string displayName = obj.name;
        if (config.rename_enabled && !config.name_prefix.empty()) {
            displayName = config.name_prefix + displayName;
        }

        // 名称位置 (在圆圈上方)
        ImVec2 textPos(screenX, screenY - screenRadius - config.name_offset_y);

        // 文字阴影 (提高可读性)
        bgDraw->AddText(
            nullptr, config.name_font_size,
            ImVec2(textPos.x + 1, textPos.y + 1),
            IM_COL32(0, 0, 0, 200),
            displayName.c_str()
        );

        // 文字
        bgDraw->AddText(
            nullptr, config.name_font_size,
            textPos,
            config.name_color,
            displayName.c_str()
        );
    }

    // --- 4. 绘制追踪线 (对应 egui 的 line_segment) ---
    // 逆向字符串: "tracer_color"
    if (config.show_tracer) {
        ImVec2 start(cam.screen_w / 2.0f, cam.screen_h);
        ImVec2 end(screenX, screenY);
        bgDraw->AddLine(start, end, config.tracer_color, config.tracer_thickness);
    }

    // --- 5. 绘制附加信息 ---
    float infoY = screenY + screenRadius + 5.0f;
    float infoX = screenX;
    char buf[128];

    // ID (逆向字符串: "show_id")
    if (config.show_id) {
        snprintf(buf, sizeof(buf), "ID: %d", obj.object_id);
        bgDraw->AddText(nullptr, 16.0f, ImVec2(infoX, infoY),
            IM_COL32(200, 200, 200, 255), buf);
        infoY += 18.0f;
    }

    // 分数
    if (config.show_score) {
        snprintf(buf, sizeof(buf), "Score: %d", obj.score);
        bgDraw->AddText(nullptr, 16.0f, ImVec2(infoX, infoY),
            IM_COL32(255, 255, 0, 255), buf);
        infoY += 18.0f;
    }

    // 半径
    if (config.show_radius) {
        snprintf(buf, sizeof(buf), "R: %.1f", obj.radius);
        bgDraw->AddText(nullptr, 16.0f, ImVec2(infoX, infoY),
            IM_COL32(0, 255, 255, 255), buf);
        infoY += 18.0f;
    }

    // 距离 (到自身的距离)
    if (config.show_distance) {
        // 计算距离 (使用世界坐标)
        float dx = obj.world_x - m_camera.cam_x;
        float dy = obj.world_y - m_camera.cam_y;
        float dist = sqrtf(dx * dx + dy * dy);
        snprintf(buf, sizeof(buf), "Dist: %.0f", dist);
        bgDraw->AddText(nullptr, 16.0f, ImVec2(infoX, infoY),
            IM_COL32(255, 150, 0, 255), buf);
        infoY += 18.0f;
    }
}

// =============================================================================
// 应用改名 — 调用 GameCoreCenter.Rename(string)
// 逆向字符串: "rename_enabled", "Rename", "GameCoreCenter"
// =============================================================================

void ESPSystem::ApplyRename(const std::string& newName) {
    if (!m_gameInstance || !m_methodGetInstance) {
        ESP_LOGE("ESP: Rename 不可用 (游戏未就绪)");
        return;
    }

    // 查找 Rename 方法 (逆向: GameCoreCenter.Rename(string), 参数数=1)
    MethodInfo* methodRename = m_fn_class_get_method(m_classGameCoreCenter, "Rename", 1);
    if (!methodRename) {
        ESP_LOGE("ESP: 未找到 GameCoreCenter.Rename 方法");
        return;
    }

    // 创建 IL2CPP 字符串参数 (逆向: il2cpp_string_new + runtime_invoke)
    if (!m_fn_string_new) {
        ESP_LOGE("ESP: il2cpp_string_new 不可用");
        return;
    }

    Il2CppString* strArg = m_fn_string_new(newName.c_str());
    if (!strArg) {
        ESP_LOGE("ESP: 创建 IL2CPP 字符串失败");
        return;
    }

    // 调用 GameCoreCenter.Rename(string)
    void* args[] = { &strArg };
    Il2CppException* exc = nullptr;
    m_fn_runtime_invoke(methodRename, m_gameInstance, args, &exc);

    if (exc) {
        ESP_LOGE("ESP: Rename 调用异常");
    } else {
        ESP_LOGI("ESP: + 改名成功: %s", newName.c_str());
    }
}

// =============================================================================
// 便捷调用函数 (供 imguijni.cpp 调用)
// 对应 game_loop.rs 的 egui_rust_step_impl()
// =============================================================================

bool ESP_Init() {
    return ESPSystem::Instance().InitIL2CPP();
}

void ESP_Tick() {
    ESPSystem::Instance().Tick();
}

void ESP_Render() {
    ESPSystem::Instance().Render();
}
