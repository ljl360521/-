#pragma once

#include <cstdint>
#include <string>

// ============================================================================
// IL2CPP API 封装
// 通过 dlopen("libil2cpp.so") 动态获取 IL2CPP 导出函数
// 用于访问 Unity IL2CPP 游戏的托管对象（如 GameCoreCenter.BallDic）
// 不需要硬编码字段偏移，通过 il2cpp_field_get_offset 动态获取
// ============================================================================

// IL2CPP 基础类型前向声明
struct Il2CppClass;
struct Il2CppType;
struct Il2CppObject;
struct MethodInfo;
struct FieldInfo;
struct Il2CppDomain;
struct Il2CppAssembly;
struct Il2CppImage;
struct Il2CppString;

// IL2CPP API 函数指针类型
typedef Il2CppDomain* (*il2cpp_domain_get_t)();
typedef Il2CppAssembly** (*il2cpp_domain_get_assemblies_t)(Il2CppDomain*, size_t*);
typedef const char* (*il2cpp_image_get_name_t)(const Il2CppImage*);
typedef Il2CppClass* (*il2cpp_class_from_name_t)(const Il2CppImage*, const char*, const char*);
typedef FieldInfo* (*il2cpp_class_get_field_from_name_t)(Il2CppClass*, const char*);
typedef size_t (*il2cpp_field_get_offset_t)(FieldInfo*);
typedef void (*il2cpp_field_static_get_value_t)(FieldInfo*, void*);
typedef void* (*il2cpp_class_get_static_field_data_t)(Il2CppClass*);
typedef const char* (*il2cpp_class_get_name_t)(Il2CppClass*);
typedef Il2CppObject* (*il2cpp_object_new_t)(Il2CppClass*);
typedef Il2CppString* (*il2cpp_string_new_t)(const char*);
typedef void* (*il2cpp_resolve_icall_t)(const char*);
typedef MethodInfo* (*il2cpp_class_get_methods_t)(Il2CppClass*, void**);
typedef const char* (*il2cpp_method_get_name_t)(const MethodInfo*);
typedef void* (*il2cpp_method_get_invoker_t)(const MethodInfo*);

// 初始化 IL2CPP API（dlopen libil2cpp.so 并解析符号）
// 返回 true 表示成功
bool il2cpp_api_init();

// 获取已加载的 IL2CPP 类
// namespace_name: 命名空间（如 "" 或 "UnityEngine"）
// class_name: 类名（如 "GameCoreCenter"）
Il2CppClass* il2cpp_get_class(const char* namespace_name, const char* class_name);

// 获取类的实例字段偏移
size_t il2cpp_get_field_offset(Il2CppClass* klass, const char* field_name);

// 获取类的静态字段值
// field_name: 静态字段名
// out_value: 输出缓冲区
void il2cpp_get_static_field_value(Il2CppClass* klass, const char* field_name, void* out_value);

// 获取类的静态字段数据区指针
void* il2cpp_get_static_field_data(Il2CppClass* klass);

// 调用 icall（Unity 内部函数）
// name: icall 名称（如 "UnityEngine.Transform::get_position_Injected"）
void* il2cpp_get_icall(const char* name);

// 读取对象实例字段
template<typename T>
T il2cpp_read_field(void* obj, size_t offset) {
    if (!obj) return T{};
    return *(T*)((char*)obj + offset);
}

// 读取指针字段（用于指针跳转）
inline void* il2cpp_read_ptr(void* obj, size_t offset) {
    if (!obj) return nullptr;
    return *(void**)((char*)obj + offset);
}
