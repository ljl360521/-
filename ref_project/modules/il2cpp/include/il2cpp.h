#ifndef IL2CPP_H
#define IL2CPP_H

#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <mutex>
#include <algorithm>
#include <fstream>
#include <sstream>
#include "xdl.h"
#include "logger.h"
#include "dobby.h"
#include "il2cpp-class.h"
#include "il2cpp-tabledefs.h"

// ===== IL2CPP Opaque Types (API layer) =====

struct Il2CppObject_API {
    void* klass;
    void* monitor;
};

struct Il2CppArray {
    Il2CppObject_API obj;
    void* bounds;
    size_t max_length;
    void* vector[0];
};

struct Il2CppClass_API;
struct Il2CppImage;
struct Il2CppDomain;
struct Il2CppString_API;
struct Il2CppThread;
struct Il2CppException;

// ===== .ctor Hook Info =====

struct CtorHookInfo {
    Il2CppClass_API* klass = nullptr;
    void* ctorAddr = nullptr;
    void* originalFunc = nullptr;
    std::vector<Il2CppObject_API*> captured;
    std::mutex mtx;
    bool active = false;
};

// ===== IL2CPP API Function Pointer Types =====
// Consolidated with X-macro table for loading

#define IL2CPP_API_TABLE_REQUIRED(X) \
    X(domain_get,                Il2CppDomain*,      ()) \
    X(domain_get_assemblies,     void**,             (Il2CppDomain*, size_t*)) \
    X(assembly_get_image,        Il2CppImage*,       (void*)) \
    X(image_get_name,            const char*,        (Il2CppImage*)) \
    X(image_get_class_count,     size_t,             (Il2CppImage*)) \
    X(class_from_name,           Il2CppClass_API*,   (Il2CppImage*, const char*, const char*)) \
    X(class_from_type,           Il2CppClass_API*,   (const Il2CppType*)) \
    X(class_get_name,            const char*,        (Il2CppClass_API*)) \
    X(class_get_namespace,       const char*,        (Il2CppClass_API*)) \
    X(class_get_parent,          Il2CppClass_API*,   (Il2CppClass_API*)) \
    X(class_get_type,            const Il2CppType*,  (Il2CppClass_API*)) \
    X(class_get_flags,           int,                (Il2CppClass_API*)) \
    X(class_is_valuetype,        bool,               (Il2CppClass_API*)) \
    X(class_is_enum,             bool,               (Il2CppClass_API*)) \
    X(class_get_interfaces,      Il2CppClass_API*,   (Il2CppClass_API*, void**)) \
    X(class_get_methods,         const MethodInfo*,  (Il2CppClass_API*, void**)) \
    X(class_get_fields,          FieldInfo*,         (Il2CppClass_API*, void**)) \
    X(class_get_method_from_name,const MethodInfo*,  (Il2CppClass_API*, const char*, int)) \
    X(class_get_field_from_name, FieldInfo*,         (Il2CppClass_API*, const char*)) \
    X(class_is_subclass_of,      bool,               (Il2CppClass_API*, Il2CppClass_API*, bool)) \
    X(method_get_name,           const char*,        (const MethodInfo*)) \
    X(method_get_return_type,    const Il2CppType*,  (const MethodInfo*)) \
    X(method_get_flags,          uint32_t,           (const MethodInfo*, uint32_t*)) \
    X(method_get_param_count,    int,                (const MethodInfo*)) \
    X(method_get_param,          const Il2CppType*,  (const MethodInfo*, uint32_t)) \
    X(method_get_param_name,     const char*,        (const MethodInfo*, uint32_t)) \
    X(field_get_name,            const char*,        (FieldInfo*)) \
    X(field_get_type,            const Il2CppType*,  (FieldInfo*)) \
    X(field_get_flags,           uint32_t,           (FieldInfo*)) \
    X(field_get_offset,          size_t,             (FieldInfo*)) \
    X(field_get_value,           void,               (Il2CppObject_API*, FieldInfo*, void*)) \
    X(field_set_value,           void,               (Il2CppObject_API*, FieldInfo*, void*)) \
    X(field_static_get_value,    void,               (FieldInfo*, void*)) \
    X(field_static_set_value,    void,               (FieldInfo*, void*)) \
    X(string_new,                Il2CppString_API*,  (const char*)) \
    X(string_chars,              const char*,        (Il2CppString_API*)) \
    X(string_length,             int,                (Il2CppString_API*)) \
    X(object_new,                Il2CppObject_API*,  (Il2CppClass_API*)) \
    X(object_get_class,          Il2CppClass_API*,   (Il2CppObject_API*)) \
    X(runtime_invoke,            Il2CppObject_API*,  (const MethodInfo*, Il2CppObject_API*, void**, Il2CppException**)) \
    X(type_get_object,           Il2CppObject_API*,  (const Il2CppType*)) \
    X(thread_attach,             Il2CppThread*,      (Il2CppDomain*)) \
    X(thread_detach,             void,               (Il2CppThread*)) \
    X(thread_current,            Il2CppThread*,      ())

#define IL2CPP_API_TABLE_OPTIONAL(X) \
    X(image_get_class,           Il2CppClass_API*,   (Il2CppImage*, size_t)) \
    X(class_get_properties,      const PropertyInfo*,(Il2CppClass_API*, void**)) \
    X(class_get_declaring_type,  Il2CppClass_API*,   (Il2CppClass_API*)) \
    X(property_get_name,         const char*,        (const PropertyInfo*)) \
    X(property_get_get_method,   const MethodInfo*,  (const PropertyInfo*)) \
    X(property_get_set_method,   const MethodInfo*,  (const PropertyInfo*)) \
    X(type_is_byref,             bool,               (const Il2CppType*)) \
    X(array_new,                 Il2CppArray*,       (Il2CppClass_API*, uint32_t)) \
    X(array_new_specific,        Il2CppArray*,       (Il2CppClass_API*, uint32_t)) \
    X(array_length,              size_t,             (Il2CppArray*)) \
    X(array_get,                 void*,              (Il2CppArray*, uint32_t)) \
    X(array_set,                 void,               (Il2CppArray*, uint32_t, void*)) \
    X(array_element_class,       Il2CppClass_API*,   (Il2CppArray*)) \
    X(array_rank,                uint32_t,           (Il2CppArray*))

// Generate typedefs from tables
#define DECLARE_API_TYPEDEF(name, ret, args) typedef ret (*il2cpp_##name##_t) args;
IL2CPP_API_TABLE_REQUIRED(DECLARE_API_TYPEDEF)
IL2CPP_API_TABLE_OPTIONAL(DECLARE_API_TYPEDEF)
#undef DECLARE_API_TYPEDEF

// ===== Main Runtime Tool Class =====

class IL2CPPRuntimeTool {
private:
    // --- Library handle & base ---
    void* libil2cpp_handle = nullptr;
    uint64_t il2cpp_base = 0;

    // --- State flags ---
    std::mutex api_mutex;
    bool initialized = false;
    bool apis_loaded = false;
    bool unity_cache_initialized = false;

    // --- API function pointers (default-initialized to nullptr) ---
    #define DECLARE_API_PTR(name, ret, args) il2cpp_##name##_t name = nullptr;
    IL2CPP_API_TABLE_REQUIRED(DECLARE_API_PTR)
    IL2CPP_API_TABLE_OPTIONAL(DECLARE_API_PTR)
    #undef DECLARE_API_PTR

    // --- Caches ---
    std::map<std::string, Il2CppClass_API*> class_cache;
    std::map<std::string, Il2CppImage*> image_cache;
    std::map<std::string, const MethodInfo*> method_cache;
    Il2CppDomain* current_domain = nullptr;
    Il2CppThread* attached_thread = nullptr;
    Il2CppClass_API* unity_object_class = nullptr;
    const MethodInfo* find_objects_of_type_method = nullptr;

    // --- object_new hook state ---
    static void* original_object_new_func;
    void* object_new_real_addr = nullptr;
    std::unordered_map<Il2CppClass_API*, std::vector<Il2CppObject_API*>> tracked_objects;
    std::unordered_set<Il2CppClass_API*> tracked_classes;
    std::mutex track_mutex;
    bool object_new_hooked = false;
    bool early_hook_mode = false;
    std::vector<std::pair<Il2CppClass_API*, Il2CppObject_API*>> early_buffer;
    static constexpr size_t EARLY_BUFFER_MAX = 50000;

    static Il2CppObject_API* hooked_object_new(Il2CppClass_API* klass);
    void flushEarlyBuffer();

    // --- .ctor hook state ---
    std::unordered_map<Il2CppClass_API*, std::vector<std::unique_ptr<CtorHookInfo>>> ctor_hooks;
    std::mutex ctor_mutex;
    bool hookClassCtor(Il2CppClass_API* klass);

    // --- Internal helpers ---
    bool isObjectValid(Il2CppObject_API* obj, Il2CppClass_API* expectedClass);
    bool loadAPIs();
    bool initializeUnityCache();
    Il2CppClass_API* findClassInternal(const std::string& nameSpace, const std::string& className);
    bool ensureInitialized();
    Il2CppObject_API* getTypeObject(Il2CppClass_API* klass);

    // --- Dump helpers ---
    std::string getMethodModifier(uint32_t flags);
    bool typeIsByrefSafe(const Il2CppType* type);
    std::string dumpMethods(Il2CppClass_API* klass);
    std::string dumpFields(Il2CppClass_API* klass);
    std::string dumpProperties(Il2CppClass_API* klass);
    std::string dumpTypeStr(const Il2CppType* type);

    // --- Shared helpers ---

    // Build "Namespace.ClassName" from raw strings
    static std::string buildFullName(const char* ns, const char* name) {
        if (ns && ns[0] != '\0') return std::string(ns) + "." + name;
        return name ? name : "";
    }

    // Build fully qualified class name with nested type chain:
    // e.g. "System.Collections.Generic.Dictionary.Enumerator"
    // Walks declaring type chain via il2cpp_class_get_declaring_type
    std::string getClassFullName(Il2CppClass_API* klass);

    // --- Field accessor helper (reduces template boilerplate) ---
    FieldInfo* resolveField(Il2CppObject_API* obj, const std::string& fieldName, Il2CppClass_API** outKlass = nullptr) {
        if (!obj || !ensureInitialized()) return nullptr;
        auto* klass = object_get_class(obj);
        if (!klass) return nullptr;
        if (outKlass) *outKlass = klass;
        return class_get_field_from_name(klass, fieldName.c_str());
    }

    FieldInfo* resolveStaticField(Il2CppClass_API* klass, const std::string& fieldName) {
        if (!klass || !ensureInitialized()) return nullptr;
        return class_get_field_from_name(klass, fieldName.c_str());
    }

public:
    IL2CPPRuntimeTool() = default;
    ~IL2CPPRuntimeTool();

    // --- Lifecycle ---
    bool initialize();
    void cleanup();
    bool isInitialized() const { return initialized; }
    Il2CppDomain* getDomain() const { return current_domain; }
    bool reattachThread();

    // --- Thread management ---
    Il2CppThread* attachCurrentThread() {
        return (current_domain && thread_attach) ? thread_attach(current_domain) : nullptr;
    }
    void detachThread(Il2CppThread* thread) {
        if (thread && thread_detach) thread_detach(thread);
    }

    // --- Class lookup ---
    Il2CppClass_API* findClass(const std::string& nameSpace, const std::string& className);
    const char* getClassName(Il2CppClass_API* klass);
    const char* getClassNamespace(Il2CppClass_API* klass);
    Il2CppClass_API* getObjectClass(Il2CppObject_API* obj) {
        return (obj && object_get_class) ? object_get_class(obj) : nullptr;
    }
    Il2CppClass_API* getParentClass(Il2CppClass_API* klass) {
        return (klass && class_get_parent) ? class_get_parent(klass) : nullptr;
    }
    bool isSubclassOf(Il2CppClass_API* klass, Il2CppClass_API* parentClass);
    std::string getClassInfo(Il2CppClass_API* klass);

    // --- Assembly ---
    Il2CppImage* findAssembly(const std::string& assemblyName);
    std::vector<Il2CppImage*> getAllAssemblies();
    std::string getAssemblyName(Il2CppImage* image);
    std::vector<Il2CppClass_API*> getAllClasses();

    // --- Method ---
    const MethodInfo* findMethod(Il2CppClass_API* klass, const std::string& methodName, int paramCount = -1);
    void* extractMethodPointer(const MethodInfo* method);

    // --- Object creation & invocation ---
    Il2CppObject_API* createInstance(Il2CppClass_API* klass);
    Il2CppObject_API* invokeMethod(const MethodInfo* method, Il2CppObject_API* instance, void** args = nullptr);
    Il2CppObject_API* getClassTypeObject(Il2CppClass_API* klass) { return getTypeObject(klass); }

    // --- String ---
    Il2CppString_API* createString(const std::string& str);
    std::string getString(Il2CppString_API* str);

    // --- Field access (templates) ---
    template<typename T>
    T getFieldValue(Il2CppObject_API* obj, const std::string& fieldName) {
        auto* field = resolveField(obj, fieldName);
        if (!field) return T{};
        T value{};
        field_get_value(obj, field, &value);
        return value;
    }

    template<typename T>
    void setFieldValue(Il2CppObject_API* obj, const std::string& fieldName, const T& value) {
        auto* field = resolveField(obj, fieldName);
        if (field) field_set_value(obj, field, const_cast<T*>(&value));
    }

    template<typename T>
    T getStaticFieldValue(Il2CppClass_API* klass, const std::string& fieldName) {
        auto* field = resolveStaticField(klass, fieldName);
        if (!field) return T{};
        T value{};
        field_static_get_value(field, &value);
        return value;
    }

    template<typename T>
    void setStaticFieldValue(Il2CppClass_API* klass, const std::string& fieldName, const T& value) {
        auto* field = resolveStaticField(klass, fieldName);
        if (field) field_static_set_value(field, const_cast<T*>(&value));
    }

    // --- Raw field accessors ---
    FieldInfo* getFields(Il2CppClass_API* klass, void** iter) {
        return (klass && class_get_fields) ? class_get_fields(klass, iter) : nullptr;
    }
    uint32_t getFieldFlags(FieldInfo* field) {
        return (field && field_get_flags) ? field_get_flags(field) : 0;
    }
    const char* getFieldName(FieldInfo* field) {
        return (field && field_get_name) ? field_get_name(field) : nullptr;
    }
    size_t getFieldOffset(FieldInfo* field) {
        return (field && field_get_offset) ? field_get_offset(field) : 0;
    }

    // --- Unity object validity ---
    bool isUnityObjectAlive(Il2CppObject_API* obj);
    bool isSceneValid();

    // --- Object finding (FindObjectsOfType + tracking) ---
    std::vector<Il2CppObject_API*> findObjectsByType(Il2CppClass_API* targetClass);
    std::vector<Il2CppObject_API*> findObjectsByType(const std::string& nameSpace, const std::string& className);

    // --- Object tracking (object_new hook) ---
    bool startEarlyTracking();
    void finalizeTracking();
    bool startObjectTracking();
    void stopObjectTracking();
    bool isObjectTrackingActive() const { return object_new_hooked; }
    void trackClass(Il2CppClass_API* klass);
    void trackClass(const std::string& nameSpace, const std::string& className);
    void untrackClass(Il2CppClass_API* klass);
    std::vector<Il2CppObject_API*> getTrackedObjects(Il2CppClass_API* klass);
    std::vector<Il2CppObject_API*> getTrackedObjects(const std::string& nameSpace, const std::string& className);
    void cleanupDeadObjects(Il2CppClass_API* klass);
    void cleanupAllDeadObjects();
    size_t getTrackedClassCount() const { return tracked_classes.size(); }
    size_t getTotalTrackedObjectCount();

    // --- .ctor hook ---
    bool isCtorHooked(Il2CppClass_API* klass);
    std::vector<Il2CppObject_API*> getCtorCapturedObjects(Il2CppClass_API* klass);
    void clearCtorCapturedObjects(Il2CppClass_API* klass);
    void stopCtorHook(Il2CppClass_API* klass);
    void stopAllCtorHooks();

    // --- Array support ---
    Il2CppArray* createArray(Il2CppClass_API* elementClass, uint32_t length);
    Il2CppArray* createInt32Array(uint32_t length);
    Il2CppArray* createFloatArray(uint32_t length);
    Il2CppArray* createBoolArray(uint32_t length);
    Il2CppArray* createStringArray(uint32_t length);
    Il2CppArray* createArrayFromClass(const std::string& nameSpace, const std::string& className, uint32_t length);

    size_t getArrayLength(Il2CppArray* arr);
    Il2CppClass_API* getArrayElementClass(Il2CppArray* arr);
    uint32_t getArrayRank(Il2CppArray* arr);
    void* getArrayDataPtr(Il2CppArray* arr);

    template<typename T>
    T getArrayElement(Il2CppArray* arr, uint32_t index) {
        if (!arr || index >= getArrayLength(arr)) return T{};
        return reinterpret_cast<T*>(&arr->vector[0])[index];
    }

    template<typename T>
    void setArrayElement(Il2CppArray* arr, uint32_t index, const T& value) {
        if (!arr || index >= getArrayLength(arr)) return;
        reinterpret_cast<T*>(&arr->vector[0])[index] = value;
    }

    Il2CppObject_API* getArrayObjectElement(Il2CppArray* arr, uint32_t index);
    void setArrayObjectElement(Il2CppArray* arr, uint32_t index, Il2CppObject_API* value);
    Il2CppString_API* getArrayStringElement(Il2CppArray* arr, uint32_t index);
    void setArrayStringElement(Il2CppArray* arr, uint32_t index, Il2CppString_API* value);
    void setArrayElements(Il2CppArray* arr, uint32_t startIndex, const void* values, uint32_t count, size_t elemSize);
    std::vector<Il2CppObject_API*> getAllArrayObjectElements(Il2CppArray* arr);

    template<typename T>
    std::vector<T> arrayToVector(Il2CppArray* arr) {
        if (!arr) return {};
        size_t len = getArrayLength(arr);
        std::vector<T> result;
        result.reserve(len);
        T* data = reinterpret_cast<T*>(&arr->vector[0]);
        for (size_t i = 0; i < len; i++) result.push_back(data[i]);
        return result;
    }

    template<typename T>
    Il2CppArray* vectorToArray(Il2CppClass_API* elementClass, const std::vector<T>& vec) {
        if (!elementClass) return nullptr;
        auto* arr = createArray(elementClass, vec.size());
        if (!arr) return nullptr;
        T* data = reinterpret_cast<T*>(&arr->vector[0]);
        for (size_t i = 0; i < vec.size(); i++) data[i] = vec[i];
        return arr;
    }

    // --- Type name ---
    std::string getFullTypeName(const Il2CppType* type);

    // --- Dump ---
    bool dumpGameData(const std::string& filePath);
};

extern std::unique_ptr<IL2CPPRuntimeTool> g_il2cpp_tool;

#endif // IL2CPP_H