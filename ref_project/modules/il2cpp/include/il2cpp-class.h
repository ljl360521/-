#ifndef IL2CPP_CLASS_H
#define IL2CPP_CLASS_H

#include <stdint.h>

// 这些结构体定义参考 Perfare/Il2CppDumper 以及 Unity IL2CPP 源码
// 用于直接访问结构体成员（如 method->methodPointer, type->byref, param->attrs）

typedef uint16_t Il2CppChar;
typedef uintptr_t il2cpp_array_size_t;

// ===== Forward declarations =====
struct Il2CppGenericClass;
struct Il2CppGenericInst;
struct Il2CppGenericContext;
struct Il2CppArrayType;

// ===== Il2CppType =====
struct Il2CppType {
    union {
        void *dummy;
        int32_t klassIndex;
        const Il2CppType *type;           // for PTR / BYREF
        Il2CppArrayType *array;           // for ARRAY (multi-dimensional)
        int32_t genericParameterIndex;    // for VAR / MVAR (older Unity)
        Il2CppGenericClass *generic_class; // for GENERICINST
    } data;
    unsigned int attrs : 16;
    unsigned int type : 8;
    unsigned int num_mods : 6;
    unsigned int byref : 1;
    unsigned int pinned : 1;
};

// ===== Generic type structures (for resolving Dictionary<K,V> etc.) =====

// Il2CppGenericInst: holds an array of Il2CppType* (the type arguments)
struct Il2CppGenericInst {
    uint32_t type_argc;           // number of type arguments
    const Il2CppType **type_argv; // array of type argument pointers
};

// Il2CppGenericContext: contains class-level and method-level generic args
struct Il2CppGenericContext {
    const Il2CppGenericInst *class_inst;  // generic arguments for the class
    const Il2CppGenericInst *method_inst; // generic arguments for the method
};

// Il2CppGenericClass: links a generic type definition to its instantiation
struct Il2CppGenericClass {
    const Il2CppType *type;           // the generic type definition (e.g., Dictionary`2)
    Il2CppGenericContext context;     // the concrete type arguments
    void *cached_class;              // Il2CppClass* (internal cache, don't use)
};

// Il2CppArrayType: for multi-dimensional arrays (ARRAY, not SZARRAY)
struct Il2CppArrayType {
    const Il2CppType *etype;  // element type
    uint8_t rank;             // number of dimensions
    uint8_t numsizes;
    uint8_t numlobounds;
    int *sizes;
    int *lobounds;
};

// ===== Generic parameter name resolution structures =====
// These are used for VAR/MVAR in newer Unity versions (2019.3+) where
// genericParameterIndex is replaced by Il2CppGenericParameter*

struct Il2CppGenericParameter {
    int32_t ownerIndex;    // index into generic container
    const char *name;      // parameter name like "T", "TKey", "TValue"
    int16_t num;           // parameter position
    uint16_t flags;
    int32_t constraintsStart;
    int16_t constraintsCount;
};

// ===== Class / Method / Field / Property =====

struct Il2CppClass_1;

struct VirtualInvokeData {
    void *methodPtr;
    const struct MethodInfo *method;
};

struct MethodInfo {
    void *methodPointer;
    void *invoker_method;
    const char *name;
    Il2CppClass_1 *klass;
    const Il2CppType *return_type;
    const void *parameters; // ParameterInfo* 或 Il2CppType** 取决于版本
    union {
        const void *rgctx_data;
        const void *methodDefinition;
    };
    union {
        const void *genericMethod;
        const void *genericContainer;
    };
    uint32_t token;
    uint16_t flags;
    uint16_t iflags;
    uint16_t slot;
    uint8_t parameters_count;
    // ...
};

struct FieldInfo {
    const char *name;
    const Il2CppType *type;
    Il2CppClass_1 *parent;
    int32_t offset;
    uint32_t token;
};

struct PropertyInfo {
    const MethodInfo *get;
    const MethodInfo *set;
    const char *name;
    Il2CppClass_1 *parent;
    uint32_t attrs;
    uint32_t token;
};

// ===== Dump-only structures =====

struct Il2CppObject_Dump {
    void *klass;
    void *monitor;
};

struct Il2CppArrayBounds {
    il2cpp_array_size_t length;
    int32_t lower_bound;
};

struct Il2CppArray_Dump {
    Il2CppObject_Dump obj;
    Il2CppArrayBounds *bounds;
    il2cpp_array_size_t max_length;
    void *vector[1]; // flexible
};

struct Il2CppString_Dump {
    Il2CppObject_Dump obj;
    int32_t length;
    Il2CppChar chars[1]; // flexible
};

struct Il2CppReflectionType {
    Il2CppObject_Dump object;
    const Il2CppType *type;
};

#endif // IL2CPP_CLASS_H