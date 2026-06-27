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
// IL2CPP 托管字符串 → UTF8 转换辅助
// il2cpp_string_to_utf8 在部分 IL2CPP 版本不导出, 用 string_chars+string_length 兜底
// 手动 UTF16 → UTF8 (BMP 基本多文种平面, 不处理 surrogate pair — 玩家名足够)
// =============================================================================
static std::string Il2CppStringToUtf8(const Il2CppString* str,
    il2cpp_string_to_utf8_t utf8Fn,
    il2cpp_string_chars_t charsFn,
    il2cpp_string_length_t lenFn) {
    if (!str) return "";
    // 优先用原生 API (如果导出)
    if (utf8Fn) {
        // IL2CPP API 签名是非 const Il2CppString*, 这里 const_cast 兼容
        const char* utf8 = utf8Fn(const_cast<Il2CppString*>(str));
        if (utf8) return std::string(utf8);
    }
    // 兜底: 用 chars + length 手动 UTF16→UTF8
    if (!charsFn || !lenFn) return "";
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
      m_fn_field_get_offset(nullptr), m_fn_class_get_method_from_name(nullptr),
      m_fn_runtime_invoke(nullptr), m_fn_array_length(nullptr),
      m_fn_array_get(nullptr), m_fn_object_get_class(nullptr),
      m_fn_string_to_utf8(nullptr), m_fn_string_chars(nullptr),
      m_fn_string_length(nullptr), m_fn_string_new(nullptr),
      m_fn_class_get_name(nullptr), m_fn_image_get_name(nullptr),
      m_fn_thread_attach(nullptr), m_fn_domain_assembly_open(nullptr),
      m_fn_image_get_class_count(nullptr), m_fn_image_get_class(nullptr),
      m_classGameCoreCenter(nullptr), m_classPlayerBase(nullptr),
      m_classBall(nullptr), m_classCamera(nullptr),
      m_imageCSharp(nullptr), m_imageUnityEngine(nullptr), m_domain(nullptr),
      m_methodGetInstance(nullptr), m_methodGetCurrent(nullptr),
      m_methodGetName(nullptr),
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
    ESP_RESOLVE(class_get_method_from_name, il2cpp_class_get_method_from_name_t);
    ESP_RESOLVE(runtime_invoke,        il2cpp_runtime_invoke_t);
    ESP_RESOLVE(array_length,          il2cpp_array_length_t);
    // il2cpp_array_get 在 IL2CPP 中通常不导出 (只有 il2cpp_array_length / il2cpp_array_unbox)
    // 改为可选解析: 找不到不报错, 用直接内存偏移读取数组元素 (数据起始 = 4*sizeof(void*))
    if (m_il2cppBase != 0) {
        m_fn_array_get = (il2cpp_array_get_t)ELFResolver::LookupSymbol(m_il2cppBase, "il2cpp_array_get");
    }
    if (!m_fn_array_get && m_il2cppHandle) {
        m_fn_array_get = (il2cpp_array_get_t)dlsym(m_il2cppHandle, "il2cpp_array_get");
    }
    if (m_fn_array_get) {
        APP_LOGI("ESP", "  ✓ [可选] il2cpp_array_get 已解析 @ %p (将用 API)", (void*)m_fn_array_get);
    } else {
        APP_LOGW("ESP", "  ! [可选] il2cpp_array_get 未导出 — 将用直接内存偏移读取数组 (数据起始=4*sizeof(void*))");
    }
    ESP_RESOLVE(object_get_class,      il2cpp_object_get_class_t);
    // il2cpp_string_to_utf8 在部分 IL2CPP 版本不导出, 改为可选
    // 用 il2cpp_string_chars + il2cpp_string_length 手动 UTF16→UTF8 兜底
    ESP_RESOLVE(string_chars,          il2cpp_string_chars_t);
    ESP_RESOLVE(string_length,         il2cpp_string_length_t);
    if (m_il2cppBase != 0) {
        m_fn_string_to_utf8 = (il2cpp_string_to_utf8_t)ELFResolver::LookupSymbol(m_il2cppBase, "il2cpp_string_to_utf8");
    }
    if (!m_fn_string_to_utf8 && m_il2cppHandle) {
        m_fn_string_to_utf8 = (il2cpp_string_to_utf8_t)dlsym(m_il2cppHandle, "il2cpp_string_to_utf8");
    }
    if (m_fn_string_to_utf8) {
        APP_LOGI("ESP", "  ✓ [可选] il2cpp_string_to_utf8 已解析 @ %p", (void*)m_fn_string_to_utf8);
    } else {
        APP_LOGW("ESP", "  ! [可选] il2cpp_string_to_utf8 未导出 — 将用 string_chars+string_length 手动转 UTF8");
    }
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

    // 关键: 在调用任何 IL2CPP 反射 API (class_from_name/runtime_invoke 等) 之前,
    // 必须先把当前线程 attach 到 IL2CPP 运行时, 否则 class_from_name 会返回 null。
    // 对应 game_direct_call.rs:440 api.attach_thread(domain) — 在 find_class 之前执行。
    if (!m_threadAttached.load() && m_fn_thread_attach) {
        void* thread = m_fn_thread_attach(m_domain);
        if (thread) {
            m_threadAttached.store(true);
            ESP_LOGI("ESP: + 当前线程已 attach 到 IL2CPP (thread=%p) — 在 FindGameClasses 之前", thread);
            APP_LOGI("ESP", "✓ 当前线程已 attach 到 IL2CPP (thread=%p) — 在查找类之前", thread);
        } else {
            m_diagStatus = "失败: il2cpp_thread_attach 返回 null";
            ESP_LOGE("ESP: %s", m_diagStatus.c_str());
            APP_LOGE("ESP", "il2cpp_thread_attach 失败 — 无法 attach 到 IL2CPP, class_from_name 将失败");
            return false;
        }
    }

    // 方式 1 (优先): 用 domain_assembly_open 直接打开 Assembly-CSharp
    // 对应 game_direct_call.rs 的 open_assembly_image(domain, "Assembly-CSharp")
    // 但注意: 某些游戏把代码拆到别的程序集, Assembly-CSharp 可能只含框架类
    // 所以方式1拿到 image 后必须先验证里面有没有 GameCoreCenter, 没有就走方式2
    if (m_fn_domain_assembly_open) {
        APP_LOGI("ESP", "尝试 domain_assembly_open(\"Assembly-CSharp\")...");
        const Il2CppAssembly* assembly = m_fn_domain_assembly_open(m_domain, "Assembly-CSharp");
        if (assembly) {
            Il2CppImage* img = m_fn_assembly_get_image(assembly);
            APP_LOGI("ESP", "domain_assembly_open 成功, image=%p", (void*)img);
            // 关键: 验证这个 image 里是否真的有 GameCoreCenter
            // 日志显示 Assembly-CSharp 可能只含 14 个框架类, 游戏代码在别的程序集
            if (img && m_fn_class_from_name) {
                Il2CppClass* testClass = m_fn_class_from_name(img, "", "GameCoreCenter");
                if (testClass) {
                    m_imageCSharp = img;
                    m_classGameCoreCenter = testClass;
                    APP_LOGI("ESP", "✓ Assembly-CSharp 含 GameCoreCenter (class=%p)", (void*)testClass);
                } else {
                    size_t cc = m_fn_image_get_class_count ? m_fn_image_get_class_count(img) : 0;
                    APP_LOGW("ESP", "Assembly-CSharp 里没有 GameCoreCenter (只有 %zu 个类), 改用遍历方式找游戏程序集", cc);
                    // 不设置 m_imageCSharp, 让方式2遍历所有程序集
                }
            }
        } else {
            ESP_LOGW("ESP: domain_assembly_open(\"Assembly-CSharp\") 返回 null, 尝试遍历方式...");
            APP_LOGW("ESP", "domain_assembly_open 返回 null, 改用遍历方式");
        }
    } else {
        APP_LOGW("ESP", "domain_assembly_open 函数指针为空, 跳过此方式");
    }

    // 方式 2 (兜底/主): 遍历所有程序集查找包含 GameCoreCenter 的 image
    // 当方式1失败或 Assembly-CSharp 不含游戏类时执行
    if (!m_imageCSharp) {
        size_t count = 0;
        const Il2CppAssembly** assemblies = m_fn_domain_get_assemblies(m_domain, &count);
        if (!assemblies || count == 0) {
            m_diagStatus = "失败: 无可用程序集";
            ESP_LOGE("ESP: %s", m_diagStatus.c_str());
            APP_LOGE("ESP", "domain_get_assemblies 返回空 (count=%zu, ptr=%p)", count, (void*)assemblies);
            return false;
        }
        ESP_LOGI("ESP: 共 %zu 个程序集, 遍历查找 GameCoreCenter...", count);
        APP_LOGI("ESP", "共 %zu 个程序集, 遍历查找含 GameCoreCenter 的 image", count);

        // 首次遍历打印所有非系统程序集名 + 类数量 (诊断用, 仅一次)
        static bool s_dumpedAsms = false;

        for (size_t i = 0; i < count; i++) {
            if (!assemblies[i]) continue;
            Il2CppImage* img = m_fn_assembly_get_image(assemblies[i]);
            if (!img) continue;

            // 打印每个 image 的名字 + 类数量帮助诊断
            const char* imgName = m_fn_image_get_name ? m_fn_image_get_name(img) : nullptr;
            size_t classCount = m_fn_image_get_class_count ? m_fn_image_get_class_count(img) : 0;
            if (imgName && !s_dumpedAsms) {
                // 打印所有程序集 (含系统, 让用户看到全貌)
                APP_LOGI("ESP", "  程序集 #%zu: %s (类数=%zu)", i, imgName, classCount);
            }

            // 在这个 image 里查找 GameCoreCenter
            Il2CppClass* testClass = m_fn_class_from_name(img, "", "GameCoreCenter");
            if (testClass) {
                m_imageCSharp = img;
                m_classGameCoreCenter = testClass;
                ESP_LOGI("ESP: + 在程序集 #%zu (%s) 中找到 GameCoreCenter", i, imgName ? imgName : "?");
                APP_LOGI("ESP", "✓ 在程序集 #%zu (%s) 中找到 GameCoreCenter, image=%p",
                    i, imgName ? imgName : "?", (void*)img);
                break;
            }
        }
        if (!s_dumpedAsms) {
            APP_LOGI("ESP", "=== 程序集列表转储完成 (%zu 个, 后续重试不再转储) ===", count);
            s_dumpedAsms = true;
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
    // 如果方式1/方式2已经找到, 直接用; 否则重新查找
    if (!m_classGameCoreCenter) {
        m_classGameCoreCenter = m_fn_class_from_name(m_imageCSharp, "", "GameCoreCenter");
    }
    if (m_classGameCoreCenter) {
        ESP_LOGI("ESP: + 找到 GameCoreCenter (namespace=\"\")");
        APP_LOGI("ESP", "✓ 找到类 GameCoreCenter (namespace=\"\", class=%p)", (void*)m_classGameCoreCenter);
    } else {
        // 全局命名空间找不到, 遍历 image 所有类帮助诊断
        ESP_LOGW("ESP: GameCoreCenter (namespace=\"\") 未找到, 遍历 image 类列表...");
        APP_LOGW("ESP", "GameCoreCenter 在全局命名空间未找到, 遍历 Assembly-CSharp 所有类");

        // 尝试用 image_get_class 遍历所有类, 打印类名 + 尝试模糊匹配
        // 注意: 只在首次失败时转储完整列表, 避免重试时日志刷屏
        static bool s_dumpedClasses = false;
        if (m_fn_image_get_class_count && m_fn_image_get_class && m_fn_class_get_name) {
            size_t classCount = m_fn_image_get_class_count(m_imageCSharp);

            // 模糊匹配候选 (按优先级)
            const char* matchKeywords[] = {"GameCoreCenter", "GameCore", "CoreCenter",
                                            "GameManager", "GameCenter", "GameMain",
                                            "Center", "Manager"};
            const int nKeywords = sizeof(matchKeywords) / sizeof(matchKeywords[0]);

            for (size_t i = 0; i < classCount; i++) {
                Il2CppClass* cls = m_fn_image_get_class(m_imageCSharp, i);
                if (!cls) continue;
                const char* clsName = m_fn_class_get_name(cls);
                if (!clsName) continue;

                // 首次失败时打印每个类名 (帮助诊断), 后续重试只做匹配不打印
                if (!s_dumpedClasses) {
                    APP_LOGI("ESP", "  类 #%zu: %s", i, clsName);
                }

                // 尝试精确匹配 "GameCoreCenter" (无论是否转储都要尝试)
                if (!m_classGameCoreCenter) {
                    for (int k = 0; k < nKeywords; k++) {
                        if (strstr(clsName, matchKeywords[k]) != nullptr) {
                            APP_LOGW("ESP", "  ⚠ 类 #%zu 名字含 '%s': %s (候选?)",
                                i, matchKeywords[k], clsName);
                            if (strcmp(clsName, "GameCoreCenter") == 0) {
                                m_classGameCoreCenter = cls;
                                ESP_LOGI("ESP: + 精确匹配到 GameCoreCenter (类 #%zu)", i);
                                break;
                            }
                        }
                    }
                }
            }
            if (!s_dumpedClasses) {
                APP_LOGI("ESP", "=== 类转储完成 (%zu 个, 后续重试不再转储) ===", classCount);
                s_dumpedClasses = true;
            }
        } else {
            APP_LOGE("ESP", "image_get_class/image_get_class_count/class_get_name 之一为空, 无法遍历");
        }

        if (!m_classGameCoreCenter) {
            m_diagStatus = "失败: GameCoreCenter 类未找到 (见日志类列表)";
            ESP_LOGE("ESP: %s", m_diagStatus.c_str());
            APP_LOGE("ESP", "GameCoreCenter 类查找失败 — 请检查日志中的类名列表, 类名可能不同");
            APP_LOGE("ESP", "  可能: 1)类被混淆 2)类名不同 3)在命名空间下 4)游戏未完全加载");
            return false;
        }
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

    // --- 查找 Ball 类 (BallDic 里的对象是 Ball, 不是 PlayerBase!) ---
    // 日志确认 GameCoreCenter.BallDic @ 0x68 存在, 里面的对象需要单独找类
    m_classBall = m_fn_class_from_name(m_imageCSharp, "", "Ball");
    if (!m_classBall) {
        // 兜底: 尝试其他常见球体类名
        const char* ballNames[] = {"BallBase", "BallEntity", "GameBall", "Cell", "Fish",
                                    "BallObject", "BaseBall", "Qiu"};
        for (const char* name : ballNames) {
            m_classBall = m_fn_class_from_name(m_imageCSharp, "", name);
            if (m_classBall) {
                APP_LOGI("ESP", "✓ 用兜底名 %s 作为 Ball 类", name);
                break;
            }
        }
    }
    if (m_classBall) {
        ESP_LOGI("ESP: + 找到 Ball 类");
        APP_LOGI("ESP", "✓ 找到类 Ball (class=%p) — BallDic 里的对象用这个类的偏移",
            (void*)m_classBall);
    } else {
        ESP_LOGW("ESP: 未找到 Ball 类, BallDic 对象将用 PlayerBase 偏移读取 (可能不准)");
        APP_LOGW("ESP", "未找到 Ball 类, BallDic 对象将用 PlayerBase 偏移读取 (可能不准)");
    }

    // --- 查找 Camera (命名空间 "UnityEngine") ---
    // 关键: Camera 在 UnityEngine.CoreModule.dll, 不在游戏程序集!
    // 必须遍历所有程序集找 UnityEngine.Camera, 而不是只在 m_imageCSharp 里找
    m_classCamera = m_fn_class_from_name(m_imageCSharp, "UnityEngine", "Camera");
    if (!m_classCamera) {
        // 游戏程序集没有 Camera, 遍历所有程序集找 UnityEngine.CoreModule
        ESP_LOGW("ESP: 游戏程序集无 Camera, 遍历所有程序集找 UnityEngine.Camera...");
        APP_LOGW("ESP", "游戏程序集无 UnityEngine.Camera, 遍历所有程序集查找");

        size_t count = 0;
        const Il2CppAssembly** assemblies = m_fn_domain_get_assemblies(m_domain, &count);
        for (size_t i = 0; i < count; i++) {
            if (!assemblies[i]) continue;
            Il2CppImage* img = m_fn_assembly_get_image(assemblies[i]);
            if (!img) continue;
            // 优先找 UnityEngine 命名空间的 Camera
            Il2CppClass* camClass = m_fn_class_from_name(img, "UnityEngine", "Camera");
            if (camClass) {
                m_classCamera = camClass;
                m_imageUnityEngine = img;
                const char* imgName = m_fn_image_get_name ? m_fn_image_get_name(img) : "?";
                ESP_LOGI("ESP: + 在程序集 %s 中找到 UnityEngine.Camera", imgName);
                APP_LOGI("ESP", "✓ 在程序集 %s 中找到 UnityEngine.Camera (class=%p)",
                    imgName, (void*)camClass);
                break;
            }
        }
        // 仍找不到, 尝试全局命名空间
        if (!m_classCamera) {
            for (size_t i = 0; i < count; i++) {
                if (!assemblies[i]) continue;
                Il2CppImage* img = m_fn_assembly_get_image(assemblies[i]);
                if (!img) continue;
                Il2CppClass* camClass = m_fn_class_from_name(img, "", "Camera");
                if (camClass) {
                    m_classCamera = camClass;
                    m_imageUnityEngine = img;
                    const char* imgName = m_fn_image_get_name ? m_fn_image_get_name(img) : "?";
                    APP_LOGI("ESP", "✓ 在程序集 %s 中找到 Camera (全局命名空间)", imgName);
                    break;
                }
            }
        }
    } else {
        APP_LOGI("ESP", "✓ 找到类 UnityEngine.Camera (class=%p)", (void*)m_classCamera);
    }
    if (!m_classCamera) {
        ESP_LOGW("ESP: Camera 未找到, 世界坐标转换将用默认 zoom 兜底");
        APP_LOGW("ESP", "Camera 未找到 (所有程序集都没有), W2S 将用默认 zoom=30 兜底");
    }

    // --- 查找方法 (逆向: "get_instance", "get_current", "get_name") ---
    if (m_classGameCoreCenter) {
        m_methodGetInstance = m_fn_class_get_method_from_name(m_classGameCoreCenter, "get_instance", 0);
        ESP_LOGI("ESP: GameCoreCenter.get_instance = %p", (void*)m_methodGetInstance);
        APP_LOGI("ESP", "GameCoreCenter.get_instance = %p", (void*)m_methodGetInstance);
    }
    if (m_classCamera) {
        m_methodGetCurrent = m_fn_class_get_method_from_name(m_classCamera, "get_current", 0);
        ESP_LOGI("ESP: Camera.get_current = %p", (void*)m_methodGetCurrent);
        APP_LOGI("ESP", "Camera.get_current = %p", (void*)m_methodGetCurrent);
    }
    if (m_classPlayerBase) {
        m_methodGetName = m_fn_class_get_method_from_name(m_classPlayerBase, "get_name", 0);
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

    // --- 遍历 GameCoreCenter 字段 (打印全部) ---
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

            // 匹配真实字段名 (从日志确认: PlayerDic/BallDic/SelfPlayerID)
            // 旧模板字段名 (players/fishList/cells) 作为兜底兼容
            if (strcmp(fname, "PlayerDic") == 0) {
                m_offsets.gcc_player_dic = offset;
            } else if (strcmp(fname, "BallDic") == 0) {
                m_offsets.gcc_ball_dic = offset;
            } else if (strcmp(fname, "SelfPlayerID") == 0) {
                m_offsets.gcc_self_player_id = offset;
            }
            // 兼容旧模板字段名 (其他版本游戏可能用 List)
            else if (strcmp(fname, "players") == 0 || strcmp(fname, "playerList") == 0 ||
                strcmp(fname, "player_list") == 0 || strcmp(fname, "allPlayers") == 0) {
                m_offsets.gcc_player_list = offset;
            } else if (strcmp(fname, "fishList") == 0 || strcmp(fname, "fish_list") == 0 ||
                       strcmp(fname, "fishes") == 0) {
                m_offsets.gcc_fish_list = offset;
            } else if (strcmp(fname, "cells") == 0 || strcmp(fname, "cellList") == 0 ||
                       strcmp(fname, "cell_list") == 0) {
                m_offsets.gcc_cell_list = offset;
            } else if (strcmp(fname, "selfPlayer") == 0 || strcmp(fname, "self_player") == 0 ||
                       strcmp(fname, "localPlayer") == 0 || strcmp(fname, "myPlayer") == 0) {
                m_offsets.gcc_self_player = offset;
            }
        }
    }

    // --- 遍历 PlayerBase 字段 (打印全部) ---
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

            // 匹配真实字段名 (从日志确认: Name/ID/Color/isAlive/_pos/radius)
            // _pos 是 Vector3 (12 字节: x,y,z), 拆成三个 float 偏移
            if (strcmp(fname, "Name") == 0 || strcmp(fname, "name") == 0 ||
                strcmp(fname, "playerName") == 0 || strcmp(fname, "nickName") == 0 ||
                strcmp(fname, "userName") == 0) {
                m_offsets.pb_name = offset;
            } else if (strcmp(fname, "_pos") == 0) {
                // _pos 是 Vector3, x@offset, y@offset+4, z@offset+8
                m_offsets.pb_x = offset;
                m_offsets.pb_y = offset + 4;
                m_offsets.pb_z = offset + 8;
            } else if (strcmp(fname, "pos") == 0) {
                // 备用 pos 字段 (如果 _pos 没匹配到, 用 pos)
                if (m_offsets.pb_x == 0) {
                    m_offsets.pb_x = offset;
                    m_offsets.pb_y = offset + 4;
                    m_offsets.pb_z = offset + 8;
                }
            } else if (strcmp(fname, "radius") == 0 || strcmp(fname, "Radius") == 0 ||
                       strcmp(fname, "size") == 0 || strcmp(fname, "Size") == 0 ||
                       strcmp(fname, "mass") == 0) {
                m_offsets.pb_radius = offset;
            } else if (strcmp(fname, "ID") == 0 || strcmp(fname, "id") == 0 ||
                       strcmp(fname, "playerId") == 0 || strcmp(fname, "objectId") == 0) {
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
            } else if (strcmp(fname, "Color") == 0 || strcmp(fname, "color") == 0 ||
                       strcmp(fname, "playerColor") == 0) {
                m_offsets.pb_color = offset;
            }
            // Transform 引用字段 (Unity 位置在 Transform 上, 必须通过它读坐标!)
            // PlayerBase 日志确认有 SelfTF @ 0xb8, 但之前没解析
            else if (strcmp(fname, "SelfTF") == 0 || strcmp(fname, "selfTF") == 0 ||
                     strcmp(fname, "tf") == 0 || strcmp(fname, "TF") == 0 ||
                     strcmp(fname, "transform") == 0 || strcmp(fname, "Transform") == 0 ||
                     strcmp(fname, "selfTf") == 0 || strcmp(fname, "_transform") == 0 ||
                     strcmp(fname, "m_Transform") == 0 || strcmp(fname, "SelfTf") == 0 ||
                     strcmp(fname, "ballTf") == 0 || strcmp(fname, "BallTF") == 0) {
                m_offsets.pb_transform = offset;
            }
        }
    }

    // --- 遍历 Ball 类字段 (BallDic 里的对象是 Ball, 字段布局和 PlayerBase 不同) ---
    // 日志证据: BallDic 首个对象 name=空 x=0 y=0 r=1.0, 说明 Ball 的坐标字段偏移和 PlayerBase 不同
    // 必须单独解析 Ball 类的字段偏移
    if (m_classBall) {
        ESP_LOGI("ESP: === Ball 全部字段 ===");
        APP_LOGI("ESP", "--- Ball 字段 ---");
        void* iter = nullptr;
        FieldInfo* field;
        while ((field = m_fn_class_get_fields(m_classBall, &iter)) != nullptr) {
            const char* fname = m_fn_field_get_name(field);
            size_t offset = m_fn_field_get_offset(field);
            if (!fname) continue;

            ESP_LOGI("ESP:   Ball.%s @ 0x%zx", fname, offset);
            APP_LOGI("ESP", "  Ball.%s @ 0x%zx", fname, offset);

            // 用和 PlayerBase 一样的字段名匹配规则, 但存到 m_ballOffsets
            if (strcmp(fname, "Name") == 0 || strcmp(fname, "name") == 0 ||
                strcmp(fname, "playerName") == 0 || strcmp(fname, "nickName") == 0) {
                m_ballOffsets.pb_name = offset;
            } else if (strcmp(fname, "_pos") == 0 || strcmp(fname, "pos") == 0 ||
                       strcmp(fname, "position") == 0 || strcmp(fname, "Position") == 0 ||
                       strcmp(fname, "curPos") == 0 || strcmp(fname, "worldPos") == 0) {
                // Vector3: x@offset, y@offset+4, z@offset+8
                m_ballOffsets.pb_x = offset;
                m_ballOffsets.pb_y = offset + 4;
                m_ballOffsets.pb_z = offset + 8;
            } else if (strcmp(fname, "x") == 0 || strcmp(fname, "posX") == 0 ||
                       strcmp(fname, "X") == 0) {
                if (m_ballOffsets.pb_x == 0) m_ballOffsets.pb_x = offset;
            } else if (strcmp(fname, "y") == 0 || strcmp(fname, "posY") == 0 ||
                       strcmp(fname, "Y") == 0) {
                if (m_ballOffsets.pb_y == 0) m_ballOffsets.pb_y = offset;
            } else if (strcmp(fname, "radius") == 0 || strcmp(fname, "Radius") == 0 ||
                       strcmp(fname, "size") == 0 || strcmp(fname, "Size") == 0 ||
                       strcmp(fname, "mass") == 0) {
                m_ballOffsets.pb_radius = offset;
            } else if (strcmp(fname, "ID") == 0 || strcmp(fname, "id") == 0 ||
                       strcmp(fname, "ballId") == 0 || strcmp(fname, "Ballid") == 0 ||
                       strcmp(fname, "BallID") == 0) {
                m_ballOffsets.pb_id = offset;
            } else if (strcmp(fname, "rankId") == 0 || strcmp(fname, "rank_id") == 0 ||
                       strcmp(fname, "PlayerID") == 0 || strcmp(fname, "playerId") == 0 ||
                       strcmp(fname, "ownerId") == 0) {
                m_ballOffsets.pb_rank_id = offset;
            } else if (strcmp(fname, "score") == 0 || strcmp(fname, "weight") == 0) {
                m_ballOffsets.pb_score = offset;
            } else if (strcmp(fname, "isAlive") == 0 || strcmp(fname, "is_alive") == 0 ||
                       strcmp(fname, "alive") == 0) {
                m_ballOffsets.pb_is_alive = offset;
            } else if (strcmp(fname, "Color") == 0 || strcmp(fname, "color") == 0) {
                m_ballOffsets.pb_color = offset;
            }
            // Transform 引用字段 (Ball 的位置在 Transform 上!)
            else if (strcmp(fname, "SelfTF") == 0 || strcmp(fname, "selfTF") == 0 ||
                     strcmp(fname, "tf") == 0 || strcmp(fname, "TF") == 0 ||
                     strcmp(fname, "transform") == 0 || strcmp(fname, "Transform") == 0 ||
                     strcmp(fname, "selfTf") == 0 || strcmp(fname, "_transform") == 0 ||
                     strcmp(fname, "m_Transform") == 0 || strcmp(fname, "SelfTf") == 0 ||
                     strcmp(fname, "ballTf") == 0 || strcmp(fname, "BallTF") == 0) {
                m_ballOffsets.pb_transform = offset;
            }
        }
        m_ballOffsets.initialized = true;
        APP_LOGI("ESP", "Ball 偏移: name=0x%zu ID=0x%zu x=0x%zu y=0x%zu radius=0x%zu transform=0x%zu",
            m_ballOffsets.pb_name, m_ballOffsets.pb_id, m_ballOffsets.pb_x, m_ballOffsets.pb_y,
            m_ballOffsets.pb_radius, m_ballOffsets.pb_transform);
    } else {
        // 没有 Ball 类, 复用 PlayerBase 偏移 (兜底)
        m_ballOffsets = m_offsets;
        APP_LOGW("ESP", "未找到 Ball 类, BallDic 对象复用 PlayerBase 偏移");
    }

    m_offsets.initialized = true;
    ESP_LOGI("ESP: 字段偏移初始化完成");
    APP_LOGI("ESP", "=== 字段偏移汇总 ===");
    APP_LOGI("ESP", "GCC: PlayerDic=0x%zu BallDic=0x%zu SelfPlayerID=0x%zu",
         m_offsets.gcc_player_dic, m_offsets.gcc_ball_dic, m_offsets.gcc_self_player_id);
    APP_LOGI("ESP", "GCC(兼容): player_list=0x%zu fish_list=0x%zu cell_list=0x%zu",
         m_offsets.gcc_player_list, m_offsets.gcc_fish_list, m_offsets.gcc_cell_list);
    APP_LOGI("ESP", "PB: name=0x%zu ID=0x%zu x=0x%zu y=0x%zu z=0x%zu radius=0x%zu color=0x%zu isAlive=0x%zu",
         m_offsets.pb_name, m_offsets.pb_id, m_offsets.pb_x, m_offsets.pb_y, m_offsets.pb_z,
         m_offsets.pb_radius, m_offsets.pb_color, m_offsets.pb_is_alive);
    ESP_LOGI("ESP:   GCC: PlayerDic=0x%zu BallDic=0x%zu SelfPlayerID=0x%zu",
         m_offsets.gcc_player_dic, m_offsets.gcc_ball_dic, m_offsets.gcc_self_player_id);
    ESP_LOGI("ESP:   PB: name=0x%zu ID=0x%zu x=0x%zu y=0x%zu radius=0x%zu color=0x%zu isAlive=0x%zu",
         m_offsets.pb_name, m_offsets.pb_id, m_offsets.pb_x, m_offsets.pb_y,
         m_offsets.pb_radius, m_offsets.pb_color, m_offsets.pb_is_alive);

    // 诊断: 如果关键字典字段没找到, 提示
    if (m_offsets.gcc_player_dic == 0 && m_offsets.gcc_ball_dic == 0 &&
        m_offsets.gcc_player_list == 0 && m_offsets.gcc_fish_list == 0) {
        m_diagStatus = "警告: 未匹配到 PlayerDic/BallDic 任何对象字典字段, 请查看日志字段列表";
        ESP_LOGW("ESP: %s", m_diagStatus.c_str());
        APP_LOGE("ESP", "✗ 未匹配到任何对象字典/列表字段 — ReadGameObjects 将无法读取对象!");
    } else {
        APP_LOGI("ESP", "✓ 已匹配到对象字典字段, 可读取对象");
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

    Il2CppClass* camClass = m_fn_object_get_class(camObj);
    if (!camClass) return;

    // Unity Camera 的 orthographicSize / transform.position 都是属性,
    // class_get_fields 拿不到 (它们是 get_ 方法), 必须用 runtime_invoke 调用 getter
    //
    // 关键方法:
    //   Camera.get_orthographicSize() → float  (正交相机半高, 2D 游戏用)
    //   Camera.get_transform()        → Transform
    //   Transform.get_position()      → Vector3 (相机世界坐标)

    // 读 orthographicSize
    if (m_camera.zoom <= 0) {
        MethodInfo* mGetOrtho = m_fn_class_get_method_from_name(camClass, "get_orthographicSize", 0);
        if (mGetOrtho) {
            Il2CppException* e2 = nullptr;
            Il2CppObject* ret = m_fn_runtime_invoke(mGetOrtho, camObj, nullptr, &e2);
            if (ret && !e2) {
                // 返回值是 float, 装箱为 object (Il2CppObject 头 16 字节 + float 4 字节)
                float val = *(float*)((char*)ret + 16);
                if (val > 0) {
                    m_camera.zoom = val;
                    static bool s_loggedOrtho = false;
                    if (!s_loggedOrtho) {
                        APP_LOGI("ESP", "✓ Camera.orthographicSize = %.2f", val);
                        s_loggedOrtho = true;
                    }
                }
            }
        }
    }

    // 读 transform.position (相机世界坐标)
    MethodInfo* mGetTransform = m_fn_class_get_method_from_name(camClass, "get_transform", 0);
    if (mGetTransform) {
        Il2CppException* e3 = nullptr;
        Il2CppObject* transObj = m_fn_runtime_invoke(mGetTransform, camObj, nullptr, &e3);
        if (transObj && !e3) {
            Il2CppClass* transClass = m_fn_object_get_class(transObj);
            if (transClass) {
                MethodInfo* mGetPos = m_fn_class_get_method_from_name(transClass, "get_position", 0);
                if (mGetPos) {
                    Il2CppException* e4 = nullptr;
                    Il2CppObject* posObj = m_fn_runtime_invoke(mGetPos, transObj, nullptr, &e4);
                    if (posObj && !e4) {
                        // Vector3 是值类型, 装箱后布局: [Il2CppObject 头 16 字节][x 4][y 4][z 4]
                        float px = *(float*)((char*)posObj + 16);
                        float py = *(float*)((char*)posObj + 20);
                        m_camera.cam_x = px;
                        m_camera.cam_y = py;
                        static bool s_loggedPos = false;
                        if (!s_loggedPos) {
                            APP_LOGI("ESP", "✓ Camera.transform.position = (%.2f, %.2f)", px, py);
                            s_loggedPos = true;
                        }
                    }
                }
            }
        }
    }

    // 设置屏幕尺寸 (从 ImGui IO 获取)
    ImGuiIO& io = ImGui::GetIO();
    m_camera.screen_w = io.DisplaySize.x;
    m_camera.screen_h = io.DisplaySize.y;

    // 如果没读到 zoom, 用经验默认值 (大鱼吃小鱼 orthographicSize 约 20-30)
    if (m_camera.zoom <= 0) {
        m_camera.zoom = 30.0f;
    }

    m_camera.valid = true;
}

// =============================================================================
// 通过 Transform.get_position 读取对象世界坐标
// 关键修复: Unity 游戏对象的位置在 Transform 组件上, 不是直接字段!
// 之前直接读 _pos 字段读到 0, 因为 _pos 可能不是实际生效的位置字段
//
// 调用链: obj.transform → Transform 对象 → Transform.get_position() → Vector3
// Vector3 是值类型, runtime_invoke 返回装箱对象: [Il2CppObject 头 16字节][x 4][y 4][z 4]
// =============================================================================
bool ESPSystem::ReadObjectPositionViaTransform(Il2CppObject* obj, size_t transformOffset,
                                                float& outX, float& outY, float& outZ) {
    outX = outY = outZ = 0;
    if (!obj || transformOffset == 0 || !m_fn_class_get_method_from_name ||
        !m_fn_runtime_invoke || !m_fn_object_get_class) {
        return false;
    }

    // 1. 读 Transform 引用字段
    Il2CppObject* tfObj = *(Il2CppObject**)((char*)obj + transformOffset);
    if (!tfObj) return false;

    // 2. 拿 Transform 类, 找 get_position 方法 (0 参数, 返回 Vector3)
    Il2CppClass* tfClass = m_fn_object_get_class(tfObj);
    if (!tfClass) return false;
    MethodInfo* mGetPos = m_fn_class_get_method_from_name(tfClass, "get_position", 0);
    if (!mGetPos) return false;

    // 3. 调用 get_position
    Il2CppException* exc = nullptr;
    Il2CppObject* posObj = m_fn_runtime_invoke(mGetPos, tfObj, nullptr, &exc);
    if (!posObj || exc) return false;

    // 4. 装箱 Vector3: [Il2CppObject 头 16字节][x f32][y f32][z f32]
    outX = *(float*)((char*)posObj + 16);
    outY = *(float*)((char*)posObj + 20);
    outZ = *(float*)((char*)posObj + 24);
    return true;
}

// =============================================================================
// 读取游戏对象 — 遍历 GameCoreCenter 的对象字典 (PlayerDic / BallDic)
// 对应 data_reader.rs 的 read_objects()
//
// 关键: 游戏用 Dictionary<int, T> 而非 List<T>!
//   GCC.PlayerDic @ 0x58 — Dictionary<int, PlayerBase>
//   GCC.BallDic   @ 0x68 — Dictionary<int, Ball>
//   GCC.SelfPlayerID @ 0x160 — int (自身玩家 ID)
//
// IL2CPP Dictionary 内存布局 (64位):
//   +0x10: _buckets   (int[])
//   +0x18: _entries   (Entry[])  ← 遍历这个
//   +0x20: _count     (int, 含 free entry)
//   +0x28: _freeCount (int)
// Entry<int, TRef> 布局 (24 字节):
//   +0x00: hashCode (int, free entry 为 -1)
//   +0x04: next     (int)
//   +0x08: key      (int)
//   +0x10: value    (引用, 8 字节)
// =============================================================================

// 读取 Dictionary 的所有有效 value
// outValues 返回所有 hashCode >= 0 且 value 非空的对象指针
void ESPSystem::ReadDictionaryValues(void* dictObj, std::vector<Il2CppObject*>& outValues) {
    outValues.clear();
    if (!dictObj) return;

    // Dictionary._entries @ 0x18 (Entry[] 引用)
    Il2CppArray* entries = *(Il2CppArray**)((char*)dictObj + 0x18);
    // Dictionary._count @ 0x20 (int, 已分配 entry 数, 含 free)
    int count = *(int*)((char*)dictObj + 0x20);

    if (!entries || count <= 0) return;

    // 安全上限: 避免读到垃圾值导致巨量循环 (一个游戏对局玩家/球数不会超过 10000)
    if (count > 10000) count = 10000;

    // IL2CPP 数组数据起始 = 4 * sizeof(void*) = 32 字节 (64位)
    // { vtable(8), monitor(8), bounds*(8), max_length(8), data[] }
    char* dataStart = (char*)entries + 4 * sizeof(void*);

    // Entry<int, TRef> 大小 = 24 字节 (4+4+4[+4pad]+8)
    // value 偏移 = 0x10
    const size_t ENTRY_SIZE = 24;
    const size_t HASH_OFFSET = 0x0;
    const size_t VALUE_OFFSET = 0x10;

    for (int i = 0; i < count; i++) {
        char* entry = dataStart + i * ENTRY_SIZE;
        int hashCode = *(int*)(entry + HASH_OFFSET);
        if (hashCode < 0) continue;  // free entry (hashCode = -1)

        Il2CppObject* value = *(Il2CppObject**)(entry + VALUE_OFFSET);
        if (value) outValues.push_back(value);
    }
}

void ESPSystem::ReadGameObjects() {
    if (!m_offsets.initialized || !m_gameInstance) return;

    std::vector<GameObjectInfo> newObjects;
    uint32_t c = m_counter.load();

    // --- 读取自身玩家 ID (GCC.SelfPlayerID @ 0x160) ---
    // 用于识别哪个 PlayerBase 是自己
    int selfPlayerId = -1;
    if (m_offsets.gcc_self_player_id) {
        selfPlayerId = *(int*)((char*)m_gameInstance + m_offsets.gcc_self_player_id);
    }
    // 用户在 UI 配置的 self_rank_id 优先 (如果有), 否则用游戏字段读取的
    int effectiveSelfId = (config.self_rank_id >= 0) ? config.self_rank_id : selfPlayerId;

    // --- 读取单个对象的字段 (接受偏移参数, PlayerBase 和 Ball 用各自偏移) ---
    auto readObjectFields = [&](Il2CppObject* obj, const FieldOffsets& off) -> GameObjectInfo {
        GameObjectInfo info;

        // 读取名称 (通过 get_name 方法或 name 字段)
        info.name = ReadObjectName(obj);

        // 读取坐标: 优先用 Transform.get_position (Unity 位置在 Transform 上!)
        // 之前直接读 _pos 字段读到 0, 因为 _pos 不是实际生效的位置字段
        bool gotPos = false;
        if (off.pb_transform) {
            gotPos = ReadObjectPositionViaTransform(obj, off.pb_transform,
                                                     info.world_x, info.world_y, info.world_z);
        }
        // 兜底: 如果没有 transform 字段或读取失败, 用 _pos 字段
        if (!gotPos) {
            if (off.pb_x) info.world_x = *(float*)((char*)obj + off.pb_x);
            if (off.pb_y) info.world_y = *(float*)((char*)obj + off.pb_y);
            if (off.pb_z) info.world_z = *(float*)((char*)obj + off.pb_z);
        }

        // 读取半径
        if (off.pb_radius) info.radius = *(float*)((char*)obj + off.pb_radius);

        // 读取 ID
        if (off.pb_id) info.object_id = *(int*)((char*)obj + off.pb_id);

        // 读取排名 ID (如果有 rankId 字段)
        if (off.pb_rank_id) info.rank_id = *(int*)((char*)obj + off.pb_rank_id);
        else info.rank_id = info.object_id;  // 没有独立的 rankId, 用 ID 代替

        // 读取分数
        if (off.pb_score) info.score = *(int*)((char*)obj + off.pb_score);

        // 读取存活状态
        if (off.pb_is_alive) info.is_alive = *(bool*)((char*)obj + off.pb_is_alive);
        else info.is_alive = true;

        // 识别是否是自己 (用 ID 比对 SelfPlayerID)
        info.is_self = (effectiveSelfId >= 0 && info.object_id == effectiveSelfId);

        // 设置颜色
        info.color = GetObjectColor(info);

        return info;
    };

    // --- 读取对象 (Dictionary 优先, 兼容 Array) ---
    // useBallOffsets=true 时用 m_ballOffsets (Ball 类偏移), 否则用 m_offsets (PlayerBase)
    auto readFromDicOrArray = [&](size_t dicOffset, size_t listOffset,
                                   const char* name, const FieldOffsets& off) {
        if (dicOffset) {
            // 方式 1: 读取 Dictionary (PlayerDic / BallDic)
            void* dictObj = *(void**)((char*)m_gameInstance + dicOffset);
            if (!dictObj) {
                if (c % 120 == 0) APP_LOGD("ESP", "%s 字典为 null (offset=0x%zx)", name, dicOffset);
                return (size_t)0;
            }
            std::vector<Il2CppObject*> values;
            ReadDictionaryValues(dictObj, values);
            size_t n = values.size();
            if (c % 120 == 0) {
                APP_LOGI("ESP", "%s 字典读取: %zu 个对象 (offset=0x%zx)", name, n, dicOffset);
            }
            for (size_t i = 0; i < n; i++) {
                GameObjectInfo info = readObjectFields(values[i], off);
                // 每 120 帧打印首个对象详情 (诊断用)
                if (c % 120 == 0 && i == 0) {
                    APP_LOGI("ESP", "%s 首个对象 [name=%s x=%.1f y=%.1f r=%.1f id=%d alive=%d self=%d]",
                        name, info.name.c_str(), info.world_x, info.world_y, info.radius,
                        info.object_id, (int)info.is_alive, (int)info.is_self);
                }
                newObjects.push_back(std::move(info));
            }
            return n;
        } else if (listOffset) {
            // 方式 2 (兼容): 读取 Array (旧模板字段 players/fishList/cells)
            Il2CppArray* array = *(Il2CppArray**)((char*)m_gameInstance + listOffset);
            if (!array) {
                if (c % 120 == 0) APP_LOGD("ESP", "%s 数组为 null (offset=0x%zx)", name, listOffset);
                return (size_t)0;
            }
            size_t len = m_fn_array_length(array);
            if (c % 120 == 0) APP_LOGI("ESP", "%s 数组长度=%zu (offset=0x%zx)", name, len, listOffset);
            for (size_t i = 0; i < len; i++) {
                Il2CppObject* obj = nullptr;
                if (m_fn_array_get) {
                    obj = m_fn_array_get(array, i);
                } else {
                    char* dataStart = (char*)array + 4 * sizeof(void*);
                    obj = *(Il2CppObject**)(dataStart + i * sizeof(void*));
                }
                if (!obj) continue;
                GameObjectInfo info = readObjectFields(obj, off);
                newObjects.push_back(std::move(info));
            }
            return len;
        }
        return (size_t)0;
    };

    // 读取玩家字典 (PlayerDic) — 用 PlayerBase 偏移
    size_t playerCount = readFromDicOrArray(m_offsets.gcc_player_dic,
                                              m_offsets.gcc_player_list, "PlayerDic", m_offsets);

    // --- BallDic 动态类识别 ---
    // BallDic 里的对象类名可能不叫 "Ball" (FindGameClasses 找不到),
    // 用 object_get_class 从首对象拿真实类, 再解析其字段偏移
    if (m_offsets.gcc_ball_dic && !m_classBall && m_fn_object_get_class && m_fn_class_get_name) {
        void* dictObj = *(void**)((char*)m_gameInstance + m_offsets.gcc_ball_dic);
        if (dictObj) {
            std::vector<Il2CppObject*> tmp;
            ReadDictionaryValues(dictObj, tmp);
            if (!tmp.empty() && tmp[0]) {
                Il2CppClass* bc = m_fn_object_get_class(tmp[0]);
                if (bc) {
                    const char* cn = m_fn_class_get_name(bc);
                    APP_LOGW("ESP", "BallDic 对象真实类=%s (不叫 Ball), 现场解析字段偏移",
                        cn ? cn : "?");
                    m_classBall = bc;
                    // 重新解析 Ball 字段偏移 (用真实类)
                    // 复用 InitFieldOffsets 里的 Ball 字段遍历逻辑
                    if (m_fn_class_get_fields && m_fn_field_get_name && m_fn_field_get_offset) {
                        APP_LOGI("ESP", "--- Ball(%s) 字段 ---", cn ? cn : "?");
                        void* iter = nullptr;
                        FieldInfo* field;
                        while ((field = m_fn_class_get_fields(bc, &iter)) != nullptr) {
                            const char* fname = m_fn_field_get_name(field);
                            size_t offset = m_fn_field_get_offset(field);
                            if (!fname) continue;
                            APP_LOGI("ESP", "  Ball.%s @ 0x%zx", fname, offset);
                            if (strcmp(fname, "Radius") == 0 || strcmp(fname, "radius") == 0 ||
                                strcmp(fname, "Size") == 0 || strcmp(fname, "size") == 0 ||
                                strcmp(fname, "mass") == 0) {
                                m_ballOffsets.pb_radius = offset;
                            } else if (strcmp(fname, "SelfTF") == 0 || strcmp(fname, "selfTF") == 0 ||
                                       strcmp(fname, "tf") == 0 || strcmp(fname, "TF") == 0 ||
                                       strcmp(fname, "transform") == 0 || strcmp(fname, "Transform") == 0 ||
                                       strcmp(fname, "selfTf") == 0 || strcmp(fname, "_transform") == 0 ||
                                       strcmp(fname, "m_Transform") == 0 || strcmp(fname, "SelfTf") == 0 ||
                                       strcmp(fname, "ballTf") == 0 || strcmp(fname, "BallTF") == 0) {
                                m_ballOffsets.pb_transform = offset;
                            } else if (strcmp(fname, "ID") == 0 || strcmp(fname, "id") == 0 ||
                                       strcmp(fname, "ballId") == 0 || strcmp(fname, "BallID") == 0 ||
                                       strcmp(fname, "Ballid") == 0 || strcmp(fname, "Id") == 0) {
                                m_ballOffsets.pb_id = offset;
                            } else if (strcmp(fname, "_pos") == 0 || strcmp(fname, "pos") == 0 ||
                                       strcmp(fname, "position") == 0) {
                                m_ballOffsets.pb_x = offset;
                                m_ballOffsets.pb_y = offset + 4;
                                m_ballOffsets.pb_z = offset + 8;
                            } else if (strcmp(fname, "score") == 0 || strcmp(fname, "Score") == 0 ||
                                       strcmp(fname, "weight") == 0 || strcmp(fname, "curScore") == 0) {
                                m_ballOffsets.pb_score = offset;
                            }
                        }
                        m_ballOffsets.initialized = true;
                        APP_LOGI("ESP", "Ball(%s) 偏移: radius=0x%zu transform=0x%zu id=0x%zu x=0x%zu",
                            cn ? cn : "?", m_ballOffsets.pb_radius, m_ballOffsets.pb_transform,
                            m_ballOffsets.pb_id, m_ballOffsets.pb_x);
                    }
                }
            }
        }
    }

    // 读取球字典 (BallDic) — 用 Ball 偏移 (关键: Ball 字段布局和 PlayerBase 不同!)
    size_t ballCount = readFromDicOrArray(m_offsets.gcc_ball_dic,
                                            m_offsets.gcc_fish_list, "BallDic", m_ballOffsets);
    // 兼容: 读取细胞列表 (用 PlayerBase 偏移)
    readFromDicOrArray(0, m_offsets.gcc_cell_list, "cells", m_offsets);

    // 更新缓存
    {
        std::lock_guard<std::mutex> lock(m_objectMutex);
        m_objects = std::move(newObjects);
    }

    // 更新诊断状态 + 详细日志 (每 60 帧约 1 秒)
    if (c % 60 == 0) {
        char buf[200];
        snprintf(buf, sizeof(buf), "游戏就绪, 对象数=%zu (PlayerDic=%zu, BallDic=%zu, selfID=%d, cam=%d)",
            m_objects.size(), playerCount, ballCount, effectiveSelfId,
            m_camera.valid ? 1 : 0);
        m_diagStatus = buf;
        APP_LOGI("ESP", "Tick: %s", buf);
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
            // 优先用 il2cpp_string_to_utf8, 不存在则用 chars+length 手动转
            return Il2CppStringToUtf8((Il2CppString*)nameObj,
                m_fn_string_to_utf8, m_fn_string_chars, m_fn_string_length);
        }
    }

    // 方法 2: 通过字段偏移读取名称字符串引用
    if (m_offsets.pb_name) {
        Il2CppString* nameStr = *(Il2CppString**)((char*)obj + m_offsets.pb_name);
        if (nameStr) {
            return Il2CppStringToUtf8(nameStr,
                m_fn_string_to_utf8, m_fn_string_chars, m_fn_string_length);
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

    // 获取当前相机信息
    CameraInfo cam = m_camera;
    // 不再要求 cam.valid (Camera 读取可能失败), 只要屏幕尺寸有效就尝试绘制
    if (cam.screen_w <= 0 || cam.screen_h <= 0) {
        // 屏幕尺寸尚未初始化, 从 ImGui IO 兜底
        ImGuiIO& io = ImGui::GetIO();
        cam.screen_w = io.DisplaySize.x;
        cam.screen_h = io.DisplaySize.y;
    }
    if (cam.zoom <= 0) cam.zoom = 30.0f;  // zoom 兜底
    if (cam.screen_w <= 0 || cam.screen_h <= 0) return;

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
    if (!WorldToScreen(obj.world_x, obj.world_y, screenX, screenY, cam)) {
        return; // 对象不在屏幕范围内
    }

    // 计算屏幕上的半径 (世界半径 × 缩放 × 用户微调)
    float scale = cam.screen_h / (2.0f * cam.zoom);
    float screenRadius = obj.radius * scale * config.circle_scale;
    if (screenRadius < 2.0f) screenRadius = 2.0f;    // 最小半径
    if (screenRadius > 2000.0f) screenRadius = 2000.0f; // 最大半径

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

        // 内部填充 (半透明) — 手动提取 RGBA (此 imgui 版本无 IM_COL32_R/G/B 宏)
        bgDraw->AddCircleFilled(
            ImVec2(screenX, screenY),
            screenRadius,
            IM_COL32(
                (int)((color >> IM_COL32_R_SHIFT) & 0xFF),
                (int)((color >> IM_COL32_G_SHIFT) & 0xFF),
                (int)((color >> IM_COL32_B_SHIFT) & 0xFF),
                30  // 低透明度填充
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
    MethodInfo* methodRename = m_fn_class_get_method_from_name(m_classGameCoreCenter, "Rename", 1);
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
