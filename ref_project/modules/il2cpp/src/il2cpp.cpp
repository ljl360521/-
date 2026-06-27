#include "il2cpp.h"

std::unique_ptr<IL2CPPRuntimeTool> g_il2cpp_tool;
void* IL2CPPRuntimeTool::original_object_new_func = nullptr;

// ===== Global .ctor hook dispatch =====

static std::unordered_map<void*, CtorHookInfo*> g_ctorAddrMap;
static std::mutex g_ctorAddrMapMutex;

static void* GenericCtorHookFunc(Il2CppObject_API* thisPtr, void* a1, void* a2, void* a3, void* a4, void* a5) {
    if (!thisPtr) return nullptr;

    auto* klass = static_cast<Il2CppClass_API*>(thisPtr->klass);
    CtorHookInfo* info = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_ctorAddrMapMutex);
        for (auto& [addr, entry] : g_ctorAddrMap) {
            if (entry && entry->klass == klass && entry->active) {
                info = entry;
                break;
            }
        }
    }

    if (!info || !info->originalFunc) return nullptr;

    {
        std::lock_guard<std::mutex> lock(info->mtx);
        info->captured.push_back(thisPtr);
    }

    using OrigFunc = void* (*)(Il2CppObject_API*, void*, void*, void*, void*, void*);
    return reinterpret_cast<OrigFunc>(info->originalFunc)(thisPtr, a1, a2, a3, a4, a5);
}

// ===== Lifecycle =====

IL2CPPRuntimeTool::~IL2CPPRuntimeTool() { cleanup(); }

bool IL2CPPRuntimeTool::ensureInitialized() {
    return initialized || initialize();
}

bool IL2CPPRuntimeTool::initialize() {
    std::lock_guard<std::mutex> lock(api_mutex);
    if (initialized) return true;

    // Reopen if handle exists but APIs not loaded
    if (libil2cpp_handle && !apis_loaded) {
        xdl_close(libil2cpp_handle);
        libil2cpp_handle = nullptr;
    }

    if (!libil2cpp_handle) {
        libil2cpp_handle = xdl_open("libil2cpp.so", XDL_DEFAULT);
        if (!libil2cpp_handle) { LOGE("Failed to load libil2cpp.so"); return false; }
        LOGI("Successfully loaded libil2cpp.so");
    }

    // Get base address
    xdl_info_t info;
    if (xdl_info(libil2cpp_handle, XDL_DI_DLINFO, &info)) {
        il2cpp_base = reinterpret_cast<uint64_t>(info.dli_fbase);
        LOGI("il2cpp_base: 0x%llx", static_cast<unsigned long long>(il2cpp_base));
    } else {
        il2cpp_base = 0;
        LOGW("Failed to get il2cpp base address");
    }

    if (!apis_loaded) {
        if (!loadAPIs()) { LOGE("Failed to load IL2CPP APIs"); return false; }
        apis_loaded = true;
    }

    if (!current_domain) {
        current_domain = domain_get();
        if (!current_domain) { LOGE("Failed to get IL2CPP domain"); return false; }
    }

    if (!attached_thread) {
        attached_thread = thread_attach(current_domain);
        if (!attached_thread) { LOGE("Failed to attach thread"); return false; }
    }

    initialized = true;

    if (!unity_cache_initialized && !initializeUnityCache()) {
        LOGW("Failed to initialize Unity cache");
    }

    LOGI("IL2CPP Runtime Tool initialized successfully");
    return true;
}

void IL2CPPRuntimeTool::cleanup() {
    std::lock_guard<std::mutex> lock(api_mutex);

    // Remove .ctor hooks
    {
        std::lock_guard<std::mutex> cLock(ctor_mutex);
        for (auto& [klass, hooks] : ctor_hooks)
            for (auto& h : hooks)
                if (h && h->active && h->ctorAddr) DobbyDestroy(h->ctorAddr);
        ctor_hooks.clear();
    }
    {
        std::lock_guard<std::mutex> gLock(g_ctorAddrMapMutex);
        g_ctorAddrMap.clear();
    }

    // Remove object_new hook
    if (object_new_hooked) {
        if (object_new_real_addr) DobbyDestroy(object_new_real_addr);
        object_new_hooked = false;
        original_object_new_func = nullptr;
    }

    // Clear tracking data
    {
        std::lock_guard<std::mutex> tLock(track_mutex);
        tracked_objects.clear();
        tracked_classes.clear();
        early_buffer.clear();
        early_hook_mode = false;
    }

    // Detach & close
    if (attached_thread && thread_detach) {
        thread_detach(attached_thread);
        attached_thread = nullptr;
    }
    if (libil2cpp_handle) {
        xdl_close(libil2cpp_handle);
        libil2cpp_handle = nullptr;
    }

    // Reset all state
    class_cache.clear();
    image_cache.clear();
    method_cache.clear();
    current_domain = nullptr;
    unity_object_class = nullptr;
    find_objects_of_type_method = nullptr;
    object_new_real_addr = nullptr;
    initialized = false;
    apis_loaded = false;
    unity_cache_initialized = false;
    il2cpp_base = 0;
}

// ===== API Loading (table-driven) =====

bool IL2CPPRuntimeTool::loadAPIs() {
    bool ok = true;

    // Load required APIs — fail on any missing
    #define LOAD_REQUIRED(fname, ret, args) \
        fname = reinterpret_cast<il2cpp_##fname##_t>(xdl_sym(libil2cpp_handle, "il2cpp_" #fname, nullptr)); \
        if (!fname) { LOGE("Failed to load: il2cpp_" #fname); ok = false; }
    IL2CPP_API_TABLE_REQUIRED(LOAD_REQUIRED)
    #undef LOAD_REQUIRED

    if (!ok) return false;

    // Load optional APIs — warn on missing
    #define LOAD_OPTIONAL(fname, ret, args) \
        fname = reinterpret_cast<il2cpp_##fname##_t>(xdl_sym(libil2cpp_handle, "il2cpp_" #fname, nullptr)); \
        if (!fname) LOGW("Optional not found: il2cpp_" #fname);
    IL2CPP_API_TABLE_OPTIONAL(LOAD_OPTIONAL)
    #undef LOAD_OPTIONAL

    LOGI("All IL2CPP APIs loaded successfully");
    return true;
}

// ===== Unity Cache =====

bool IL2CPPRuntimeTool::initializeUnityCache() {
    unity_object_class = findClassInternal("UnityEngine", "Object");
    if (!unity_object_class) {
        LOGE("[UnityCache] UnityEngine.Object not found");
        return false;
    }
    LOGI("[UnityCache] UnityEngine.Object class: %p", unity_object_class);

    // Prefer 2-param version: FindObjectsOfType(Type, bool includeInactive)
    find_objects_of_type_method = class_get_method_from_name(unity_object_class, "FindObjectsOfType", 2);
    if (!find_objects_of_type_method) {
        find_objects_of_type_method = class_get_method_from_name(unity_object_class, "FindObjectsOfType", 1);
    }

    if (find_objects_of_type_method) {
        int pc = method_get_param_count(find_objects_of_type_method);
        LOGI("[UnityCache] Using FindObjectsOfType with %d param(s): %p", pc, find_objects_of_type_method->methodPointer);
    } else {
        LOGE("[UnityCache] FindObjectsOfType not found (neither 1 nor 2 param version)");
        return false;
    }

    unity_cache_initialized = true;
    LOGI("[UnityCache] Unity cache initialized");
    return true;
}

Il2CppObject_API* IL2CPPRuntimeTool::getTypeObject(Il2CppClass_API* klass) {
    if (!klass || !type_get_object || !class_get_type) return nullptr;
    auto* type = class_get_type(klass);
    return type ? type_get_object(type) : nullptr;
}

// ===== Object Validity =====

bool IL2CPPRuntimeTool::isObjectValid(Il2CppObject_API* obj, Il2CppClass_API* expectedClass) {
    if (!obj || !object_get_class) return false;
    if (reinterpret_cast<uintptr_t>(obj) < 0x10000) return false;

    void* klassPtr = obj->klass;
    if (!klassPtr || reinterpret_cast<uintptr_t>(klassPtr) < 0x10000) return false;
    if (expectedClass && klassPtr != static_cast<void*>(expectedClass)) return false;

    if (expectedClass && class_get_name) {
        const char* name = class_get_name(static_cast<Il2CppClass_API*>(klassPtr));
        if (!name || reinterpret_cast<uintptr_t>(name) < 0x10000) return false;
    }
    return true;
}

bool IL2CPPRuntimeTool::isUnityObjectAlive(Il2CppObject_API* obj) {
    if (!obj || reinterpret_cast<uintptr_t>(obj) < 0x10000) return false;
    if (!isObjectValid(obj, nullptr)) return false;
    if (!unity_cache_initialized) return true;

    auto* objClass = findClassInternal("UnityEngine", "Object");
    if (!objClass) return true;

    auto* opImplicit = class_get_method_from_name(objClass, "op_Implicit", 1);
    if (!opImplicit || !opImplicit->methodPointer) return true;

    using OpImplicitFn = bool (*)(void*, const MethodInfo*);
    return reinterpret_cast<OpImplicitFn>(opImplicit->methodPointer)(obj, opImplicit);
}

bool IL2CPPRuntimeTool::isSceneValid() {
    if (!initialized || !unity_cache_initialized) return false;

    auto* camClass = findClassInternal("UnityEngine", "Camera");
    if (!camClass) return false;

    auto* getMain = class_get_method_from_name(camClass, "get_main", 0);
    if (!getMain || !getMain->methodPointer) return false;

    Il2CppException* exc = nullptr;
    auto* cam = runtime_invoke(getMain, nullptr, nullptr, &exc);
    if (exc || !cam || reinterpret_cast<uintptr_t>(cam) < 0x10000) return false;

    return isUnityObjectAlive(cam);
}

// ===== .ctor Hook =====

bool IL2CPPRuntimeTool::hookClassCtor(Il2CppClass_API* klass) {
    if (!klass) return false;
    std::lock_guard<std::mutex> lock(ctor_mutex);

    // Check if already hooked
    if (auto it = ctor_hooks.find(klass); it != ctor_hooks.end())
        for (auto& h : it->second)
            if (h && h->active) return true;

    bool anyHooked = false;
    for (int pc = 0; pc <= 5; pc++) {
        auto* ctor = class_get_method_from_name(klass, ".ctor", pc);
        if (!ctor) continue;
        void* addr = reinterpret_cast<void*>(ctor->methodPointer);
        if (!addr) continue;

        {
            std::lock_guard<std::mutex> gLock(g_ctorAddrMapMutex);
            if (g_ctorAddrMap.count(addr) > 0) continue;
        }

        auto info = std::make_unique<CtorHookInfo>();
        info->klass = klass;
        info->ctorAddr = addr;
        info->active = true;

        {
            std::lock_guard<std::mutex> gLock(g_ctorAddrMapMutex);
            g_ctorAddrMap[addr] = info.get();
        }

        if (DobbyHook(addr, reinterpret_cast<void*>(GenericCtorHookFunc), &info->originalFunc) == 0) {
            ctor_hooks[klass].push_back(std::move(info));
            anyHooked = true;
        } else {
            std::lock_guard<std::mutex> gLock(g_ctorAddrMapMutex);
            g_ctorAddrMap.erase(addr);
        }
    }
    return anyHooked;
}

bool IL2CPPRuntimeTool::isCtorHooked(Il2CppClass_API* klass) {
    if (!klass) return false;
    std::lock_guard<std::mutex> lock(ctor_mutex);
    auto it = ctor_hooks.find(klass);
    if (it == ctor_hooks.end()) return false;
    return std::any_of(it->second.begin(), it->second.end(),
        [](const auto& h) { return h && h->active; });
}

std::vector<Il2CppObject_API*> IL2CPPRuntimeTool::getCtorCapturedObjects(Il2CppClass_API* klass) {
    if (!klass) return {};
    std::lock_guard<std::mutex> lock(ctor_mutex);
    auto it = ctor_hooks.find(klass);
    if (it == ctor_hooks.end()) return {};

    std::vector<Il2CppObject_API*> all;
    for (auto& info : it->second) {
        std::lock_guard<std::mutex> iLock(info->mtx);
        // Clean dead objects in place
        info->captured.erase(
            std::remove_if(info->captured.begin(), info->captured.end(),
                [this, klass](auto* obj) { return !isObjectValid(obj, klass); }),
            info->captured.end());
        all.insert(all.end(), info->captured.begin(), info->captured.end());
    }
    return all;
}

void IL2CPPRuntimeTool::clearCtorCapturedObjects(Il2CppClass_API* klass) {
    if (!klass) return;
    std::lock_guard<std::mutex> lock(ctor_mutex);
    auto it = ctor_hooks.find(klass);
    if (it == ctor_hooks.end()) return;
    for (auto& h : it->second) {
        std::lock_guard<std::mutex> iLock(h->mtx);
        h->captured.clear();
    }
}

void IL2CPPRuntimeTool::stopCtorHook(Il2CppClass_API* klass) {
    if (!klass) return;
    std::lock_guard<std::mutex> lock(ctor_mutex);
    auto it = ctor_hooks.find(klass);
    if (it == ctor_hooks.end()) return;
    for (auto& h : it->second) {
        if (h && h->active && h->ctorAddr) {
            DobbyDestroy(h->ctorAddr);
            h->active = false;
            std::lock_guard<std::mutex> gLock(g_ctorAddrMapMutex);
            g_ctorAddrMap.erase(h->ctorAddr);
        }
    }
}

void IL2CPPRuntimeTool::stopAllCtorHooks() {
    std::lock_guard<std::mutex> lock(ctor_mutex);
    for (auto& [klass, hooks] : ctor_hooks)
        for (auto& h : hooks)
            if (h && h->active && h->ctorAddr) {
                DobbyDestroy(h->ctorAddr);
                h->active = false;
            }
    {
        std::lock_guard<std::mutex> gLock(g_ctorAddrMapMutex);
        g_ctorAddrMap.clear();
    }
    ctor_hooks.clear();
}

// ===== object_new Hook & Tracking =====

Il2CppObject_API* IL2CPPRuntimeTool::hooked_object_new(Il2CppClass_API* klass) {
    if (!original_object_new_func) return nullptr;

    auto* obj = reinterpret_cast<Il2CppObject_API* (*)(Il2CppClass_API*)>(original_object_new_func)(klass);

    if (obj && klass && g_il2cpp_tool) {
        std::unique_lock<std::mutex> lock(g_il2cpp_tool->track_mutex, std::try_to_lock);
        if (lock.owns_lock()) {
            if (g_il2cpp_tool->early_hook_mode) {
                if (g_il2cpp_tool->early_buffer.size() < EARLY_BUFFER_MAX)
                    g_il2cpp_tool->early_buffer.emplace_back(klass, obj);
            } else if (g_il2cpp_tool->tracked_classes.count(klass) > 0) {
                g_il2cpp_tool->tracked_objects[klass].push_back(obj);
            }
        }
    }
    return obj;
}

bool IL2CPPRuntimeTool::startEarlyTracking() {
    if (object_new_hooked) return true;

    if (!libil2cpp_handle) {
        libil2cpp_handle = xdl_open("libil2cpp.so", XDL_DEFAULT);
        if (!libil2cpp_handle) { LOGE("startEarlyTracking: cannot load libil2cpp.so"); return false; }
    }

    object_new_real_addr = xdl_sym(libil2cpp_handle, "il2cpp_object_new", nullptr);
    if (!object_new_real_addr) { LOGE("startEarlyTracking: symbol not found"); return false; }

    early_hook_mode = true;
    early_buffer.reserve(10000);

    if (DobbyHook(object_new_real_addr, reinterpret_cast<void*>(hooked_object_new), &original_object_new_func) == 0) {
        object_new_hooked = true;
        LOGI("Early tracking started at %p", object_new_real_addr);
        return true;
    }
    early_hook_mode = false;
    return false;
}

void IL2CPPRuntimeTool::flushEarlyBuffer() {
    std::lock_guard<std::mutex> lock(track_mutex);
    size_t matched = 0;
    for (auto& [klass, obj] : early_buffer) {
        if (tracked_classes.count(klass) > 0 && isObjectValid(obj, klass)) {
            tracked_objects[klass].push_back(obj);
            matched++;
        }
    }
    LOGI("flushEarlyBuffer: %zu scanned, %zu matched", early_buffer.size(), matched);
}

void IL2CPPRuntimeTool::finalizeTracking() {
    if (!object_new_hooked) return;
    flushEarlyBuffer();
    std::lock_guard<std::mutex> lock(track_mutex);
    early_hook_mode = false;
}

bool IL2CPPRuntimeTool::startObjectTracking() {
    if (object_new_hooked) return true;
    if (!ensureInitialized() || !libil2cpp_handle) return false;

    object_new_real_addr = xdl_sym(libil2cpp_handle, "il2cpp_object_new", nullptr);
    if (!object_new_real_addr) return false;

    if (DobbyHook(object_new_real_addr, reinterpret_cast<void*>(hooked_object_new), &original_object_new_func) == 0) {
        object_new_hooked = true;
        return true;
    }
    return false;
}

void IL2CPPRuntimeTool::stopObjectTracking() {
    if (!object_new_hooked) return;
    if (object_new_real_addr) DobbyDestroy(object_new_real_addr);
    object_new_hooked = false;
    original_object_new_func = nullptr;
    object_new_real_addr = nullptr;
}

void IL2CPPRuntimeTool::trackClass(Il2CppClass_API* klass) {
    if (!klass) return;
    std::lock_guard<std::mutex> lock(track_mutex);
    if (tracked_classes.insert(klass).second)
        tracked_objects[klass]; // ensure entry exists
}

void IL2CPPRuntimeTool::trackClass(const std::string& ns, const std::string& cn) {
    if (auto* k = findClass(ns, cn)) trackClass(k);
}

void IL2CPPRuntimeTool::untrackClass(Il2CppClass_API* klass) {
    if (!klass) return;
    std::lock_guard<std::mutex> lock(track_mutex);
    tracked_classes.erase(klass);
    tracked_objects.erase(klass);
}

std::vector<Il2CppObject_API*> IL2CPPRuntimeTool::getTrackedObjects(Il2CppClass_API* klass) {
    if (!klass) return {};
    // NOTE: cleanupDeadObjects also locks track_mutex, so call it first
    cleanupDeadObjects(klass);
    std::lock_guard<std::mutex> lock(track_mutex);
    auto it = tracked_objects.find(klass);
    return (it != tracked_objects.end()) ? it->second : std::vector<Il2CppObject_API*>{};
}

std::vector<Il2CppObject_API*> IL2CPPRuntimeTool::getTrackedObjects(const std::string& ns, const std::string& cn) {
    auto* k = findClass(ns, cn);
    return k ? getTrackedObjects(k) : std::vector<Il2CppObject_API*>{};
}

void IL2CPPRuntimeTool::cleanupDeadObjects(Il2CppClass_API* klass) {
    if (!klass) return;
    std::lock_guard<std::mutex> lock(track_mutex);
    auto it = tracked_objects.find(klass);
    if (it == tracked_objects.end()) return;
    auto& v = it->second;
    v.erase(std::remove_if(v.begin(), v.end(),
        [this, klass](auto* obj) { return !isObjectValid(obj, klass); }), v.end());
}

void IL2CPPRuntimeTool::cleanupAllDeadObjects() {
    // Collect keys first to avoid nested lock issues
    std::vector<Il2CppClass_API*> classes;
    {
        std::lock_guard<std::mutex> lock(track_mutex);
        classes.assign(tracked_classes.begin(), tracked_classes.end());
    }
    for (auto* k : classes) cleanupDeadObjects(k);
}

size_t IL2CPPRuntimeTool::getTotalTrackedObjectCount() {
    std::lock_guard<std::mutex> lock(track_mutex);
    size_t total = 0;
    for (auto& [klass, objs] : tracked_objects) total += objs.size();
    return total;
}

// ===== FindObjectsByType =====

std::vector<Il2CppObject_API*> IL2CPPRuntimeTool::findObjectsByType(Il2CppClass_API* targetClass) {
    if (!targetClass || !ensureInitialized()) return {};

    const char* cn = class_get_name(targetClass);
    LOGI("[findByType] Looking for: %s (klass=%p)", cn ? cn : "?", targetClass);

    // 1) Try Unity FindObjectsOfType for UnityEngine.Object subclasses
    bool isUnityObj = unity_object_class && class_is_subclass_of &&
                      class_is_subclass_of(targetClass, unity_object_class, false);

    if (isUnityObj && unity_cache_initialized && find_objects_of_type_method) {
        auto* typeObj = getTypeObject(targetClass);
        if (typeObj) {
            int paramCount = method_get_param_count(find_objects_of_type_method);
            Il2CppException* exc = nullptr;
            Il2CppObject_API* arrayObj = nullptr;

            if (paramCount == 2) {
                bool includeInactive = true;
                void* args[2] = { typeObj, &includeInactive };
                arrayObj = runtime_invoke(find_objects_of_type_method, nullptr, args, &exc);
            } else {
                void* args[1] = { typeObj };
                arrayObj = runtime_invoke(find_objects_of_type_method, nullptr, args, &exc);
            }

            if (!exc && arrayObj) {
                auto* arr = reinterpret_cast<Il2CppArray*>(arrayObj);
                std::vector<Il2CppObject_API*> result;
                result.reserve(arr->max_length);
                for (size_t i = 0; i < arr->max_length; i++)
                    if (arr->vector[i])
                        result.push_back(static_cast<Il2CppObject_API*>(arr->vector[i]));
                if (!result.empty()) {
                    LOGI("[findByType] FindObjectsOfType found %zu", result.size());
                    return result;
                }
            }
        }
    }

    // 2) Try tracked objects
    if (object_new_hooked) {
        auto result = getTrackedObjects(targetClass);
        if (!result.empty()) {
            LOGI("[findByType] tracked objects: %zu", result.size());
            return result;
        }
        // Auto-register for future tracking
        std::lock_guard<std::mutex> lock(track_mutex);
        if (tracked_classes.insert(targetClass).second) {
            tracked_objects[targetClass];
            LOGI("[findByType] Registered tracking for: %s", cn ? cn : "?");
        }
    }

    // 3) Try early buffer
    if (!early_buffer.empty()) {
        std::lock_guard<std::mutex> lock(track_mutex);
        std::vector<Il2CppObject_API*> result;
        for (auto& [klass, obj] : early_buffer)
            if (klass == targetClass && isObjectValid(obj, targetClass))
                result.push_back(obj);
        if (!result.empty()) {
            tracked_classes.insert(targetClass);
            tracked_objects[targetClass] = result;
            LOGI("[findByType] early buffer matched: %zu", result.size());
            return result;
        }
    }

    LOGI("[findByType] No instances found for %s", cn ? cn : "?");
    return {};
}

std::vector<Il2CppObject_API*> IL2CPPRuntimeTool::findObjectsByType(const std::string& ns, const std::string& cn) {
    auto* k = findClass(ns, cn);
    return k ? findObjectsByType(k) : std::vector<Il2CppObject_API*>{};
}

// ===== Basic Lookup =====

Il2CppClass_API* IL2CPPRuntimeTool::findClassInternal(const std::string& ns, const std::string& cn) {
    if (!apis_loaded || !current_domain) return nullptr;

    std::string key = ns + "::" + cn;
    if (auto it = class_cache.find(key); it != class_cache.end())
        return it->second;

    size_t cnt = 0;
    auto* asms = domain_get_assemblies(current_domain, &cnt);
    if (!asms) return nullptr;

    for (size_t i = 0; i < cnt; i++) {
        if (!asms[i]) continue;
        auto* img = assembly_get_image(asms[i]);
        if (!img) continue;
        auto* k = class_from_name(img, ns.c_str(), cn.c_str());
        if (k) { class_cache[key] = k; return k; }
    }
    return nullptr;
}

Il2CppClass_API* IL2CPPRuntimeTool::findClass(const std::string& ns, const std::string& cn) {
    return ensureInitialized() ? findClassInternal(ns, cn) : nullptr;
}

const MethodInfo* IL2CPPRuntimeTool::findMethod(Il2CppClass_API* klass, const std::string& methodName, int paramCount) {
    if (!klass || !ensureInitialized()) return nullptr;

    const char* ns = class_get_namespace(klass);
    const char* cn = class_get_name(klass);
    std::string key = std::string(ns ? ns : "") + "::" + (cn ? cn : "") + "::" + methodName + "::" + std::to_string(paramCount);

    if (auto it = method_cache.find(key); it != method_cache.end())
        return it->second;

    auto* m = class_get_method_from_name(klass, methodName.c_str(), paramCount);
    if (m) method_cache[key] = m;
    return m;
}

void* IL2CPPRuntimeTool::extractMethodPointer(const MethodInfo* method) {
    return method ? method->methodPointer : nullptr;
}

Il2CppObject_API* IL2CPPRuntimeTool::createInstance(Il2CppClass_API* klass) {
    return (klass && ensureInitialized()) ? object_new(klass) : nullptr;
}

Il2CppObject_API* IL2CPPRuntimeTool::invokeMethod(const MethodInfo* method, Il2CppObject_API* instance, void** args) {
    if (!method || !ensureInitialized()) return nullptr;
    Il2CppException* exc = nullptr;
    auto* r = runtime_invoke(method, instance, args, &exc);
    if (exc) { LOGE("Exception during invocation"); return nullptr; }
    return r;
}

// ===== String =====

Il2CppString_API* IL2CPPRuntimeTool::createString(const std::string& str) {
    return ensureInitialized() ? string_new(str.c_str()) : nullptr;
}

std::string IL2CPPRuntimeTool::getString(Il2CppString_API* str) {
    if (!str || !ensureInitialized()) return "";
    const auto* chars = reinterpret_cast<const char16_t*>(string_chars(str));
    if (!chars) return "";
    int32_t len = string_length(str);
    if (len <= 0) return "";

    std::string result;
    result.reserve(static_cast<size_t>(len) * 3);
    for (int32_t i = 0; i < len; i++) {
        char16_t ch = chars[i];
        if (ch < 0x80) {
            result.push_back(static_cast<char>(ch));
        } else if (ch < 0x800) {
            result.push_back(static_cast<char>(0xC0 | (ch >> 6)));
            result.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        } else {
            result.push_back(static_cast<char>(0xE0 | (ch >> 12)));
            result.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        }
    }
    return result;
}

// ===== Assembly & Class Enumeration =====

Il2CppImage* IL2CPPRuntimeTool::findAssembly(const std::string& name) {
    if (!ensureInitialized()) return nullptr;
    if (auto it = image_cache.find(name); it != image_cache.end())
        return it->second;

    size_t cnt = 0;
    auto* asms = domain_get_assemblies(current_domain, &cnt);
    if (!asms) return nullptr;

    for (size_t i = 0; i < cnt; i++) {
        if (!asms[i]) continue;
        auto* img = assembly_get_image(asms[i]);
        if (!img) continue;
        auto* n = image_get_name(img);
        if (n && name == n) { image_cache[name] = img; return img; }
    }
    return nullptr;
}

std::vector<Il2CppImage*> IL2CPPRuntimeTool::getAllAssemblies() {
    if (!ensureInitialized()) return {};
    size_t cnt = 0;
    auto* asms = domain_get_assemblies(current_domain, &cnt);
    if (!asms) return {};

    std::vector<Il2CppImage*> result;
    result.reserve(cnt);
    for (size_t i = 0; i < cnt; i++) {
        if (!asms[i]) continue;
        auto* img = assembly_get_image(asms[i]);
        if (img) result.push_back(img);
    }
    return result;
}

std::string IL2CPPRuntimeTool::getAssemblyName(Il2CppImage* image) {
    if (!image || !ensureInitialized()) return "";
    auto* n = image_get_name(image);
    return n ? n : "";
}

std::vector<Il2CppClass_API*> IL2CPPRuntimeTool::getAllClasses() {
    if (!ensureInitialized() || !image_get_class) return {};

    size_t cnt = 0;
    auto* asms = domain_get_assemblies(current_domain, &cnt);
    if (!asms) return {};

    std::vector<Il2CppClass_API*> classes;
    for (size_t i = 0; i < cnt; i++) {
        if (!asms[i]) continue;
        auto* img = assembly_get_image(asms[i]);
        if (!img) continue;
        auto cc = image_get_class_count(img);
        for (size_t j = 0; j < cc; j++) {
            auto* k = image_get_class(img, j);
            if (k) classes.push_back(k);
        }
    }
    return classes;
}

std::string IL2CPPRuntimeTool::getClassInfo(Il2CppClass_API* klass) {
    if (!klass || !ensureInitialized()) return "";
    const char* ns = class_get_namespace(klass);
    const char* cn = class_get_name(klass);
    std::string info = "Class: " + buildFullName(ns, cn);

    auto* parent = class_get_parent(klass);
    if (parent) {
        info += "\nParent: " + buildFullName(class_get_namespace(parent), class_get_name(parent));
    }
    return info;
}

bool IL2CPPRuntimeTool::isSubclassOf(Il2CppClass_API* klass, Il2CppClass_API* parentClass) {
    if (!klass || !parentClass || !ensureInitialized()) return false;
    return class_is_subclass_of(klass, parentClass, false);
}

const char* IL2CPPRuntimeTool::getClassName(Il2CppClass_API* klass) {
    return (klass && class_get_name) ? class_get_name(klass) : nullptr;
}

const char* IL2CPPRuntimeTool::getClassNamespace(Il2CppClass_API* klass) {
    return (klass && class_get_namespace) ? class_get_namespace(klass) : nullptr;
}

bool IL2CPPRuntimeTool::reattachThread() {
    if (!ensureInitialized() || !current_domain) return false;
    if (attached_thread) thread_detach(attached_thread);
    attached_thread = thread_attach(current_domain);
    return attached_thread != nullptr;
}

// ===== Array Support =====

Il2CppArray* IL2CPPRuntimeTool::createArray(Il2CppClass_API* elementClass, uint32_t length) {
    if (!elementClass || !ensureInitialized()) return nullptr;
    if (array_new) return array_new(elementClass, length);
    if (array_new_specific) return array_new_specific(elementClass, length);
    LOGE("createArray: no array creation API available");
    return nullptr;
}

// Helper to reduce repetition in typed array creation
static Il2CppArray* createTypedArray(IL2CPPRuntimeTool* tool, const char* ns, const char* cn, uint32_t length, const char* label) {
    auto* klass = tool->findClass(ns, cn);
    if (!klass) { LOGE("%s: %s.%s not found", label, ns, cn); return nullptr; }
    return tool->createArray(klass, length);
}

Il2CppArray* IL2CPPRuntimeTool::createInt32Array(uint32_t length) {
    return createTypedArray(this, "System", "Int32", length, "createInt32Array");
}
Il2CppArray* IL2CPPRuntimeTool::createFloatArray(uint32_t length) {
    return createTypedArray(this, "System", "Single", length, "createFloatArray");
}
Il2CppArray* IL2CPPRuntimeTool::createBoolArray(uint32_t length) {
    return createTypedArray(this, "System", "Boolean", length, "createBoolArray");
}
Il2CppArray* IL2CPPRuntimeTool::createStringArray(uint32_t length) {
    return createTypedArray(this, "System", "String", length, "createStringArray");
}

Il2CppArray* IL2CPPRuntimeTool::createArrayFromClass(const std::string& nameSpace, const std::string& className, uint32_t length) {
    auto* klass = findClass(nameSpace, className);
    if (!klass) {
        LOGE("createArrayFromClass: class not found [%s::%s]", nameSpace.c_str(), className.c_str());
        return nullptr;
    }
    return createArray(klass, length);
}

size_t IL2CPPRuntimeTool::getArrayLength(Il2CppArray* arr) {
    if (!arr) return 0;
    return array_length ? array_length(arr) : arr->max_length;
}

Il2CppClass_API* IL2CPPRuntimeTool::getArrayElementClass(Il2CppArray* arr) {
    return (arr && array_element_class) ? array_element_class(arr) : nullptr;
}

uint32_t IL2CPPRuntimeTool::getArrayRank(Il2CppArray* arr) {
    return (arr && array_rank) ? array_rank(arr) : 1;
}

void* IL2CPPRuntimeTool::getArrayDataPtr(Il2CppArray* arr) {
    return arr ? &arr->vector[0] : nullptr;
}

Il2CppObject_API* IL2CPPRuntimeTool::getArrayObjectElement(Il2CppArray* arr, uint32_t index) {
    return getArrayElement<Il2CppObject_API*>(arr, index);
}

void IL2CPPRuntimeTool::setArrayObjectElement(Il2CppArray* arr, uint32_t index, Il2CppObject_API* value) {
    setArrayElement(arr, index, value);
}

Il2CppString_API* IL2CPPRuntimeTool::getArrayStringElement(Il2CppArray* arr, uint32_t index) {
    return getArrayElement<Il2CppString_API*>(arr, index);
}

void IL2CPPRuntimeTool::setArrayStringElement(Il2CppArray* arr, uint32_t index, Il2CppString_API* value) {
    setArrayElement(arr, index, value);
}

void IL2CPPRuntimeTool::setArrayElements(Il2CppArray* arr, uint32_t startIndex,
                                         const void* values, uint32_t count, size_t elemSize) {
    if (!arr || !values || count == 0) return;

    size_t arrLen = getArrayLength(arr);
    if (startIndex + count > arrLen) {
        LOGW("setArrayElements: out of bounds (start=%u, count=%u, len=%zu)", startIndex, count, arrLen);
        return;
    }

    void* dataPtr = getArrayDataPtr(arr);
    if (dataPtr) {
        memcpy(static_cast<char*>(dataPtr) + startIndex * elemSize, values, count * elemSize);
    } else if (array_set) {
        for (uint32_t i = 0; i < count; i++)
            array_set(arr, startIndex + i, const_cast<char*>(static_cast<const char*>(values)) + i * elemSize);
    }
}

std::vector<Il2CppObject_API*> IL2CPPRuntimeTool::getAllArrayObjectElements(Il2CppArray* arr) {
    if (!arr) return {};
    size_t len = getArrayLength(arr);
    std::vector<Il2CppObject_API*> result;
    result.reserve(len);
    for (size_t i = 0; i < len; i++) {
        auto* elem = getArrayObjectElement(arr, static_cast<uint32_t>(i));
        if (elem && isObjectValid(elem, nullptr))
            result.push_back(elem);
    }
    return result;
}

// ===== Dump Implementation (Perfare/Il2CppDumper style) =====

bool IL2CPPRuntimeTool::typeIsByrefSafe(const Il2CppType* type) {
    if (!type) return false;
    return type_is_byref ? type_is_byref(type) : type->byref;
}

std::string IL2CPPRuntimeTool::getMethodModifier(uint32_t flags) {
    std::string out;

    switch (flags & METHOD_ATTRIBUTE_MEMBER_ACCESS_MASK) {
        case METHOD_ATTRIBUTE_PRIVATE:       out += "private "; break;
        case METHOD_ATTRIBUTE_PUBLIC:        out += "public "; break;
        case METHOD_ATTRIBUTE_FAMILY:        out += "protected "; break;
        case METHOD_ATTRIBUTE_ASSEM:
        case METHOD_ATTRIBUTE_FAM_AND_ASSEM: out += "internal "; break;
        case METHOD_ATTRIBUTE_FAM_OR_ASSEM:  out += "protected internal "; break;
    }

    if (flags & METHOD_ATTRIBUTE_STATIC) out += "static ";

    bool isReuse = (flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_REUSE_SLOT;
    bool isNew   = (flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_NEW_SLOT;

    if (flags & METHOD_ATTRIBUTE_ABSTRACT) {
        out += "abstract ";
        if (isReuse) out += "override ";
    } else if (flags & METHOD_ATTRIBUTE_FINAL) {
        if (isReuse) out += "sealed override ";
    } else if (flags & METHOD_ATTRIBUTE_VIRTUAL) {
        out += isNew ? "virtual " : "override ";
    }

    if (flags & METHOD_ATTRIBUTE_PINVOKE_IMPL) out += "extern ";
    return out;
}

// Build fully qualified class name including nested type chain
// e.g. Dictionary`2 -> "System.Collections.Generic.Dictionary"
//      Enumerator (nested in Dictionary`2) -> "System.Collections.Generic.Dictionary.Enumerator"
std::string IL2CPPRuntimeTool::getClassFullName(Il2CppClass_API* klass) {
    if (!klass) return "???";

    const char* name = class_get_name(klass);
    if (!name) return "???";

    // Check if this is a nested type by looking for a declaring type
    if (class_get_declaring_type) {
        auto* declaring = class_get_declaring_type(klass);
        if (declaring) {
            // Recursively get the declaring type's full name, then append ".NestedName"
            std::string parentName = getClassFullName(declaring);
            // Strip backtick from the nested class name too
            std::string thisName = name;
            auto pos = thisName.find('`');
            if (pos != std::string::npos) thisName = thisName.substr(0, pos);
            return parentName + "." + thisName;
        }
    }

    // Not nested: use namespace + name (strip backtick for generic definitions)
    std::string baseName = name;
    auto pos = baseName.find('`');
    if (pos != std::string::npos) baseName = baseName.substr(0, pos);

    return buildFullName(class_get_namespace(klass), baseName.c_str());
}

std::string IL2CPPRuntimeTool::getFullTypeName(const Il2CppType* type) {
    if (!type) return "???";

    unsigned int typeEnum = type->type;

    // --- Handle special composite types BEFORE calling class_from_type ---

    // GENERICINST: e.g. Dictionary<uint, Transform>, List<int>
    if (typeEnum == IL2CPP_TYPE_GENERICINST) {
        auto* genericClass = type->data.generic_class;
        if (!genericClass || !genericClass->type) return "???";

        // Get the generic definition class
        auto* defKlass = class_from_type(genericClass->type);
        if (!defKlass) return "???";

        // Get full name including nested type chain
        std::string fullName = getClassFullName(defKlass);

        // Append generic arguments: <T1,T2,...>
        auto* classInst = genericClass->context.class_inst;
        if (classInst && classInst->type_argc > 0 && classInst->type_argv) {
            fullName += "<";
            for (uint32_t i = 0; i < classInst->type_argc; ++i) {
                if (i > 0) fullName += ",";
                fullName += getFullTypeName(classInst->type_argv[i]);
            }
            fullName += ">";
        }
        return fullName;
    }

    // SZARRAY: single-dimension zero-based array, e.g. int[], string[]
    if (typeEnum == IL2CPP_TYPE_SZARRAY) {
        // data.type points to the element type
        auto* elemType = type->data.type;
        if (!elemType) return "???[]";
        return getFullTypeName(elemType) + "[]";
    }

    // ARRAY: multi-dimensional array, e.g. int[,]
    if (typeEnum == IL2CPP_TYPE_ARRAY) {
        auto* arrType = type->data.array;
        if (!arrType || !arrType->etype) return "???[,]";
        std::string result = getFullTypeName(arrType->etype) + "[";
        for (uint8_t i = 1; i < arrType->rank; ++i) result += ",";
        result += "]";
        return result;
    }

    // PTR: pointer type, e.g. void*
    if (typeEnum == IL2CPP_TYPE_PTR) {
        auto* pointedType = type->data.type;
        if (!pointedType) return "???*";
        return getFullTypeName(pointedType) + "*";
    }

    // BYREF: ref type (handled separately in dump, but just in case)
    if (typeEnum == IL2CPP_TYPE_BYREF) {
        auto* refType = type->data.type;
        if (!refType) return "???&";
        return getFullTypeName(refType) + "&";
    }

    // VAR / MVAR: generic type parameters (T, TKey, etc.)
    if (typeEnum == IL2CPP_TYPE_VAR || typeEnum == IL2CPP_TYPE_MVAR) {
        // Try to get the name via class_from_type (works in most Unity versions)
        auto* klass = class_from_type(type);
        if (klass) {
            const char* name = class_get_name(klass);
            if (name && name[0] != '\0') return name;
        }
        // Fallback: unnamed generic parameter
        return (typeEnum == IL2CPP_TYPE_VAR) ? "T" : "TMethod";
    }

    // --- Standard types: resolve via class_from_type ---
    auto* klass = class_from_type(type);
    if (!klass) return "???";

    // Use getClassFullName to handle nested types properly
    return getClassFullName(klass);
}

std::string IL2CPPRuntimeTool::dumpMethods(Il2CppClass_API* klass) {
    std::stringstream out;
    out << "\n\t// Methods\n";

    void* iter = nullptr;
    while (auto* method = class_get_methods(klass, &iter)) {
        out << "\t";

        uint32_t iflags = 0;
        auto flags = method_get_flags(method, &iflags);
        out << getMethodModifier(flags);

        // Return type
        auto* retType = method_get_return_type(method);
        if (typeIsByrefSafe(retType)) out << "ref ";
        out << getFullTypeName(retType);

        out << " " << method_get_name(method) << "(";

        // Parameters
        int paramCount = method_get_param_count(method);
        for (int i = 0; i < paramCount; ++i) {
            auto* param = method_get_param(method, i);
            if (param) {
                auto attrs = param->attrs;
                if (typeIsByrefSafe(param)) {
                    if ((attrs & PARAM_ATTRIBUTE_OUT) && !(attrs & PARAM_ATTRIBUTE_IN))
                        out << "out ";
                    else if ((attrs & PARAM_ATTRIBUTE_IN) && !(attrs & PARAM_ATTRIBUTE_OUT))
                        out << "in ";
                    else
                        out << "ref ";
                    out << getFullTypeName(param) << "&";
                } else {
                    if (attrs & PARAM_ATTRIBUTE_IN)  out << "[In] ";
                    if (attrs & PARAM_ATTRIBUTE_OUT) out << "[Out] ";
                    out << getFullTypeName(param);
                }
            } else {
                out << "???";
            }
            out << " " << method_get_param_name(method, i);
            if (i < paramCount - 1) out << ", ";
        }
        out << ");";

        // RVA
        if (method->methodPointer) {
            out << " // 0x" << std::hex << (reinterpret_cast<uint64_t>(method->methodPointer) - il2cpp_base);
        } else {
            out << " // 0x0";
        }
        out << "\n";
    }
    return out.str();
}

std::string IL2CPPRuntimeTool::dumpFields(Il2CppClass_API* klass) {
    std::stringstream out;
    out << "\n\t// Fields\n";

    bool isEnum = class_is_enum(klass);
    void* iter = nullptr;
    while (auto* field = class_get_fields(klass, &iter)) {
        out << "\t";
        auto attrs = field_get_flags(field);

        switch (attrs & FIELD_ATTRIBUTE_FIELD_ACCESS_MASK) {
            case FIELD_ATTRIBUTE_PRIVATE:       out << "private "; break;
            case FIELD_ATTRIBUTE_PUBLIC:        out << "public "; break;
            case FIELD_ATTRIBUTE_FAMILY:        out << "protected "; break;
            case FIELD_ATTRIBUTE_ASSEMBLY:
            case FIELD_ATTRIBUTE_FAM_AND_ASSEM: out << "internal "; break;
            case FIELD_ATTRIBUTE_FAM_OR_ASSEM:  out << "protected internal "; break;
        }

        if (attrs & FIELD_ATTRIBUTE_LITERAL) {
            out << "const ";
        } else {
            if (attrs & FIELD_ATTRIBUTE_STATIC)    out << "static ";
            if (attrs & FIELD_ATTRIBUTE_INIT_ONLY) out << "readonly ";
        }

        out << getFullTypeName(field_get_type(field)) << " " << field_get_name(field);

        if ((attrs & FIELD_ATTRIBUTE_LITERAL) && isEnum) {
            uint64_t val = 0;
            field_static_get_value(field, &val);
            out << " = " << std::dec << val;
        }
        out << "; // 0x" << std::hex << field_get_offset(field) << "\n";
    }
    return out.str();
}

std::string IL2CPPRuntimeTool::dumpProperties(Il2CppClass_API* klass) {
    if (!class_get_properties || !property_get_name) return "";

    std::stringstream out;
    out << "\n\t// Properties\n";

    void* iter = nullptr;
    while (auto* prop = class_get_properties(klass, &iter)) {
        auto* get = property_get_get_method ? property_get_get_method(prop) : nullptr;
        auto* set = property_get_set_method ? property_get_set_method(prop) : nullptr;
        auto* propName = property_get_name(prop);

        out << "\t";
        std::string typeName;
        uint32_t iflags = 0;

        if (get) {
            out << getMethodModifier(method_get_flags(get, &iflags));
            typeName = getFullTypeName(method_get_return_type(get));
        } else if (set) {
            out << getMethodModifier(method_get_flags(set, &iflags));
            auto* param = method_get_param(set, 0);
            if (param) typeName = getFullTypeName(param);
        }

        if (!typeName.empty() && propName) {
            out << typeName << " " << propName << " { ";
            if (get) out << "get; ";
            if (set) out << "set; ";
            out << "}\n";
        } else if (propName) {
            out << " // unknown property " << propName << "\n";
        }
    }
    return out.str();
}

std::string IL2CPPRuntimeTool::dumpTypeStr(const Il2CppType* type) {
    auto* klass = class_from_type(type);
    const char* ns = class_get_namespace(klass);
    const char* name = class_get_name(klass);

    std::stringstream out;
    out << "\n// Namespace: " << (ns ? ns : "") << "\n";

    auto flags = class_get_flags(klass);
    if (flags & TYPE_ATTRIBUTE_SERIALIZABLE) out << "[Serializable]\n";

    bool isValue = class_is_valuetype(klass);
    bool isEnum  = class_is_enum(klass);

    // Visibility
    switch (flags & TYPE_ATTRIBUTE_VISIBILITY_MASK) {
        case TYPE_ATTRIBUTE_PUBLIC:
        case TYPE_ATTRIBUTE_NESTED_PUBLIC:          out << "public "; break;
        case TYPE_ATTRIBUTE_NOT_PUBLIC:
        case TYPE_ATTRIBUTE_NESTED_FAM_AND_ASSEM:
        case TYPE_ATTRIBUTE_NESTED_ASSEMBLY:        out << "internal "; break;
        case TYPE_ATTRIBUTE_NESTED_PRIVATE:         out << "private "; break;
        case TYPE_ATTRIBUTE_NESTED_FAMILY:          out << "protected "; break;
        case TYPE_ATTRIBUTE_NESTED_FAM_OR_ASSEM:    out << "protected internal "; break;
    }

    // Class modifiers
    if ((flags & TYPE_ATTRIBUTE_ABSTRACT) && (flags & TYPE_ATTRIBUTE_SEALED))
        out << "static ";
    else if (!(flags & TYPE_ATTRIBUTE_INTERFACE) && (flags & TYPE_ATTRIBUTE_ABSTRACT))
        out << "abstract ";
    else if (!isValue && !isEnum && (flags & TYPE_ATTRIBUTE_SEALED))
        out << "sealed ";

    // Type keyword
    if (flags & TYPE_ATTRIBUTE_INTERFACE) out << "interface ";
    else if (isEnum) out << "enum ";
    else if (isValue) out << "struct ";
    else out << "class ";

    // Fully qualified name
    out << buildFullName(ns, name);

    // Inheritance — use getFullTypeName to resolve generics
    std::vector<std::string> extends;
    auto* parent = class_get_parent(klass);
    if (!isValue && !isEnum && parent) {
        auto* parentType = class_get_type(parent);
        if (parentType) {
            extends.push_back(getFullTypeName(parentType));
        }
    }

    void* iter = nullptr;
    while (auto* itf = class_get_interfaces(klass, &iter)) {
        auto* itfType = class_get_type(itf);
        if (itfType)
            extends.push_back(getFullTypeName(itfType));
        else
            extends.push_back(buildFullName(class_get_namespace(itf), class_get_name(itf)));
    }

    if (!extends.empty()) {
        out << " : " << extends[0];
        for (size_t i = 1; i < extends.size(); ++i) out << ", " << extends[i];
    }

    out << "\n";
    out << dumpFields(klass);
    out << dumpProperties(klass);
    out << dumpMethods(klass);
    out << "\n";
    return out.str();
}

bool IL2CPPRuntimeTool::dumpGameData(const std::string& filePath) {
    if (!ensureInitialized()) {
        LOGE("dumpGameData: not initialized");
        return false;
    }

    LOGI("dumpGameData: start -> %s", filePath.c_str());

    size_t size = 0;
    auto* assemblies = domain_get_assemblies(current_domain, &size);
    if (!assemblies || size == 0) {
        LOGE("dumpGameData: no assemblies");
        return false;
    }

    if (!image_get_class) {
        LOGE("dumpGameData: il2cpp_image_get_class not available");
        return false;
    }

    LOGI("dumpGameData: %zu assemblies", size);

    // Phase 1: Image index
    std::stringstream imageOutput;
    for (size_t i = 0; i < size; ++i) {
        auto* image = assembly_get_image(assemblies[i]);
        if (image) imageOutput << "// Image " << i << ": " << image_get_name(image) << "\n";
    }

    // Phase 2: Dump all classes
    std::vector<std::string> outputs;
    for (size_t i = 0; i < size; ++i) {
        auto* image = assembly_get_image(assemblies[i]);
        if (!image) continue;

        const char* imgName = image_get_name(image);
        LOGI("dumpGameData: [%zu/%zu] %s", i, size, imgName ? imgName : "?");

        std::string imagePrefix = std::string("\n// ") + (imgName ? imgName : "?");
        auto classCount = image_get_class_count(image);

        for (size_t j = 0; j < classCount; ++j) {
            auto* klass = image_get_class(image, j);
            if (!klass) continue;
            auto* type = class_get_type(const_cast<Il2CppClass_API*>(klass));
            if (!type) continue;
            try {
                outputs.push_back(imagePrefix + dumpTypeStr(type));
            } catch (...) {
                LOGW("dumpGameData: exception at class[%zu] in %s", j, imgName ? imgName : "?");
            }
        }
    }

    // Phase 3: Write to file
    LOGI("dumpGameData: writing %zu classes", outputs.size());
    std::ofstream outStream(filePath);
    if (!outStream.is_open()) {
        LOGE("dumpGameData: cannot open %s", filePath.c_str());
        return false;
    }
    outStream << imageOutput.str();
    for (auto& s : outputs) outStream << s;
    outStream.close();

    LOGI("dumpGameData: done! %zu classes dumped", outputs.size());
    return true;
}