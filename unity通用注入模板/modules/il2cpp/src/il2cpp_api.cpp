// ============================================================================
// IL2CPP API 封装实现
// 通过 dlopen("libil2cpp.so") 动态获取 IL2CPP 导出函数
// 用于访问 Unity IL2CPP 游戏的托管对象
// ============================================================================

#include "il2cpp_api.h"
#include <dlfcn.h>
#include <cstring>
#include "logger.h"

// IL2CPP 内部结构前向声明（简化版，用于遍历程序集）
// Il2CppAssembly 内部包含 image 指针
struct Il2CppAssemblyInternal {
    void* image;  // Il2CppImage* 在不同版本位置不同，这里作为占位
};

// IL2CPP API 函数指针
static il2cpp_domain_get_t             p_il2cpp_domain_get = nullptr;
static il2cpp_domain_get_assemblies_t  p_il2cpp_domain_get_assemblies = nullptr;
static il2cpp_image_get_name_t         p_il2cpp_image_get_name = nullptr;
static il2cpp_class_from_name_t        p_il2cpp_class_from_name = nullptr;
static il2cpp_class_get_field_from_name_t p_il2cpp_class_get_field_from_name = nullptr;
static il2cpp_field_get_offset_t       p_il2cpp_field_get_offset = nullptr;
static il2cpp_field_static_get_value_t p_il2cpp_field_static_get_value = nullptr;
static il2cpp_class_get_static_field_data_t p_il2cpp_class_get_static_field_data = nullptr;
static il2cpp_class_get_name_t         p_il2cpp_class_get_name = nullptr;
static il2cpp_resolve_icall_t          p_il2cpp_resolve_icall = nullptr;

// 额外的 API
typedef const Il2CppImage* (*il2cpp_assembly_get_image_t)(const Il2CppAssembly*);
static il2cpp_assembly_get_image_t p_il2cpp_assembly_get_image = nullptr;

static void* g_il2cpp_handle = nullptr;
static bool g_il2cpp_initialized = false;

template<typename T>
static T resolve_sym(const char* name) {
    if (!g_il2cpp_handle) return nullptr;
    return (T)dlsym(g_il2cpp_handle, name);
}

bool il2cpp_api_init() {
    if (g_il2cpp_initialized) return true;

    g_il2cpp_handle = dlopen("libil2cpp.so", RTLD_NOW | RTLD_GLOBAL);
    if (!g_il2cpp_handle) {
        LOGE("il2cpp_api: dlopen libil2cpp.so 失败: %s", dlerror());
        return false;
    }

    p_il2cpp_domain_get = resolve_sym<il2cpp_domain_get_t>("il2cpp_domain_get");
    p_il2cpp_domain_get_assemblies = resolve_sym<il2cpp_domain_get_assemblies_t>("il2cpp_domain_get_assemblies");
    p_il2cpp_image_get_name = resolve_sym<il2cpp_image_get_name_t>("il2cpp_image_get_name");
    p_il2cpp_class_from_name = resolve_sym<il2cpp_class_from_name_t>("il2cpp_class_from_name");
    p_il2cpp_class_get_field_from_name = resolve_sym<il2cpp_class_get_field_from_name_t>("il2cpp_class_get_field_from_name");
    p_il2cpp_field_get_offset = resolve_sym<il2cpp_field_get_offset_t>("il2cpp_field_get_offset");
    p_il2cpp_field_static_get_value = resolve_sym<il2cpp_field_static_get_value_t>("il2cpp_field_static_get_value");
    p_il2cpp_class_get_static_field_data = resolve_sym<il2cpp_class_get_static_field_data_t>("il2cpp_class_get_static_field_data");
    p_il2cpp_class_get_name = resolve_sym<il2cpp_class_get_name_t>("il2cpp_class_get_name");
    p_il2cpp_resolve_icall = resolve_sym<il2cpp_resolve_icall_t>("il2cpp_resolve_icall");
    p_il2cpp_assembly_get_image = resolve_sym<il2cpp_assembly_get_image_t>("il2cpp_assembly_get_image");

    if (!p_il2cpp_domain_get || !p_il2cpp_class_from_name || !p_il2cpp_class_get_field_from_name || !p_il2cpp_assembly_get_image) {
        LOGE("il2cpp_api: 解析核心符号失败 (domain=%p class_from_name=%p get_field=%p get_image=%p)",
             (void*)p_il2cpp_domain_get, (void*)p_il2cpp_class_from_name,
             (void*)p_il2cpp_class_get_field_from_name, (void*)p_il2cpp_assembly_get_image);
        return false;
    }

    g_il2cpp_initialized = true;
    LOGI("il2cpp_api: 初始化成功");
    return true;
}

Il2CppClass* il2cpp_get_class(const char* namespace_name, const char* class_name) {
    if (!g_il2cpp_initialized || !p_il2cpp_domain_get || !p_il2cpp_domain_get_assemblies) return nullptr;

    Il2CppDomain* domain = p_il2cpp_domain_get();
    if (!domain) return nullptr;

    size_t count = 0;
    Il2CppAssembly** assemblies = p_il2cpp_domain_get_assemblies(domain, &count);
    if (!assemblies || count == 0) return nullptr;

    // 遍历所有程序集，获取 image，用 il2cpp_class_from_name 查找类
    for (size_t i = 0; i < count; i++) {
        if (!assemblies[i]) continue;

        const Il2CppImage* image = nullptr;
        if (p_il2cpp_assembly_get_image) {
            image = p_il2cpp_assembly_get_image(assemblies[i]);
        }
        if (!image) continue;

        Il2CppClass* klass = p_il2cpp_class_from_name(image, namespace_name, class_name);
        if (klass) return klass;
    }

    return nullptr;
}

size_t il2cpp_get_field_offset(Il2CppClass* klass, const char* field_name) {
    if (!g_il2cpp_initialized || !p_il2cpp_class_get_field_from_name || !p_il2cpp_field_get_offset || !klass) return (size_t)-1;
    FieldInfo* field = p_il2cpp_class_get_field_from_name(klass, field_name);
    if (!field) return (size_t)-1;
    return p_il2cpp_field_get_offset(field);
}

void il2cpp_get_static_field_value(Il2CppClass* klass, const char* field_name, void* out_value) {
    if (!g_il2cpp_initialized || !p_il2cpp_class_get_field_from_name || !p_il2cpp_field_static_get_value || !klass) return;
    FieldInfo* field = p_il2cpp_class_get_field_from_name(klass, field_name);
    if (!field) return;
    p_il2cpp_field_static_get_value(field, out_value);
}

void* il2cpp_get_static_field_data(Il2CppClass* klass) {
    if (!g_il2cpp_initialized || !p_il2cpp_class_get_static_field_data || !klass) return nullptr;
    return p_il2cpp_class_get_static_field_data(klass);
}

void* il2cpp_get_icall(const char* name) {
    if (!g_il2cpp_initialized || !p_il2cpp_resolve_icall) return nullptr;
    return p_il2cpp_resolve_icall(name);
}
