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
#include "elf_resolver.h"
#include "app_log.h"

// =============================================================================
// IL2CPP 托管字符串 → UTF8 转换辅助函数
// 手动 UTF16 → UTF8 (替代旧版 il2cpp_string_to_utf8, 该符号在部分 IL2CPP 版本不可用)
// =============================================================================

static std::string Il2CppStringToUtf8Impl(const Il2CppString* str,
    il2cpp_string_chars_t charsFn, il2cpp_string_length_t lenFn) {
    if (!str || !charsFn || !lenFn) return "";
    int len = lenFn(str);
    const uint16_t* chars = charsFn(str);
    if (!chars || len <= 0) return "";
    std::string result;
    result.reserve(len * 3);
    for (int i = 0; i < len; i++) {
        uint16_t c = chars[i];
        if (c < 0x80) {
            result += (char)c;
        } else if (c < 0x800) {
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

// IL2CPP 数组元素数据起始偏移 (64位=32, 32位=16; 等于 4 * sizeof(void*))
// Il2CppArray 布局: { vtable, monitor, bounds*, max_length, [pad], data[] }
static const size_t kIl2CppArrayDataOffset = 4 * sizeof(void*);

// -----------------------------------------------------------------------------
// 动态查找 Dictionary<int,T> 内部偏移 (_entries / _count)
// 通过遍历 Dictionary 对象的类字段按名匹配, 未找到则用日志确认的默认值。
// entry 布局: { int hashCode(0); int next(4); int key(8); [pad]; T value }
//   value 偏移 = 12 向上对齐到 sizeof(void*); entry 大小 = value偏移 + sizeof(void*)
//   (64位: value@16, entry=24; 32位: value@12, entry=16 — 与日志确认的 24/16 一致)
// -----------------------------------------------------------------------------
static void DiscoverDicOffsets(void* dicObj, FieldOffsets& off,
    il2cpp_object_get_class_t objGetClass,
    il2cpp_class_get_fields_t classGetFields,
    il2cpp_field_get_name_t fieldGetName,
    il2cpp_field_get_offset_t fieldGetOffset) {
    if (off.dic_entries != 0 || !dicObj || !objGetClass) return;  // 已发现
    Il2CppClass* dicClass = objGetClass((Il2CppObject*)dicObj);
    if (!dicClass) return;
    void* iter = nullptr;
    FieldInfo* field;
    while ((field = classGetFields(dicClass, &iter)) != nullptr) {
        const char* fname = fieldGetName(field);
        size_t offset = fieldGetOffset(field);
        if (!fname) continue;
        if (strcmp(fname, "_entries") == 0) off.dic_entries = offset;
        else if (strcmp(fname, "_count") == 0) off.dic_count = offset;
    }
    if (off.dic_entries == 0) off.dic_entries = 0x18;  // 日志确认默认值
    if (off.dic_count == 0) off.dic_count = 0x20;
}

// =============================================================================
// 单例
// =============================================================================

ESPSystem& ESPSystem::Instance() {
    static ESPSystem instance;
    return instance;
}

ESPSystem::ESPSystem()
    : m_il2cppHandle(nullptr), m_il2cppBase(0), m_il2cppLoaded(false),
      m_fn_domain_get(nullptr), m_fn_domain_get_assemblies(nullptr),
      m_fn_assembly_get_image(nullptr), m_fn_class_from_name(nullptr),
      m_fn_class_get_fields(nullptr), m_fn_field_get_name(nullptr),
      m_fn_field_get_offset(nullptr), m_fn_class_get_method(nullptr),
      m_fn_runtime_invoke(nullptr), m_fn_array_length(nullptr),
      m_fn_array_get(nullptr), m_fn_object_get_class(nullptr),
      m_fn_string_chars(nullptr), m_fn_string_length(nullptr),
      m_fn_string_new(nullptr),
      m_fn_class_get_name(nullptr), m_fn_image_get_name(nullptr),
      m_fn_thread_attach(nullptr), m_fn_domain_assembly_open(nullptr),
      m_fn_image_get_class_count(nullptr), m_fn_image_get_class(nullptr),
      m_classGameCoreCenter(nullptr), m_classPlayerBase(nullptr),
      m_classCamera(nullptr), m_classBall(nullptr), m_classTransform(nullptr),
      m_imageCSharp(nullptr), m_domain(nullptr),
      m_methodGetInstance(nullptr), m_methodGetCurrent(nullptr),
      m_methodGetName(nullptr), m_methodGetPosition(nullptr),
      m_methodGetOrthographicSize(nullptr),
      m_gameReady(false), m_threadAttached(false), m_counter(0), m_pcounter(0),
      m_gameInstance(nullptr) {}

// =============================================================================
// IL2CPP 初始化 — dlopen libil2cpp.so + 解析函数符号
// 对应 il2cpp/mod.rs 的 Il2CppApi::load()
// =============================================================================

bool ESPSystem::InitIL2CPP() {
    if (m_il2cppLoaded) return true;

    ESP_LOGI("ESP: 初始化 IL2CPP API...");
    APP_LOGI("ESP", "开始初始化 IL2CPP API");
    m_diagStatus = "初始化中... (查找 libil2cpp.so)";

    // === 方式 1 (主): 从 /proc/self/maps 查找已加载的 libil2cpp.so 基址 ===
    // 必须这样做, 因为外部注入的 .so 与游戏在不同 linker 命名空间,
    // dlopen("libil2cpp.so") 会失败 ("library not found")。
    APP_LOGI("ESP", "扫描 /proc/self/maps 查找 libil2cpp.so...");
    m_il2cppBase = ELFResolver::FindLibraryBase("libil2cpp.so");

    if (m_il2cppBase != 0) {
        ESP_LOGI("ESP: + 从 /proc/self/maps 找到 libil2cpp.so @ 0x%lx", m_il2cppBase);
        APP_LOGI("ESP", "找到 libil2cpp.so @ 0x%lx (来自 /proc/self/maps)", m_il2cppBase);
        m_diagStatus = "libil2cpp.so 已定位 (maps), 解析符号...";
    } else {
        // === 方式 2 (兜底): 尝试传统 dlopen (某些环境可能可用) ===
        APP_LOGW("ESP", "/proc/self/maps 未找到 libil2cpp.so, 尝试 dlopen...");
        ESP_LOGW("ESP: /proc/self/maps 未找到 libil2cpp.so, 尝试 dlopen...");
        m_il2cppHandle = dlopen("libil2cpp.so", RTLD_NOW);
        if (m_il2cppHandle) {
            ESP_LOGI("ESP: + dlopen libil2cpp.so 成功");
            APP_LOGI("ESP", "dlopen libil2cpp.so 成功");
            m_diagStatus = "libil2cpp.so 已加载 (dlopen), 解析符号...";
        } else {
            const char* err = dlerror();
            m_diagStatus = std::string("失败: libil2cpp.so 未加载 (maps+dlopen 均失败): ") + (err ? err : "?");
            ESP_LOGE("ESP: %s", m_diagStatus.c_str());
            APP_LOGE("ESP", "libil2cpp.so 加载失败: %s", err ? err : "未知错误");
            APP_LOGE("ESP", "可能原因: 1)游戏未启动 2)非IL2CPP构建(Mono?) 3)命名空间隔离");
            APP_LOGE("ESP", "诊断: 请查看日志标签页的 maps 内容, 确认游戏使用的运行时库");
            return false;
        }
    }

    if (!ResolveIL2CPPFunctions()) {
        m_diagStatus = "失败: 解析 IL2CPP 函数符号失败";
        ESP_LOGE("ESP: %s", m_diagStatus.c_str());
        APP_LOGE("ESP", "解析 IL2CPP 函数符号失败 (见上方具体哪个符号失败)");
        return false;
    }
    APP_LOGI("ESP", "所有 IL2CPP 函数符号解析成功");
    m_diagStatus = "符号解析完成, 查找游戏类...";

    if (!FindGameClasses()) {
        ESP_LOGE("ESP: 查找游戏类失败");
        APP_LOGE("ESP", "查找游戏类失败 (见上方日志)");
        return false;
    }
    APP_LOGI("ESP", "游戏类查找完成");
    m_diagStatus = "游戏类已找到, 初始化字段偏移...";

    if (!InitFieldOffsets()) {
        ESP_LOGE("ESP: 初始化字段偏移失败");
        APP_LOGE("ESP", "初始化字段偏移失败");
        return false;
    }

    m_il2cppLoaded = true;
    m_diagStatus = "IL2CPP 初始化完成, 等待游戏就绪...";
    ESP_LOGI("ESP: IL2CPP API 初始化成功!");
    APP_LOGI("ESP", "IL2CPP API 初始化全部完成, 等待游戏进入对局");
    return true;
}

// =============================================================================
// 解析 IL2CPP 函数符号
// 对应 il2cpp/mod.rs 的 Il2CppApi::resolve()
// =============================================================================

bool ESPSystem::ResolveIL2CPPFunctions() {
    APP_LOGI("ESP", "=== 开始解析 IL2CPP 函数符号 (共 17 个核心 + 2 可选) ===");
    APP_LOGI("ESP", "libil2cppBase=0x%lx, il2cppHandle=%p", m_il2cppBase, m_il2cppHandle);

    // 通用解析: 优先 ELF (绕过命名空间隔离), dlopen handle 兜底
    // 每个符号都记录详细日志, 失败时立即报告哪个符号找不到
    #define ESP_RESOLVE(name, type) \
        do { \
            const char* sym = "il2cpp_" #name; \
            APP_LOGI("ESP", "解析符号 [%s] ...", sym); \
            if (m_il2cppBase != 0) { \
                m_fn_##name = (type)ELFResolver::LookupSymbol(m_il2cppBase, sym); \
                if (m_fn_##name) { \
                    APP_LOGI("ESP", "  ✓ ELF 查找成功 [%s] @ %p", sym, (void*)m_fn_##name); \
                } else { \
                    APP_LOGW("ESP", "  ✗ ELF 查找失败 [%s] (符号不在 .dynsym/.symtab 或节头被剥离)", sym); \
                } \
            } \
            if (!m_fn_##name && m_il2cppHandle) { \
                m_fn_##name = (type)dlsym(m_il2cppHandle, sym); \
                if (m_fn_##name) { \
                    APP_LOGI("ESP", "  ✓ dlsym 兜底成功 [%s] @ %p", sym, (void*)m_fn_##name); \
                } \
            } \
            if (!m_fn_##name) { \
                APP_LOGE("ESP", "  ✗✗✗ 无法解析符号 [%s] — 这是失败原因! ELF 和 dlsym 都失败", sym); \
                APP_LOGE("ESP", "  建议: 检查 libil2cpp.so 是否被 strip 节头, 或符号 visibility=hidden"); \
                APP_LOGI("ESP", "  → 转储 .dynsym 符号表帮助诊断 (检查符号是否被混淆/重命名)..."); \
                ELFResolver::DumpDynSymbols("libil2cpp.so"); \
                ESP_LOGE("ESP: 无法解析 %s", sym); \
                return false; \
            } \
            ESP_LOGD("ESP: + %s = %p", sym, (void*)m_fn_##name); \
        } while(0);

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
    ESP_RESOLVE(string_chars,          il2cpp_string_chars_t);
    ESP_RESOLVE(string_length,         il2cpp_string_length_t);
    ESP_RESOLVE(string_new,            il2cpp_string_new_t);
    ESP_RESOLVE(class_get_name,        il2cpp_class_get_name_t);
    ESP_RESOLVE(image_get_name,        il2cpp_image_get_name_t);
    ESP_RESOLVE(thread_attach,         il2cpp_thread_attach_t);
    ESP_RESOLVE(domain_assembly_open,  il2cpp_domain_assembly_open_t);
    // image_get_class_count / image_get_class 在某些版本可能不存在, 不强制
    if (m_il2cppBase != 0) {
        m_fn_image_get_class_count = (il2cpp_image_get_class_count_t)ELFResolver::LookupSymbol(m_il2cppBase, "il2cpp_image_get_class_count");
        m_fn_image_get_class       = (il2cpp_image_get_class_t)ELFResolver::LookupSymbol(m_il2cppBase, "il2cpp_image_get_class");
    }
    if (!m_fn_image_get_class_count && m_il2cppHandle) {
        m_fn_image_get_class_count = (il2cpp_image_get_class_count_t)dlsym(m_il2cppHandle, "il2cpp_image_get_class_count");
        m_fn_image_get_class       = (il2cpp_image_get_class_t)dlsym(m_il2cppHandle, "il2cpp_image_get_class");
    }
    ESP_LOGD("ESP: + il2cpp_image_get_class_count = %p (可选)", (void*)m_fn_image_get_class_count);
    ESP_LOGD("ESP: + il2cpp_image_get_class = %p (可选)", (void*)m_fn_image_get_class);

    #undef ESP_RESOLVE
    return true;
}

// =============================================================================
// 查找游戏类 — 遍历程序集查找 GameCoreCenter / PlayerBase / Camera
// 对应 data_reader.rs 的 init_field_offsets() 中的类查找逻辑
// =============================================================================

bool ESPSystem::FindGameClasses() {
    ESP_LOGI("ESP: 查找游戏类...");
    APP_LOGI("ESP", "=== 开始查找游戏类 ===");

    m_domain = m_fn_domain_get();
    if (!m_domain) {
        m_diagStatus = "失败: il2cpp_domain_get 返回 null";
        ESP_LOGE("ESP: %s", m_diagStatus.c_str());
        APP_LOGE("ESP", "il2cpp_domain_get() 返回 null — IL2CPP 运行时未初始化?");
        return false;
    }
    APP_LOGI("ESP", "il2cpp_domain_get() 成功, domain=%p", (void*)m_domain);

    // 方式 1 (优先): 用 domain_assembly_open 直接打开 Assembly-CSharp
    // 对应 game_direct_call.rs 的 open_assembly_image(domain, "Assembly-CSharp")
    if (m_fn_domain_assembly_open) {
        APP_LOGI("ESP", "尝试 domain_assembly_open(\"Assembly-CSharp\")...");
        const Il2CppAssembly* assembly = m_fn_domain_assembly_open(m_domain, "Assembly-CSharp");
        if (assembly) {
            m_imageCSharp = m_fn_assembly_get_image(assembly);
            ESP_LOGI("ESP: + domain_assembly_open 成功打开 Assembly-CSharp, image=%p", (void*)m_imageCSharp);
            APP_LOGI("ESP", "domain_assembly_open 成功, image=%p", (void*)m_imageCSharp);
        } else {
            ESP_LOGW("ESP: domain_assembly_open(\"Assembly-CSharp\") 返回 null, 尝试遍历方式...");
            APP_LOGW("ESP", "domain_assembly_open 返回 null, 改用遍历方式");
        }
    } else {
        APP_LOGW("ESP", "domain_assembly_open 函数指针为空, 跳过此方式");
    }

    // 方式 2 (兜底): 遍历所有程序集查找包含 GameCoreCenter 的 image
    if (!m_imageCSharp) {
        size_t count = 0;
        const Il2CppAssembly** assemblies = m_fn_domain_get_assemblies(m_domain, &count);
        if (!assemblies || count == 0) {
            m_diagStatus = "失败: 无可用程序集";
            ESP_LOGE("ESP: %s", m_diagStatus.c_str());
            APP_LOGE("ESP", "domain_get_assemblies 返回空 (count=%zu, ptr=%p)", count, (void*)assemblies);
            return false;
        }
        ESP_LOGI("ESP: 共 %zu 个程序集, 遍历查找 Assembly-CSharp...", count);
        APP_LOGI("ESP", "共 %zu 个程序集, 遍历查找含 GameCoreCenter 的 image", count);

        for (size_t i = 0; i < count; i++) {
            if (!assemblies[i]) continue;
            Il2CppImage* img = m_fn_assembly_get_image(assemblies[i]);
            if (!img) continue;

            // 打印每个 image 的名字帮助诊断
            const char* imgName = m_fn_image_get_name ? m_fn_image_get_name(img) : nullptr;
            if (imgName) {
                // 仅打印非系统程序集, 避免日志过多
                if (strstr(imgName, "mscorlib")==nullptr && strstr(imgName, "System")==nullptr &&
                    strstr(imgName, "UnityEngine")==nullptr) {
                    APP_LOGI("ESP", "  程序集 #%zu: %s", i, imgName);
                }
            }
            Il2CppClass* testClass = m_fn_class_from_name(img, "", "GameCoreCenter");
            if (testClass) {
                m_imageCSharp = img;
                ESP_LOGI("ESP: + 在程序集 #%zu (%s) 中找到 GameCoreCenter", i, imgName ? imgName : "?");
                APP_LOGI("ESP", "✓ 在程序集 #%zu (%s) 中找到 GameCoreCenter, image=%p",
                    i, imgName ? imgName : "?", (void*)img);
                break;
            }
        }
    }

    if (!m_imageCSharp) {
        m_diagStatus = "失败: 未找到包含 GameCoreCenter 的程序集 (Assembly-CSharp)";
        ESP_LOGE("ESP: %s", m_diagStatus.c_str());
        ESP_LOGE("ESP:   可能原因: 1)游戏尚未完全加载 2)类名不同 3)非IL2CPP构建");
        APP_LOGE("ESP", "未找到 GameCoreCenter — 可能游戏未完全加载或非 IL2CPP 构建");
        return false;
    }

    // --- 查找 GameCoreCenter (全局命名空间 "") ---
    m_classGameCoreCenter = m_fn_class_from_name(m_imageCSharp, "", "GameCoreCenter");
    if (m_classGameCoreCenter) {
        ESP_LOGI("ESP: + 找到 GameCoreCenter (namespace=\"\")");
        APP_LOGI("ESP", "✓ 找到类 GameCoreCenter (namespace=\"\", class=%p)", (void*)m_classGameCoreCenter);
    } else {
        m_diagStatus = "失败: GameCoreCenter 类未找到";
        ESP_LOGE("ESP: %s", m_diagStatus.c_str());
        APP_LOGE("ESP", "GameCoreCenter 类查找失败");
        return false;
    }

    // --- 查找 PlayerBase (全局命名空间 "") ---
    m_classPlayerBase = m_fn_class_from_name(m_imageCSharp, "", "PlayerBase");
    if (m_classPlayerBase) {
        ESP_LOGI("ESP: + 找到 PlayerBase (namespace=\"\")");
        APP_LOGI("ESP", "✓ 找到类 PlayerBase (class=%p)", (void*)m_classPlayerBase);
    } else {
        ESP_LOGW("ESP: 未找到 PlayerBase (可能类名不同, 尝试其他名字...)");
        APP_LOGW("ESP", "未找到 PlayerBase, 尝试兜底类名...");
        // 兜底: 尝试其他常见类名
        const char* altNames[] = {"Player", "Ball", "Cell", "Fish", "BallBase", "Actor"};
        for (const char* name : altNames) {
            m_classPlayerBase = m_fn_class_from_name(m_imageCSharp, "", name);
            if (m_classPlayerBase) {
                ESP_LOGI("ESP: + 用兜底名找到 %s, 作为 PlayerBase", name);
                APP_LOGI("ESP", "✓ 用兜底名 %s 作为 PlayerBase", name);
                break;
            }
        }
        if (!m_classPlayerBase) {
            ESP_LOGW("ESP: PlayerBase 及兜底名均未找到, 对象读取将不可用");
            APP_LOGW("ESP", "PlayerBase 及所有兜底名均未找到");
        }
    }

    // --- 查找 Camera (命名空间 "UnityEngine"!) ---
    // 关键修正: Camera 在 UnityEngine 命名空间下, 不是全局命名空间
    m_classCamera = m_fn_class_from_name(m_imageCSharp, "UnityEngine", "Camera");
    if (m_classCamera) {
        ESP_LOGI("ESP: + 找到 UnityEngine.Camera");
        APP_LOGI("ESP", "✓ 找到类 UnityEngine.Camera (class=%p)", (void*)m_classCamera);
    } else {
        ESP_LOGW("ESP: 未找到 UnityEngine.Camera, 尝试全局命名空间...");
        APP_LOGW("ESP", "未找到 UnityEngine.Camera, 尝试全局命名空间...");
        m_classCamera = m_fn_class_from_name(m_imageCSharp, "", "Camera");
        if (m_classCamera) {
            ESP_LOGI("ESP: + 找到 Camera (全局命名空间)");
            APP_LOGI("ESP", "✓ 找到 Camera (全局命名空间)");
        } else {
            ESP_LOGW("ESP: Camera 未找到, 世界坐标转换将不可用 (ESP 仍可绘制原始坐标)");
            APP_LOGW("ESP", "Camera 未找到, 世界坐标转换将不可用");
        }
    }

    // --- 查找方法 (逆向: "get_instance", "get_current", "get_name") ---
    if (m_classGameCoreCenter) {
        m_methodGetInstance = m_fn_class_get_method(m_classGameCoreCenter, "get_instance", 0);
        ESP_LOGI("ESP: GameCoreCenter.get_instance = %p", (void*)m_methodGetInstance);
        APP_LOGI("ESP", "GameCoreCenter.get_instance = %p", (void*)m_methodGetInstance);
    }
    if (m_classCamera) {
        m_methodGetCurrent = m_fn_class_get_method(m_classCamera, "get_current", 0);
        ESP_LOGI("ESP: Camera.get_current = %p", (void*)m_methodGetCurrent);
        APP_LOGI("ESP", "Camera.get_current = %p", (void*)m_methodGetCurrent);
        // 查找 get_orthographicSize (用于读取正交相机缩放, 修复圆圈太小问题)
        if (!m_methodGetOrthographicSize) {
            m_methodGetOrthographicSize = m_fn_class_get_method(m_classCamera, "get_orthographicSize", 0);
            ESP_LOGI("ESP: Camera.get_orthographicSize = %p", (void*)m_methodGetOrthographicSize);
            APP_LOGI("ESP", "Camera.get_orthographicSize = %p", (void*)m_methodGetOrthographicSize);
        }
    }
    if (m_classPlayerBase) {
        m_methodGetName = m_fn_class_get_method(m_classPlayerBase, "get_name", 0);
        ESP_LOGI("ESP: PlayerBase.get_name = %p", (void*)m_methodGetName);
        APP_LOGI("ESP", "PlayerBase.get_name = %p", (void*)m_methodGetName);
    }

    APP_LOGI("ESP", "=== 游戏类查找完成 ===");
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
    ESP_LOGI("ESP: 初始化字段偏移 (打印所有字段名 + 偏移, 帮助诊断)...");
    APP_LOGI("ESP", "=== 初始化字段偏移 (打印全部字段) ===");
    m_diagOffsets.clear();

    // --- 遍历 GameCoreCenter 字段 (打印全部, 按日志确认的字段名匹配) ---
    if (m_classGameCoreCenter) {
        ESP_LOGI("ESP: === GameCoreCenter 全部字段 ===");
        APP_LOGI("ESP", "--- GameCoreCenter 字段 ---");
        m_diagOffsets += "GameCoreCenter:\n";
        void* iter = nullptr;
        FieldInfo* field;
        while ((field = m_fn_class_get_fields(m_classGameCoreCenter, &iter)) != nullptr) {
            const char* fname = m_fn_field_get_name(field);
            size_t offset = m_fn_field_get_offset(field);
            if (!fname) continue;

            ESP_LOGI("ESP:   GCC.%s @ 0x%zx", fname, offset);
            APP_LOGI("ESP", "  GCC.%s @ 0x%zx", fname, offset);
            m_diagOffsets += std::string("  ") + fname + " @0x" + [&]{
                char b[32]; snprintf(b,sizeof(b),"%zx",offset); return std::string(b);}() + "\n";

            // 按日志确认的字段名匹配 (GameCoreCenter: BobMainScript.dll)
            if (strcmp(fname, "PlayerDic") == 0) {
                m_offsets.gcc_player_dic = offset;
            } else if (strcmp(fname, "CachedPlayerDic") == 0) {
                m_offsets.gcc_cached_player_dic = offset;
            } else if (strcmp(fname, "BallDic") == 0) {
                m_offsets.gcc_ball_dic = offset;
            } else if (strcmp(fname, "BallUpdateList") == 0) {
                m_offsets.gcc_ball_update_list = offset;
            } else if (strcmp(fname, "PersonList") == 0) {
                m_offsets.gcc_person_list = offset;
            } else if (strcmp(fname, "PersonDic") == 0) {
                m_offsets.gcc_person_dic = offset;
            } else if (strcmp(fname, "SelfPlayerID") == 0) {
                m_offsets.gcc_self_player_id = offset;
            } else if (strcmp(fname, "curPlayerID") == 0) {
                m_offsets.gcc_cur_player_id = offset;
            } else if (strcmp(fname, "Is3DRoomType") == 0 ||
                       strcmp(fname, "<Is3DRoomType>k__BackingField") == 0) {
                m_offsets.gcc_is_3d_room = offset;
            } else if (strcmp(fname, "Map3D") == 0) {
                m_offsets.gcc_map3d = offset;
            }
        }
    }

    // --- 遍历 PlayerBase 字段 (打印全部, 按日志确认的字段名匹配) ---
    if (m_classPlayerBase) {
        ESP_LOGI("ESP: === PlayerBase 全部字段 ===");
        APP_LOGI("ESP", "--- PlayerBase 字段 ---");
        m_diagOffsets += "PlayerBase:\n";
        void* iter = nullptr;
        FieldInfo* field;
        while ((field = m_fn_class_get_fields(m_classPlayerBase, &iter)) != nullptr) {
            const char* fname = m_fn_field_get_name(field);
            size_t offset = m_fn_field_get_offset(field);
            if (!fname) continue;

            ESP_LOGI("ESP:   PB.%s @ 0x%zx", fname, offset);
            APP_LOGI("ESP", "  PB.%s @ 0x%zx", fname, offset);
            m_diagOffsets += std::string("  ") + fname + " @0x" + [&]{
                char b[32]; snprintf(b,sizeof(b),"%zx",offset); return std::string(b);}() + "\n";

            // 按日志确认的字段名匹配 (PlayerBase)
            if (strcmp(fname, "Name") == 0) {
                m_offsets.pb_name = offset;
            } else if (strcmp(fname, "ID") == 0) {
                m_offsets.pb_id = offset;
            } else if (strcmp(fname, "_pos") == 0) {
                m_offsets.pb_pos = offset;
            } else if (strcmp(fname, "pos") == 0) {
                m_offsets.pb_pos2 = offset;
            } else if (strcmp(fname, "radius") == 0) {
                m_offsets.pb_radius = offset;
            } else if (strcmp(fname, "isAlive") == 0) {
                m_offsets.pb_is_alive = offset;
            } else if (strcmp(fname, "AllBallScore") == 0) {
                m_offsets.pb_score = offset;
            } else if (strcmp(fname, "Color") == 0) {
                m_offsets.pb_color = offset;
            } else if (strcmp(fname, "TeamID") == 0) {
                m_offsets.pb_team_id = offset;
            }
        }
    }

    m_offsets.initialized = true;
    ESP_LOGI("ESP: 字段偏移初始化完成");
    ESP_LOGI("ESP:   GCC: player_dic=0x%zu ball_dic=0x%zu self_pid=0x%zu cur_pid=0x%zu",
         m_offsets.gcc_player_dic, m_offsets.gcc_ball_dic,
         m_offsets.gcc_self_player_id, m_offsets.gcc_cur_player_id);
    ESP_LOGI("ESP:   PB: name=0x%zu id=0x%zu pos=0x%zu radius=0x%zu score=0x%zu isAlive=0x%zu",
         m_offsets.pb_name, m_offsets.pb_id, m_offsets.pb_pos,
         m_offsets.pb_radius, m_offsets.pb_score, m_offsets.pb_is_alive);
    APP_LOGI("ESP", "字段偏移: GCC.player_dic=0x%zu ball_dic=0x%zu self_pid=0x%zu",
        m_offsets.gcc_player_dic, m_offsets.gcc_ball_dic, m_offsets.gcc_self_player_id);
    APP_LOGI("ESP", "字段偏移: PB.name=0x%zu id=0x%zu pos=0x%zu radius=0x%zu score=0x%zu",
        m_offsets.pb_name, m_offsets.pb_id, m_offsets.pb_pos,
        m_offsets.pb_radius, m_offsets.pb_score);

    // 诊断: 如果关键字典字段没找到, 提示
    if (m_offsets.gcc_player_dic == 0 && m_offsets.gcc_ball_dic == 0) {
        m_diagStatus = "警告: 未匹配到 PlayerDic/BallDic 字段, 请查看 logcat 中 ESP 的字段列表";
        ESP_LOGW("ESP: %s", m_diagStatus.c_str());
        APP_LOGW("ESP", "未匹配到 PlayerDic/BallDic — 位置数据主要从 BallDic 获取");
    }

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
    // 失败后不要每帧重试 (会刷爆日志缓冲), 每 300 帧 (约 5 秒@60fps) 重试一次
    if (!m_il2cppLoaded) {
        static uint32_t s_lastInitAttempt = 0;
        if (s_lastInitAttempt != 0 && (c - s_lastInitAttempt) < 300) {
            return; // 冷却中, 静默跳过 (不打印日志)
        }
        s_lastInitAttempt = c;
        if (!InitIL2CPP()) {
            // 失败一次后, 每 300 帧才提示一次重试
            APP_LOGW("ESP", "InitIL2CPP 失败, 将在约 5 秒后重试 (避免日志刷屏)");
            return;
        }
    }

    // 关键: 当前线程必须 attach 到 IL2CPP 运行时, 否则 runtime_invoke 会崩溃
    // 对应 game_direct_call.rs init() 中的 api.attach_thread(domain)
    if (!m_threadAttached.load() && m_fn_thread_attach && m_domain) {
        void* thread = m_fn_thread_attach(m_domain);
        if (thread) {
            m_threadAttached.store(true);
            ESP_LOGI("ESP: + 当前线程已 attach 到 IL2CPP (thread=%p)", thread);
            APP_LOGI("ESP", "✓ 当前线程已 attach 到 IL2CPP (thread=%p)", thread);
        } else {
            // attach 失败, 每 60 帧重试一次
            if (c % 60 == 0) {
                ESP_LOGW("ESP: il2cpp_thread_attach 失败, 将重试...");
                APP_LOGW("ESP", "il2cpp_thread_attach 失败, 将每 60 帧重试");
            }
            return;
        }
    }

    // 获取 GameCoreCenter 单例 (逆向: GameCoreCenter.get_instance())
    if (!m_gameInstance) {
        if (m_methodGetInstance) {
            Il2CppException* exc = nullptr;
            Il2CppObject* result = m_fn_runtime_invoke(m_methodGetInstance, nullptr, nullptr, &exc);
            if (exc) {
                if (c % 120 == 0) {
                    ESP_LOGW("ESP: get_instance 抛出异常 (游戏可能未进入对局)");
                    APP_LOGW("ESP", "get_instance 抛出异常 (游戏可能未进入对局)");
                }
                return;
            }
            if (result) {
                m_gameInstance = result;
                // 逆向字符串: "game_settings: GameCoreCenter.get_instance detected, game ready!"
                m_gameReady = true;
                m_diagStatus = "游戏就绪! 正在读取对象...";
                ESP_LOGI("ESP: + GameCoreCenter.get_instance 检测到, 游戏就绪!");
                ESP_LOGI("ESP:   data_reader auto-enabled, direct_call init requested");
                APP_LOGI("ESP", "✓✓✓ GameCoreCenter.get_instance 检测到, 游戏就绪! instance=%p", (void*)result);
            } else {
                if (c % 120 == 0) {
                    ESP_LOGD("ESP: get_instance 返回 null (游戏未进入对局, 等待...)");
                    APP_LOGD("ESP", "get_instance 返回 null (游戏未进入对局, 等待...)");
                }
                return;
            }
        } else {
            m_diagStatus = "失败: get_instance 方法未找到";
            APP_LOGE("ESP", "get_instance 方法未找到");
            return;
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

    // 优先: 用 runtime_invoke 调用 get_orthographicSize() 方法 (返回装箱的 float)
    if (m_methodGetOrthographicSize) {
        Il2CppException* exc2 = nullptr;
        Il2CppObject* result = m_fn_runtime_invoke(m_methodGetOrthographicSize, camObj, nullptr, &exc2);
        if (result && !exc2) {
            // 返回值是装箱的 float, 跳过对象头(16字节)读取
            float val = *(float*)((char*)result + 16);
            if (val > 0 && val < 100000) {
                m_camera.zoom = val;
                m_camera.valid = true;
                if (m_counter.load() % 240 == 0) {
                    APP_LOGI("ESP", "Camera.zoom (via get_orthographicSize) = %.2f", val);
                }
            }
        } else if (m_counter.load() % 240 == 0) {
            APP_LOGW("ESP", "get_orthographicSize 调用失败/返回 null, 将回退字段读取");
        }
    }

    // 备用: 字段读取 (backing field) — 当 runtime_invoke 不可用或失败时
    if (m_camera.zoom <= 0) {
        Il2CppClass* camClass = m_fn_object_get_class(camObj);
        if (camClass) {
            void* iter = nullptr;
            FieldInfo* field;
            while ((field = m_fn_class_get_fields(camClass, &iter)) != nullptr) {
                const char* fname = m_fn_field_get_name(field);
                size_t offset = m_fn_field_get_offset(field);
                if (!fname || offset == 0) continue;
                if (strcmp(fname, "orthographicSize") == 0 ||
                    strcmp(fname, "<orthographicSize>k__BackingField") == 0 ||
                    strcmp(fname, "m_OrthographicSize") == 0 ||
                    strcmp(fname, "zoom") == 0 || strcmp(fname, "halfHeight") == 0) {
                    float val = *(float*)((char*)camObj + offset);
                    if (val > 0 && val < 100000) {
                        m_camera.zoom = val;
                        m_camera.valid = true;
                    }
                }
            }
        }
    }

    // 设置屏幕尺寸 (从 ImGui IO 获取)
    ImGuiIO& io = ImGui::GetIO();
    m_camera.screen_w = io.DisplaySize.x;
    m_camera.screen_h = io.DisplaySize.y;

    // 如果没有读到 zoom, 标记无效 (Render 会动态兜底计算)
    if (m_camera.zoom <= 0) {
        m_camera.valid = false;
    }

    if (m_counter.load() % 120 == 0) {
        APP_LOGI("ESP", "Camera: zoom=%.1f valid=%d screen=%.0fx%.0f",
            m_camera.zoom, (int)m_camera.valid, m_camera.screen_w, m_camera.screen_h);
    }
}

// =============================================================================
// 读取游戏对象 — 优先从 BallDic 读取 (球体有半径+Transform位置)
// 对应 data_reader.rs 的 read_objects()
//
// IL2CPP 调用链:
//   GameCoreCenter 实例 → 读取 SelfPlayerID / BallDic / PlayerDic 字段
//   BallDic 是 Dictionary<int, DrawCircle>, 遍历 _entries 数组取每个 Entry.value
//   对每个 DrawCircle: 读 Radius/SelfTF(→Transform.get_position)/ID/curScore
//   若 BallDic 不可用, 回退到 PlayerDic (Dictionary<int, PlayerBase>)
// =============================================================================

void ESPSystem::ReadGameObjects() {
    if (!m_offsets.initialized || !m_gameInstance) return;

    std::vector<GameObjectInfo> newObjects;
    uint32_t c = m_counter.load();

    // --- 读取 SelfPlayerID (识别自身) ---
    int selfPlayerId = -1;
    if (m_offsets.gcc_self_player_id) {
        selfPlayerId = *(int*)((char*)m_gameInstance + m_offsets.gcc_self_player_id);
    }
    if (selfPlayerId <= 0 && m_offsets.gcc_cur_player_id) {
        int cur = *(int*)((char*)m_gameInstance + m_offsets.gcc_cur_player_id);
        if (cur > 0) selfPlayerId = cur;
    }
    if (c % 120 == 0) {
        APP_LOGI("ESP", "ReadGameObjects: selfPlayerId=%d", selfPlayerId);
    }

    // --- 优先: BallDic (球体有半径和 Transform 世界坐标) ---
    bool ballRead = false;
    void* ballDic = nullptr;
    if (m_offsets.gcc_ball_dic) {
        ballDic = *(void**)((char*)m_gameInstance + m_offsets.gcc_ball_dic);
    }
    if (ballDic) {
        // 首次: 查找 Ball(DrawCircle)/Transform 类与方法
        if (!m_classBall || !m_methodGetPosition) {
            FindBallAndTransformClasses();
        }
        if (m_classBall) {
            size_t before = newObjects.size();
            TryReadFromBallDic(ballDic, selfPlayerId, newObjects, c);
            if (newObjects.size() > before) {
                ballRead = true;
            } else if (c % 120 == 0) {
                APP_LOGW("ESP", "BallDic 存在但未读取到任何球体 (可能 entries 为空或偏移有误)");
            }
        } else if (c % 120 == 0) {
            APP_LOGW("ESP", "BallDic 存在但 DrawCircle 类未找到, 跳过 BallDic");
        }
    } else if (c % 120 == 0) {
        APP_LOGD("ESP", "BallDic 为 null (offset=0x%zu), 尝试 PlayerDic", m_offsets.gcc_ball_dic);
    }

    // --- 备用: PlayerDic (BallDic 不可用或无数据时) ---
    if (!ballRead && m_offsets.gcc_player_dic) {
        void* playerDic = *(void**)((char*)m_gameInstance + m_offsets.gcc_player_dic);
        if (playerDic) {
            DiscoverDicOffsets(playerDic, m_offsets, m_fn_object_get_class,
                m_fn_class_get_fields, m_fn_field_get_name, m_fn_field_get_offset);
            size_t eOff = m_offsets.dic_entries ? m_offsets.dic_entries : 0x18;
            size_t cOff = m_offsets.dic_count ? m_offsets.dic_count : 0x20;
            Il2CppArray* entries = *(Il2CppArray**)((char*)playerDic + eOff);
            int count = *(int*)((char*)playerDic + cOff);
            if (entries && count > 0) {
                size_t arrLen = m_fn_array_length(entries);
                size_t valueOff = (12 + sizeof(void*) - 1) & ~(sizeof(void*) - 1);
                size_t entrySize = valueOff + sizeof(void*);
                size_t scanLimit = arrLen < 500 ? arrLen : 500;
                int effectiveSelfId = (selfPlayerId > 0) ? selfPlayerId : config.self_rank_id;
                for (size_t i = 0; i < scanLimit; i++) {
                    char* entry = (char*)entries + kIl2CppArrayDataOffset + i * entrySize;
                    Il2CppObject* pbObj = *(Il2CppObject**)(entry + valueOff);
                    if (!pbObj) continue;

                    GameObjectInfo info;
                    info.name = ReadObjectName(pbObj);
                    if (m_offsets.pb_id) info.object_id = *(int*)((char*)pbObj + m_offsets.pb_id);
                    // 位置: _pos 是 Vector3 (日志确认值常为0, 位置主要从 BallDic 获取, 此处保留读取)
                    if (m_offsets.pb_pos) {
                        float* p = (float*)((char*)pbObj + m_offsets.pb_pos);
                        info.world_x = p[0];
                        info.world_y = p[1];
                        info.world_z = p[2];
                    }
                    if (m_offsets.pb_radius) info.radius = *(float*)((char*)pbObj + m_offsets.pb_radius);
                    if (m_offsets.pb_score) info.score = *(int*)((char*)pbObj + m_offsets.pb_score);
                    if (m_offsets.pb_is_alive) info.is_alive = *(bool*)((char*)pbObj + m_offsets.pb_is_alive);
                    else info.is_alive = true;
                    info.is_self = (effectiveSelfId > 0 && info.object_id == effectiveSelfId);
                    info.color = GetObjectColor(info);

                    if (c % 120 == 0 && i == 0) {
                        APP_LOGI("ESP", "首Player[name=%s id=%d r=%.2f score=%d self=%d]",
                            info.name.c_str(), info.object_id, info.radius,
                            info.score, (int)info.is_self);
                    }
                    newObjects.push_back(std::move(info));
                }
                if (c % 120 == 0) {
                    APP_LOGI("ESP", "PlayerDic: count=%d, 读取对象数=%zu", count, newObjects.size());
                }
            } else if (c % 120 == 0) {
                APP_LOGD("ESP", "PlayerDic entries 为空 (count=%d)", count);
            }
        }
    }

    // 更新缓存
    {
        std::lock_guard<std::mutex> lock(m_objectMutex);
        m_objects = std::move(newObjects);
    }

    // 更新诊断状态
    if (c % 60 == 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "游戏就绪, 对象数=%zu", m_objects.size());
        m_diagStatus = buf;
    }
}

// =============================================================================
// 查找 Ball(DrawCircle) 与 UnityEngine.Transform 类, 以及相关方法
// 从 BallDic 第一个对象用 object_get_class 获取 DrawCircle 类;
// 遍历程序集找 UnityEngine.Transform; 查找 get_position / get_orthographicSize
// =============================================================================

bool ESPSystem::FindBallAndTransformClasses() {
    if (m_classBall && m_classTransform && m_methodGetPosition) return true;
    APP_LOGI("ESP", "=== 查找 Ball(DrawCircle)/Transform 类 ===");

    // 从 BallDic 第一个对象获取 DrawCircle 类
    if (!m_classBall && m_offsets.gcc_ball_dic && m_gameInstance) {
        void* ballDic = *(void**)((char*)m_gameInstance + m_offsets.gcc_ball_dic);
        if (ballDic) {
            DiscoverDicOffsets(ballDic, m_offsets, m_fn_object_get_class,
                m_fn_class_get_fields, m_fn_field_get_name, m_fn_field_get_offset);
            size_t eOff = m_offsets.dic_entries ? m_offsets.dic_entries : 0x18;
            Il2CppArray* entries = *(Il2CppArray**)((char*)ballDic + eOff);
            if (entries) {
                size_t arrLen = m_fn_array_length(entries);
                size_t valueOff = (12 + sizeof(void*) - 1) & ~(sizeof(void*) - 1);
                size_t entrySize = valueOff + sizeof(void*);
                for (size_t i = 0; i < arrLen && i < 64; i++) {
                    char* entry = (char*)entries + kIl2CppArrayDataOffset + i * entrySize;
                    Il2CppObject* val = *(Il2CppObject**)(entry + valueOff);
                    if (!val) continue;
                    Il2CppClass* bc = m_fn_object_get_class(val);
                    if (bc) {
                        m_classBall = bc;
                        const char* cn = m_fn_class_get_name ? m_fn_class_get_name(bc) : "?";
                        APP_LOGI("ESP", "✓ BallDic 首对象类 = %s (class=%p)",
                            cn ? cn : "?", (void*)bc);
                        InitBallFieldOffsets(bc);
                        break;
                    }
                }
            }
        }
    }

    // 查找 UnityEngine.Transform 类 (可能在 UnityEngine.CoreModule 等程序集)
    if (!m_classTransform && m_domain) {
        if (m_imageCSharp) {
            m_classTransform = m_fn_class_from_name(m_imageCSharp, "UnityEngine", "Transform");
        }
        if (!m_classTransform) {
            size_t count = 0;
            const Il2CppAssembly** assemblies = m_fn_domain_get_assemblies(m_domain, &count);
            for (size_t i = 0; i < count && !m_classTransform; i++) {
                if (!assemblies[i]) continue;
                Il2CppImage* img = m_fn_assembly_get_image(assemblies[i]);
                if (!img) continue;
                m_classTransform = m_fn_class_from_name(img, "UnityEngine", "Transform");
                if (m_classTransform) {
                    const char* imgName = m_fn_image_get_name ? m_fn_image_get_name(img) : "?";
                    APP_LOGI("ESP", "✓ 在 %s 中找到 UnityEngine.Transform",
                        imgName ? imgName : "?");
                }
            }
        } else {
            APP_LOGI("ESP", "✓ 找到 UnityEngine.Transform (Assembly-CSharp)");
        }
    }

    // 查找 Transform.get_position
    if (m_classTransform && !m_methodGetPosition) {
        m_methodGetPosition = m_fn_class_get_method(m_classTransform, "get_position", 0);
        APP_LOGI("ESP", "Transform.get_position = %p", (void*)m_methodGetPosition);
    }

    // 补查 Camera.get_orthographicSize (若 FindGameClasses 未找到)
    if (m_classCamera && !m_methodGetOrthographicSize) {
        m_methodGetOrthographicSize = m_fn_class_get_method(m_classCamera, "get_orthographicSize", 0);
        APP_LOGI("ESP", "Camera.get_orthographicSize (补查) = %p",
            (void*)m_methodGetOrthographicSize);
    }

    APP_LOGI("ESP", "=== Ball/Transform 查找完成: Ball=%p Transform=%p getPos=%p ===",
        (void*)m_classBall, (void*)m_classTransform, (void*)m_methodGetPosition);
    return (m_classBall != nullptr);
}

// =============================================================================
// 初始化 Ball(DrawCircle) 字段偏移 — 按日志确认的字段名匹配
// Radius@0x028, SelfTF@0xb8, ID@0x10, curScore@0x70, pos@0x170
// =============================================================================

bool ESPSystem::InitBallFieldOffsets(Il2CppClass* ballClass) {
    if (!ballClass) return false;
    const char* cn = m_fn_class_get_name ? m_fn_class_get_name(ballClass) : "?";
    APP_LOGI("ESP", "--- Ball(%s) 字段 ---", cn ? cn : "?");
    m_diagOffsets += std::string("Ball(") + (cn ? cn : "?") + "):\n";
    void* iter = nullptr;
    FieldInfo* field;
    while ((field = m_fn_class_get_fields(ballClass, &iter)) != nullptr) {
        const char* fname = m_fn_field_get_name(field);
        size_t offset = m_fn_field_get_offset(field);
        if (!fname) continue;
        APP_LOGI("ESP", "  Ball.%s @ 0x%zx", fname, offset);
        m_diagOffsets += std::string("  ") + fname + " @0x" + [&]{
            char b[32]; snprintf(b,sizeof(b),"%zx",offset); return std::string(b);}() + "\n";

        // 半径字段 (多种命名)
        if (strcmp(fname, "Radius") == 0 || strcmp(fname, "radius") == 0 ||
            strcmp(fname, "Size") == 0 || strcmp(fname, "size") == 0 ||
            strcmp(fname, "mass") == 0) {
            m_offsets.ball_radius = offset;
        }
        // Transform 引用字段 (用于获取世界坐标)
        else if (strcmp(fname, "SelfTF") == 0 || strcmp(fname, "selfTF") == 0 ||
                 strcmp(fname, "tf") == 0 || strcmp(fname, "TF") == 0 ||
                 strcmp(fname, "transform") == 0 || strcmp(fname, "Transform") == 0 ||
                 strcmp(fname, "selfTf") == 0 || strcmp(fname, "_transform") == 0 ||
                 strcmp(fname, "m_Transform") == 0 || strcmp(fname, "SelfTf") == 0) {
            m_offsets.ball_self_tf = offset;
        }
        // ID 字段
        else if (strcmp(fname, "ID") == 0 || strcmp(fname, "id") == 0 ||
                 strcmp(fname, "ballId") == 0 || strcmp(fname, "BallID") == 0 ||
                 strcmp(fname, "Ballid") == 0 || strcmp(fname, "Id") == 0) {
            m_offsets.ball_id = offset;
        }
        // 分数字段
        else if (strcmp(fname, "curScore") == 0 || strcmp(fname, "score") == 0 ||
                 strcmp(fname, "Score") == 0 || strcmp(fname, "weight") == 0) {
            m_offsets.ball_score = offset;
        }
        // pos 字段 (Vector3, 通常值为 0, 位置在 Transform 里)
        else if (strcmp(fname, "pos") == 0 || strcmp(fname, "Pos") == 0 ||
                 strcmp(fname, "_pos") == 0 || strcmp(fname, "position") == 0) {
            m_offsets.ball_pos = offset;
        }
    }
    APP_LOGI("ESP", "Ball 偏移: radius=0x%zu self_tf=0x%zu id=0x%zu score=0x%zu pos=0x%zu",
        m_offsets.ball_radius, m_offsets.ball_self_tf,
        m_offsets.ball_id, m_offsets.ball_score, m_offsets.ball_pos);
    return m_offsets.ball_radius != 0;
}

// =============================================================================
// 获取 Transform 世界坐标 — 调用 Transform.get_position() via runtime_invoke
// 返回装箱的 Vector3, 跳过对象头(16字节)读 x/y/z
// =============================================================================

bool ESPSystem::GetTransformPosition(Il2CppObject* transformObj,
    float& outX, float& outY, float& outZ) {
    outX = outY = outZ = 0;
    if (!transformObj || !m_methodGetPosition || !m_fn_runtime_invoke) return false;

    Il2CppException* exc = nullptr;
    Il2CppObject* result = m_fn_runtime_invoke(m_methodGetPosition, transformObj, nullptr, &exc);
    if (!result || exc) return false;

    // 装箱的 Vector3: 跳过对象头(16字节)读取 x/y/z 三个 float
    float* v = (float*)((char*)result + 16);
    outX = v[0];
    outY = v[1];
    outZ = v[2];
    return true;
}

// =============================================================================
// 从 BallDic 读取球体对象 — 遍历 Dictionary._entries 数组
// 动态查找 Dictionary 偏移 (首次), 对每个 Entry.value(DrawCircle) 调用 FillBallObject
// =============================================================================

bool ESPSystem::TryReadFromBallDic(void* dicObj, int selfPlayerId,
    std::vector<GameObjectInfo>& out, uint32_t c) {
    if (!dicObj) return false;

    // 动态查找 Dictionary 偏移 (首次)
    DiscoverDicOffsets(dicObj, m_offsets, m_fn_object_get_class,
        m_fn_class_get_fields, m_fn_field_get_name, m_fn_field_get_offset);

    size_t eOff = m_offsets.dic_entries ? m_offsets.dic_entries : 0x18;
    size_t cOff = m_offsets.dic_count ? m_offsets.dic_count : 0x20;

    Il2CppArray* entries = *(Il2CppArray**)((char*)dicObj + eOff);
    int count = *(int*)((char*)dicObj + cOff);
    if (!entries || count <= 0) return false;

    size_t arrLen = m_fn_array_length(entries);
    // Entry 布局: { hashCode(4) next(4) key(4) [pad] value }; value偏移对齐到 sizeof(void*)
    size_t valueOff = (12 + sizeof(void*) - 1) & ~(sizeof(void*) - 1);
    size_t entrySize = valueOff + sizeof(void*);

    if (c % 120 == 0) {
        APP_LOGI("ESP", "BallDic: count=%d entries数组长度=%zu entrySize=%zu valueOff=%zu",
            count, arrLen, entrySize, valueOff);
    }

    size_t scanLimit = arrLen < 500 ? arrLen : 500;
    bool added = false;
    for (size_t i = 0; i < scanLimit; i++) {
        char* entry = (char*)entries + kIl2CppArrayDataOffset + i * entrySize;
        Il2CppObject* ballObj = *(Il2CppObject**)(entry + valueOff);
        if (!ballObj) continue;
        FillBallObject(ballObj, selfPlayerId, out, c, i);
        added = true;
    }
    return added;
}

// =============================================================================
// 填充单个球体对象信息 — 读 Radius/SelfTF(→位置)/ID/curScore
// =============================================================================

void ESPSystem::FillBallObject(Il2CppObject* ballObj, int selfPlayerId,
    std::vector<GameObjectInfo>& out, uint32_t c, size_t idx) {
    if (!ballObj) return;
    GameObjectInfo info;

    // 首次遇到 ball 对象时, 用 object_get_class 拿真实类并解析字段偏移
    // 解决问题: BallDic 里的对象类名未知 (可能不叫 "Ball"),
    //          之前 FindBallAndTransformClasses 在 BallDic 空时没找到类
    if (m_offsets.ball_radius == 0 && m_offsets.ball_self_tf == 0) {
        Il2CppClass* bc = m_fn_object_get_class(ballObj);
        if (bc) {
            const char* cn = m_fn_class_get_name ? m_fn_class_get_name(bc) : "?";
            APP_LOGW("ESP", "Ball 偏移未初始化! 首对象类=%s, 现场解析字段...", cn ? cn : "?");
            m_classBall = bc;
            InitBallFieldOffsets(bc);
            // 同时补查 Transform 类和方法 (如果还没查)
            if (!m_methodGetPosition) {
                FindBallAndTransformClasses();
            }
        }
    }

    // 读 Radius
    if (m_offsets.ball_radius) {
        info.radius = *(float*)((char*)ballObj + m_offsets.ball_radius);
    }

    // 读 SelfTF 并调用 Transform.get_position 获取世界坐标
    if (m_offsets.ball_self_tf) {
        Il2CppObject* tfObj = *(Il2CppObject**)((char*)ballObj + m_offsets.ball_self_tf);
        if (tfObj) {
            float tx, ty, tz;
            if (GetTransformPosition(tfObj, tx, ty, tz)) {
                info.world_x = tx;
                info.world_y = ty;
                info.world_z = tz;
            }
        }
    } else if (m_offsets.ball_pos) {
        // 备用: 直接读 pos 字段 (Vector3)
        float* p = (float*)((char*)ballObj + m_offsets.ball_pos);
        info.world_x = p[0];
        info.world_y = p[1];
        info.world_z = p[2];
    }

    // 读 ID
    if (m_offsets.ball_id) {
        info.object_id = *(int*)((char*)ballObj + m_offsets.ball_id);
    }

    // 读 curScore
    if (m_offsets.ball_score) {
        info.score = *(int*)((char*)ballObj + m_offsets.ball_score);
    }

    // 球体默认存活
    info.is_alive = true;

    // 识别自身: ball ID == selfPlayerId (回退到配置的 self_rank_id)
    int effectiveSelfId = (selfPlayerId > 0) ? selfPlayerId : config.self_rank_id;
    info.is_self = (effectiveSelfId > 0 && info.object_id == effectiveSelfId);

    // 名称 (球体可能无名称字段, ReadObjectName 会兜底返回空串)
    info.name = ReadObjectName(ballObj);

    info.color = GetObjectColor(info);

    if (c % 120 == 0 && idx == 0) {
        APP_LOGI("ESP", "首Ball: x=%.1f y=%.1f r=%.2f id=%d score=%d self=%d name=%s",
            info.world_x, info.world_y, info.radius, info.object_id,
            info.score, (int)info.is_self, info.name.c_str());
    }

    out.push_back(std::move(info));
}

// =============================================================================
// 读取对象名称 — 通过 get_name 方法或字段偏移
// 逆向字符串: "get_name", "name=", "name=%s"
//
// 优先通过 il2cpp_runtime_invoke 调用 get_name(),
// 名称存储为 IL2CPP 托管字符串, 用 string_chars + string_length 手动转 UTF16→UTF8
// (旧版 il2cpp_string_to_utf8 符号在部分 IL2CPP 版本不可用)
// =============================================================================

std::string ESPSystem::ReadObjectName(Il2CppObject* obj) {
    if (!obj) return "";

    // 方法 1: 通过 runtime_invoke 调用 get_name() (逆向字符串: "get_name")
    if (m_methodGetName) {
        Il2CppException* exc = nullptr;
        Il2CppObject* nameObj = m_fn_runtime_invoke(m_methodGetName, obj, nullptr, &exc);
        if (nameObj && !exc) {
            // 将 IL2CPP 托管字符串 (UTF16) 手动转换为 UTF8
            return Il2CppStringToUtf8Impl((Il2CppString*)nameObj,
                m_fn_string_chars, m_fn_string_length);
        }
    }

    // 方法 2: 通过字段偏移读取名称字符串引用
    if (m_offsets.pb_name) {
        Il2CppString* nameStr = *(Il2CppString**)((char*)obj + m_offsets.pb_name);
        if (nameStr) {
            return Il2CppStringToUtf8Impl(nameStr,
                m_fn_string_chars, m_fn_string_length);
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

bool ESPSystem::WorldToScreen(float worldX, float worldY, float& screenX, float& screenY, const CameraInfo& cam) {
    if (cam.screen_w <= 0 || cam.screen_h <= 0 || cam.zoom <= 0) {
        return false;
    }

    // 计算缩放比例: 屏幕高度 / (2 * 正交大小)
    float scale = cam.screen_h / (2.0f * cam.zoom);

    // 世界坐标 → 屏幕坐标
    screenX = (worldX - cam.cam_x) * scale + cam.screen_w / 2.0f;
    // Y 轴翻转 (世界坐标 Y 向上, 屏幕坐标 Y 向下)
    screenY = (cam.cam_y - worldY) * scale + cam.screen_h / 2.0f;

    // 检查是否在屏幕范围内 (允许一定边距, 因为对象可能有半径)
    float margin = 100.0f;
    if (screenX < -margin || screenX > cam.screen_w + margin ||
        screenY < -margin || screenY > cam.screen_h + margin) {
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

    // 获取对象列表 (线程安全拷贝)
    std::vector<GameObjectInfo> objects;
    {
        std::lock_guard<std::mutex> lock(m_objectMutex);
        objects = m_objects;
    }

    // 构造相机信息 (动态计算 zoom, 修复圆圈太小问题)
    CameraInfo cam = m_camera;
    {
        ImGuiIO& io = ImGui::GetIO();
        cam.screen_w = io.DisplaySize.x;
        cam.screen_h = io.DisplaySize.y;

        // 如果 zoom 无效 (< 10, 之前错误默认值 ~500 导致圆圈极小),
        // 根据自身球体半径动态计算: agar.io 类游戏 orthographicSize ≈ 6 × playerRadius
        // 这样自身球占屏幕高度约 1/12, 圆圈大小合适
        if (cam.zoom < 10.0f) {
            float selfRadius = 0;
            for (const auto& obj : objects) {
                if (obj.is_self && obj.radius > 0) {
                    selfRadius = obj.radius;
                    break;
                }
            }
            if (selfRadius > 0) {
                cam.zoom = 6.0f * selfRadius;
            } else if (!objects.empty() && objects[0].radius > 0) {
                cam.zoom = 6.0f * objects[0].radius;
            } else {
                cam.zoom = 30.0f;  // 最后兜底
            }
        }

        // 应用用户微调 (zoom_scale>1 放大圆圈 = 减小 zoom)
        if (config.zoom_scale > 0.01f) {
            cam.zoom = cam.zoom / config.zoom_scale;
        }

        // 用自己对象坐标作为相机中心 (若没有自身, 用首对象)
        bool found = false;
        for (const auto& obj : objects) {
            if (obj.is_self) {
                cam.cam_x = obj.world_x;
                cam.cam_y = obj.world_y;
                found = true;
                break;
            }
        }
        if (!found && !objects.empty()) {
            cam.cam_x = objects[0].world_x;
            cam.cam_y = objects[0].world_y;
        }
        cam.valid = true;
    }

    // 每 120 帧打印渲染状态
    static uint32_t s_renderLogFrame = 0;
    if (s_renderLogFrame % 120 == 0) {
        APP_LOGI("ESP", "Render: 对象数=%zu screen=%.0fx%.0f zoom=%.1f scale=%.2f cam=(%.1f,%.1f) draw=%d circle=%d",
            objects.size(), cam.screen_w, cam.screen_h, cam.zoom, config.zoom_scale,
            cam.cam_x, cam.cam_y, (int)config.draw_enabled, (int)config.show_circle);
        if (!objects.empty()) {
            APP_LOGI("ESP", "  首对象: x=%.1f y=%.1f r=%.2f id=%d self=%d",
                objects[0].world_x, objects[0].world_y, objects[0].radius,
                objects[0].object_id, (int)objects[0].is_self);
        }
    }
    s_renderLogFrame++;

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

    // 世界坐标 → 屏幕坐标 (使用 Render 构造的 cam)
    float screenX, screenY;
    if (!WorldToScreen(obj.world_x, obj.world_y, screenX, screenY, cam)) {
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
        // 计算距离 (使用世界坐标, 相对当前 cam 中心)
        float dx = obj.world_x - cam.cam_x;
        float dy = obj.world_y - cam.cam_y;
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
