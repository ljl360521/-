#include "游戏函数调用.h"
#include "全局状态.h"
#include "xdl.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <chrono>
#include <cmath>

#ifndef RTLD_NOLOAD
#define RTLD_NOLOAD 0
#endif

namespace {

using Il2CppDomain = void;
using Il2CppAssembly = void;
using Il2CppImage = void;
using Il2CppClass = void;
using Il2CppMethod = void;
using Il2CppObject = void;
using Il2CppException = void;
using Il2CppField = void;
using Il2CppThread = void;

using il2cpp_domain_get_t = Il2CppDomain* (*)();
using il2cpp_domain_get_assemblies_t = const Il2CppAssembly** (*)(const Il2CppDomain*, size_t*);
using il2cpp_assembly_get_image_t = const Il2CppImage* (*)(const Il2CppAssembly*);
using il2cpp_image_get_name_t = const char* (*)(const Il2CppImage*);
using il2cpp_class_from_name_t = Il2CppClass* (*)(const Il2CppImage*, const char*, const char*);
using il2cpp_class_get_method_from_name_t = const Il2CppMethod* (*)(Il2CppClass*, const char*, int);
using il2cpp_class_get_field_from_name_t = Il2CppField* (*)(Il2CppClass*, const char*);
using il2cpp_class_get_fields_t = Il2CppField* (*)(Il2CppClass*, void**);
using il2cpp_field_get_name_t = const char* (*)(Il2CppField*);
using il2cpp_field_static_get_value_t = void (*)(Il2CppField*, void*);
using il2cpp_field_static_set_value_t = void (*)(Il2CppField*, void*);
using il2cpp_field_get_value_t = void (*)(Il2CppField*, void*, void*);
using il2cpp_field_get_offset_t = int32_t (*)(Il2CppField*);
using il2cpp_runtime_invoke_t = Il2CppObject* (*)(const Il2CppMethod*, void*, void**, Il2CppException**);
using il2cpp_thread_attach_t = Il2CppThread* (*)(Il2CppDomain*);
using il2cpp_thread_current_t = Il2CppThread* (*)();
using il2cpp_string_new_t = Il2CppObject* (*)(const char*);
using il2cpp_object_get_class_t = Il2CppClass* (*)(Il2CppObject*);
using il2cpp_array_new_t = Il2CppObject* (*)(Il2CppClass*, int32_t);
using il2cpp_array_get_t = Il2CppObject* (*)(Il2CppObject*, int32_t);
using il2cpp_array_length_t = int32_t (*)(Il2CppObject*);

struct Il2CppApi {
    void* dl_handle = nullptr;      // dlopen 句柄，仅用于 dlsym
    void* xdl_handle = nullptr;     // xDL 句柄，仅用于 xdl_sym/xdl_dsym
    il2cpp_domain_get_t domain_get = nullptr;
    il2cpp_domain_get_assemblies_t domain_get_assemblies = nullptr;
    il2cpp_assembly_get_image_t assembly_get_image = nullptr;
    il2cpp_image_get_name_t image_get_name = nullptr;
    il2cpp_class_from_name_t class_from_name = nullptr;
    il2cpp_class_get_method_from_name_t class_get_method_from_name = nullptr;
    il2cpp_class_get_field_from_name_t class_get_field_from_name = nullptr;
    il2cpp_class_get_fields_t class_get_fields = nullptr;
    il2cpp_field_get_name_t field_get_name = nullptr;
    il2cpp_field_static_get_value_t field_static_get_value = nullptr;
    il2cpp_field_static_set_value_t field_static_set_value = nullptr;
    il2cpp_field_get_value_t field_get_value = nullptr;
    il2cpp_field_get_offset_t field_get_offset = nullptr;
    il2cpp_runtime_invoke_t runtime_invoke = nullptr;
    il2cpp_thread_attach_t thread_attach = nullptr;
    il2cpp_thread_current_t thread_current = nullptr;
    il2cpp_string_new_t string_new = nullptr;
    il2cpp_object_get_class_t object_get_class = nullptr;
    il2cpp_array_new_t array_new = nullptr;
    il2cpp_array_get_t array_get = nullptr;
    il2cpp_array_length_t array_length = nullptr;
};

struct UnityVector3 {
    float x;
    float y;
    float z;
};

static Il2CppApi g_api;
static std::mutex g_call_mutex;
static std::mutex g_status_mutex;
static std::string g_last_status = "未初始化";

static Il2CppClass* g_game_core_class = nullptr;
static const Il2CppMethod* g_get_instance_method = nullptr;
static const Il2CppMethod* g_send_devide_method = nullptr;

// GameCoreCenter.ViewScaleFactor（实例 float 字段，默认 0.5）
// 控制视野缩放：view.z = ... * ViewScaleFactor * curviewscale
static Il2CppField* g_viewscale_factor_field = nullptr;

// DrawCircle.ATime_SettingOffet（静态 float 字段，默认 0）
// ATimeFen1 = 0.588 + ATime_SettingOffet，值越大粘合越快
static Il2CppClass* g_drawcircle_class = nullptr;
static Il2CppField* g_atime_setting_offet_field = nullptr;

static const Il2CppMethod* g_move_method = nullptr;
static const Il2CppMethod* g_force_send_move_method = nullptr;
static const Il2CppMethod* g_set_free_type_flag_method = nullptr;
static const Il2CppMethod* g_free_type_click_method = nullptr;

// NetworkUpdater 类（直接调用 ReqFreeType 绕过所有检查）
static Il2CppClass* g_network_updater_class = nullptr;
static const Il2CppMethod* g_nu_get_instance_method = nullptr;
static const Il2CppMethod* g_nu_req_free_type_method = nullptr;

// GameCoreCenter.JostickDir 实例字段（读取当前摇杆方向）
static Il2CppField* g_jostick_dir_field = nullptr;
static UnityVector3 g_last_move_dir{0.0f, 0.0f, 0.0f};
static std::string g_found_image_name;
static size_t g_last_assembly_count = 0;
static void* g_last_instance = nullptr;
static void* g_last_thread = nullptr;

static std::atomic<int> g_button_click_count{0};
static std::atomic<int> g_move_call_count{0};
static std::atomic<int> g_success_count{0};
static std::atomic<int> g_fail_count{0};
static std::atomic<int> g_attach_count{0};

void SetMsg(std::string& out, const char* fmt, ...) {
    char buf[768];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    out = buf;
}

void SetStatus(const std::string& s) {
    std::lock_guard<std::mutex> lock(g_status_mutex);
    g_last_status = s;
}

void* ResolveSym(const char* name) {
    void* p = nullptr;
    if (g_api.dl_handle) {
        p = dlsym(g_api.dl_handle, name);
    }
    if (!p && g_api.xdl_handle) {
        size_t sz = 0;
        p = xdl_sym(g_api.xdl_handle, name, &sz);
        if (!p) p = xdl_dsym(g_api.xdl_handle, name, &sz);
    }
    return p;
}

template <typename T>
T ResolveTypedSym(const char* name) {
    return reinterpret_cast<T>(ResolveSym(name));
}

void AppendMissing(char* miss, size_t missSize, const char* name) {
    if (!miss || missSize == 0 || !name) return;
    size_t used = strlen(miss);
    if (used + 1 >= missSize) return;
    if (used > 0) {
        strncat(miss, ",", missSize - strlen(miss) - 1);
    }
    strncat(miss, name, missSize - strlen(miss) - 1);
}

bool EnsureIl2CppApi(std::string& out) {
    static bool api_logged = false;
    if (!g_api.dl_handle) {
        dlerror();
        if (RTLD_NOLOAD != 0) {
            g_api.dl_handle = dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD);
        }
        if (!g_api.dl_handle) {
            dlerror();
            g_api.dl_handle = dlopen("libil2cpp.so", RTLD_NOW);
        }
        if (!g_api.dl_handle) {
            dlerror();
            g_api.dl_handle = dlopen("libil2cpp.so", RTLD_LAZY);
        }
    }

    if (!g_api.xdl_handle) {
        g_api.xdl_handle = xdl_open("libil2cpp.so", XDL_DEFAULT);
    }

    if (!g_api.dl_handle && !g_api.xdl_handle) {
        SetMsg(out, "打开 libil2cpp.so 失败：游戏 IL2CPP 还未加载或 linker namespace 不可见");
        return false;
    }

    if (!g_api.domain_get) g_api.domain_get = ResolveTypedSym<il2cpp_domain_get_t>("il2cpp_domain_get");
    if (!g_api.domain_get_assemblies) g_api.domain_get_assemblies = ResolveTypedSym<il2cpp_domain_get_assemblies_t>("il2cpp_domain_get_assemblies");
    if (!g_api.assembly_get_image) g_api.assembly_get_image = ResolveTypedSym<il2cpp_assembly_get_image_t>("il2cpp_assembly_get_image");
    if (!g_api.image_get_name) g_api.image_get_name = ResolveTypedSym<il2cpp_image_get_name_t>("il2cpp_image_get_name");
    if (!g_api.class_from_name) g_api.class_from_name = ResolveTypedSym<il2cpp_class_from_name_t>("il2cpp_class_from_name");
    if (!g_api.class_get_method_from_name) g_api.class_get_method_from_name = ResolveTypedSym<il2cpp_class_get_method_from_name_t>("il2cpp_class_get_method_from_name");
    if (!g_api.class_get_field_from_name) g_api.class_get_field_from_name = ResolveTypedSym<il2cpp_class_get_field_from_name_t>("il2cpp_class_get_field_from_name");
    if (!g_api.class_get_fields) g_api.class_get_fields = ResolveTypedSym<il2cpp_class_get_fields_t>("il2cpp_class_get_fields");
    if (!g_api.field_get_name) g_api.field_get_name = ResolveTypedSym<il2cpp_field_get_name_t>("il2cpp_field_get_name");
    if (!g_api.field_static_get_value) g_api.field_static_get_value = ResolveTypedSym<il2cpp_field_static_get_value_t>("il2cpp_field_static_get_value");
    if (!g_api.field_static_set_value) g_api.field_static_set_value = ResolveTypedSym<il2cpp_field_static_set_value_t>("il2cpp_field_static_set_value");
    if (!g_api.field_get_value) g_api.field_get_value = ResolveTypedSym<il2cpp_field_get_value_t>("il2cpp_field_get_value");
    if (!g_api.field_get_offset) g_api.field_get_offset = ResolveTypedSym<il2cpp_field_get_offset_t>("il2cpp_field_get_offset");
    if (!g_api.runtime_invoke) g_api.runtime_invoke = ResolveTypedSym<il2cpp_runtime_invoke_t>("il2cpp_runtime_invoke");
    if (!g_api.thread_attach) g_api.thread_attach = ResolveTypedSym<il2cpp_thread_attach_t>("il2cpp_thread_attach");
    if (!g_api.thread_current) g_api.thread_current = ResolveTypedSym<il2cpp_thread_current_t>("il2cpp_thread_current");
    if (!g_api.string_new) g_api.string_new = ResolveTypedSym<il2cpp_string_new_t>("il2cpp_string_new");
    if (!g_api.object_get_class) g_api.object_get_class = ResolveTypedSym<il2cpp_object_get_class_t>("il2cpp_object_get_class");
    if (!g_api.array_new) g_api.array_new = ResolveTypedSym<il2cpp_array_new_t>("il2cpp_array_new");
    if (!g_api.array_get) g_api.array_get = ResolveTypedSym<il2cpp_array_get_t>("il2cpp_array_get");
    if (!g_api.array_length) g_api.array_length = ResolveTypedSym<il2cpp_array_length_t>("il2cpp_array_length");

    char miss[512] = {0};
    if (!g_api.domain_get) AppendMissing(miss, sizeof(miss), "il2cpp_domain_get");
    if (!g_api.domain_get_assemblies) AppendMissing(miss, sizeof(miss), "il2cpp_domain_get_assemblies");
    if (!g_api.assembly_get_image) AppendMissing(miss, sizeof(miss), "il2cpp_assembly_get_image");
    if (!g_api.class_from_name) AppendMissing(miss, sizeof(miss), "il2cpp_class_from_name");
    if (!g_api.class_get_method_from_name) AppendMissing(miss, sizeof(miss), "il2cpp_class_get_method_from_name");
    if (!g_api.runtime_invoke) AppendMissing(miss, sizeof(miss), "il2cpp_runtime_invoke");

    if (miss[0]) {
        SetMsg(out, "libil2cpp API 符号不完整，缺失：%s", miss);
        return false;
    }
    api_logged = true;
    return true;
}

Il2CppDomain* GetDomain(std::string& out) {
    Il2CppDomain* domain = g_api.domain_get ? g_api.domain_get() : nullptr;
    if (!domain) {
        SetMsg(out, "il2cpp_domain_get 返回空，游戏脚本域尚未初始化");
    }
    return domain;
}

void AttachCurrentThreadIfNeeded(Il2CppDomain* domain) {
    if (!domain || !g_api.thread_attach) return;
    Il2CppThread* cur = g_api.thread_current ? g_api.thread_current() : nullptr;
    if (!cur) {
        cur = g_api.thread_attach(domain);
        if (cur) g_attach_count++;
    }
    g_last_thread = cur;
}

Il2CppClass* FindClassEverywhere(const char* namespaze, const char* klass, std::string& out) {
    Il2CppDomain* domain = GetDomain(out);
    if (!domain) return nullptr;

    size_t count = 0;
    const Il2CppAssembly** assemblies = g_api.domain_get_assemblies(domain, &count);
    g_last_assembly_count = count;
    if (!assemblies || count == 0) {
        SetMsg(out, "il2cpp_domain_get_assemblies 返回空，请等游戏进入主界面/对局后重试");
        return nullptr;
    }

    for (size_t i = 0; i < count; ++i) {
        const Il2CppImage* img = g_api.assembly_get_image(assemblies[i]);
        if (!img) continue;
        Il2CppClass* cls = g_api.class_from_name(img, namespaze ? namespaze : "", klass);
        if (cls) {
            const char* imageName = g_api.image_get_name ? g_api.image_get_name(img) : nullptr;
            g_found_image_name = imageName ? imageName : "unknown";
            return cls;
        }
    }

    SetMsg(out, "未找到类 %s%s%s（已扫描 %zu 个程序集）",
           namespaze && namespaze[0] ? namespaze : "",
           namespaze && namespaze[0] ? "." : "",
           klass ? klass : "", count);
    return nullptr;
}

bool RuntimeInvoke(const Il2CppMethod* method, void* obj, void** params, Il2CppObject** result, const char* label, std::string& out) {
    if (!method) {
        SetMsg(out, "%s 方法为空", label ? label : "runtime_invoke");
        return false;
    }
    Il2CppException* exc = nullptr;
    Il2CppObject* ret = g_api.runtime_invoke(method, obj, params, &exc);
    if (result) *result = ret;
    if (exc) {
        SetMsg(out, "%s 抛出 IL2CPP 异常 exc=%p", label ? label : "runtime_invoke", exc);
        return false;
    }
    return true;
}

bool ResolveGameCoreBase(std::string& out) {
    if (!g_game_core_class) {
        g_game_core_class = FindClassEverywhere("", "GameCoreCenter", out);
        if (!g_game_core_class) return false;
    }

    if (!g_get_instance_method) {
        g_get_instance_method = g_api.class_get_method_from_name(g_game_core_class, "get_instance", 0);
        // 属性 getter 被裁剪或混淆时下面还有字段 fallback，所以这里不直接失败。
    }
    return true;
}

bool ResolveGameCoreMembers(std::string& out) {
    if (!ResolveGameCoreBase(out)) return false;

    if (!g_send_devide_method) {
        g_send_devide_method = g_api.class_get_method_from_name(g_game_core_class, "SendDevide", 0);
    }
    if (!g_send_devide_method) {
        SetMsg(out, "未找到 GameCoreCenter.SendDevide()，请确认当前游戏版本与反编译源码一致");
        return false;
    }
    // 查找 JostickDir 实例字段（用于读取当前摇杆方向）
    if (!g_jostick_dir_field && g_api.class_get_field_from_name) {
        g_jostick_dir_field = g_api.class_get_field_from_name(g_game_core_class, "JostickDir");
        if (!g_jostick_dir_field) {
            g_jostick_dir_field = g_api.class_get_field_from_name(g_game_core_class, "_JostickDir");
        }
    }
    // 查找 ViewScaleFactor 实例字段（用于修改视野大小，默认 0.5）
    if (!g_viewscale_factor_field && g_api.class_get_field_from_name) {
        g_viewscale_factor_field = g_api.class_get_field_from_name(g_game_core_class, "ViewScaleFactor");
    }
    return true;
}

bool ResolveMoveMembers(std::string& out) {
    if (!ResolveGameCoreBase(out)) return false;

    if (!g_move_method) {
        g_move_method = g_api.class_get_method_from_name(g_game_core_class, "Move", 1);
    }
    if (!g_force_send_move_method) {
        g_force_send_move_method = g_api.class_get_method_from_name(g_game_core_class, "ForceSendMove", 0);
    }
    if (!g_move_method) {
        SetMsg(out, "未找到 GameCoreCenter.Move(Vector3)，请确认当前游戏版本与反编译源码一致");
        return false;
    }
    return true;
}

bool ResolveSpitBallMembers(std::string& out) {
    if (!ResolveGameCoreBase(out)) return false;

    if (!g_set_free_type_flag_method) {
        // SetFreeTypeFlag(int flag)：flag=0 分身方向，flag=1 吐球方向
        g_set_free_type_flag_method = g_api.class_get_method_from_name(g_game_core_class, "SetFreeTypeFlag", 1);
    }
    if (!g_free_type_click_method) {
        // FreeTypeClick(bool force)：force=true 跳过 DisableFeedControl 检查
        // C# 默认参数在 IL2CPP 中编译为带 [Optional] 标记的 1 参数方法，参数个数仍为 1
        g_free_type_click_method = g_api.class_get_method_from_name(g_game_core_class, "FreeTypeClick", 1);
        // 兜底：部分版本可能没有 force 参数（0 参数重载）
        if (!g_free_type_click_method) {
            g_free_type_click_method = g_api.class_get_method_from_name(g_game_core_class, "FreeTypeClick", 0);
        }
    }
    if (!g_set_free_type_flag_method) {
        SetMsg(out, "未找到 GameCoreCenter.SetFreeTypeFlag(int)，请确认当前游戏版本与反编译源码一致");
        return false;
    }
    if (!g_free_type_click_method) {
        SetMsg(out, "未找到 GameCoreCenter.FreeTypeClick(bool)，请确认当前游戏版本与反编译源码一致");
        return false;
    }
    return true;
}

void* GetGameCoreInstance(std::string& out) {
    // 正确入口是源码中的 GameCoreCenter.instance；优先通过 get_instance 属性 getter 取单例。
    if (g_get_instance_method) {
        Il2CppObject* ret = nullptr;
        if (!RuntimeInvoke(g_get_instance_method, nullptr, nullptr, &ret, "GameCoreCenter.get_instance()", out)) {
            return nullptr;
        }
        if (ret) {
            g_last_instance = ret;
            return ret;
        }
    }

    // 兜底：静态字段。不同 IL2CPP 版本/反编译器可能显示为 instance 或 backing field。
    if (g_api.class_get_field_from_name && g_api.field_static_get_value) {
        const char* fieldNames[] = {"instance", "<instance>k__BackingField", nullptr};
        for (int i = 0; fieldNames[i]; ++i) {
            Il2CppField* f = g_api.class_get_field_from_name(g_game_core_class, fieldNames[i]);
            if (!f) continue;
            void* v = nullptr;
            g_api.field_static_get_value(f, &v);
            if (v) {
                g_last_instance = v;
                return v;
            }
        }
    }

    SetMsg(out, "GameCoreCenter.instance 为空：请进入对局后再点，或当前场景尚未创建 GameCoreCenter");
    return nullptr;
}

bool ResolveNetworkUpdaterMembers(std::string& out) {
    if (!EnsureIl2CppApi(out)) return false;

    if (!g_network_updater_class) {
        g_network_updater_class = FindClassEverywhere("", "NetworkUpdater", out);
        if (!g_network_updater_class) return false;
    }

    if (!g_nu_get_instance_method) {
        // instance 是 static property，getter 名为 get_instance
        g_nu_get_instance_method = g_api.class_get_method_from_name(g_network_updater_class, "get_instance", 0);
    }
    if (!g_nu_req_free_type_method) {
        // public bool ReqFreeType(int type, int flag)
        g_nu_req_free_type_method = g_api.class_get_method_from_name(g_network_updater_class, "ReqFreeType", 2);
    }
    if (!g_nu_get_instance_method) {
        SetMsg(out, "未找到 NetworkUpdater.get_instance()");
        return false;
    }
    if (!g_nu_req_free_type_method) {
        SetMsg(out, "未找到 NetworkUpdater.ReqFreeType(int, int)");
        return false;
    }
    return true;
}

void* GetNetworkUpdaterInstance(std::string& out) {
    if (!g_nu_get_instance_method) {
        SetMsg(out, "NetworkUpdater.get_instance 方法未缓存");
        return nullptr;
    }
    // instance 是 static property，调用时 obj=null
    Il2CppObject* result = nullptr;
    if (!RuntimeInvoke(g_nu_get_instance_method, nullptr, nullptr, &result, "NetworkUpdater.get_instance", out)) {
        return nullptr;
    }
    if (!result) {
        SetMsg(out, "NetworkUpdater.instance 为空（可能未在对局中）");
    }
    return result;
}

bool InvokeMoveInternal(float x, float y, float z, bool forceSend, std::string& outMessage) {
    g_move_call_count++;
    std::lock_guard<std::mutex> lock(g_call_mutex);

    std::string msg;
    if (!EnsureIl2CppApi(msg)) {
        g_fail_count++;
        SetStatus("失败：" + msg);
        outMessage = msg;
        return false;
    }

    Il2CppDomain* domain = GetDomain(msg);
    if (!domain) {
        g_fail_count++;
        SetStatus("失败：" + msg);
        outMessage = msg;
        return false;
    }

    // ImGui 渲染线程不是 Unity 脚本线程；runtime_invoke 前必须 attach。
    AttachCurrentThreadIfNeeded(domain);

    if (!ResolveMoveMembers(msg)) {
        g_fail_count++;
        SetStatus("失败：" + msg);
        outMessage = msg;
        return false;
    }

    void* instance = GetGameCoreInstance(msg);
    if (!instance) {
        g_fail_count++;
        SetStatus("失败：" + msg);
        outMessage = msg;
        return false;
    }

    UnityVector3 dir{x, y, z};
    void* params[1] = { &dir };

    // 对应源码：GameCoreCenter.instance.Move(direction);
    // 它只更新 JostickDir，游戏自身 FixedUpdate()/SendMove() 会按 SendOffset 节流发送移动。
    if (!RuntimeInvoke(g_move_method, instance, params, nullptr, "GameCoreCenter.instance.Move(Vector3)", msg)) {
        g_fail_count++;
        SetStatus("失败：" + msg);
        outMessage = msg;
        return false;
    }

    // 停止摇杆时同步调用 ForceSendMove()，等价于游戏摇杆 OnClick 里的停止逻辑，避免松手后方向残留。
    if (forceSend && g_force_send_move_method) {
        if (!RuntimeInvoke(g_force_send_move_method, instance, nullptr, nullptr, "GameCoreCenter.instance.ForceSendMove()", msg)) {
            g_fail_count++;
            SetStatus("失败：" + msg);
            outMessage = msg;
            return false;
        }
    }

    g_last_move_dir = dir;
    g_success_count++;
    SetMsg(outMessage,
           "成功：已调用 GameCoreCenter.instance.Move(%.2f, %.2f, %.2f)%s inst=%p image=%s",
           x, y, z,
           forceSend ? " + ForceSendMove()" : "",
           instance,
           g_found_image_name.empty() ? "unknown" : g_found_image_name.c_str());
    SetStatus(outMessage);
    return true;
}

} // namespace

bool GameCall_InvokeDevide(std::string& outMessage) {
    g_button_click_count++;
    std::lock_guard<std::mutex> lock(g_call_mutex);

    std::string msg;
    if (!EnsureIl2CppApi(msg)) {
        g_fail_count++;
        SetStatus("失败：" + msg);
        outMessage = msg;
        return false;
    }

    Il2CppDomain* domain = GetDomain(msg);
    if (!domain) {
        g_fail_count++;
        SetStatus("失败：" + msg);
        outMessage = msg;
        return false;
    }

    // 当前按钮是在 ImGui/GL 渲染 JNI 线程里执行的；调用 IL2CPP API 前先 attach，避免跨 native 线程 runtime_invoke 闪退。
    AttachCurrentThreadIfNeeded(domain);

    if (!ResolveGameCoreMembers(msg)) {
        g_fail_count++;
        SetStatus("失败：" + msg);
        outMessage = msg;
        return false;
    }

    void* instance = GetGameCoreInstance(msg);
    if (!instance) {
        g_fail_count++;
        SetStatus("失败：" + msg);
        outMessage = msg;
        return false;
    }

    // 严格按用户确认的源码入口调用：GameCoreCenter.instance.SendDevide();
    // 这里不再读取/直调 methodPointer，也不再 hook FixedUpdate，避免 IL2CPP 调用约定猜错导致闪退。
    if (!RuntimeInvoke(g_send_devide_method, instance, nullptr, nullptr, "GameCoreCenter.instance.SendDevide()", msg)) {
        g_fail_count++;
        SetStatus("失败：" + msg);
        outMessage = msg;
        return false;
    }

    g_success_count++;
    SetMsg(outMessage,
           "成功：已通过 il2cpp_runtime_invoke 调用 GameCoreCenter.instance.SendDevide() inst=%p image=%s",
           instance, g_found_image_name.empty() ? "unknown" : g_found_image_name.c_str());
    SetStatus(outMessage);
    return true;
}

bool GameCall_InvokeSpitBall(std::string& outMessage) {
    g_button_click_count++;
    std::lock_guard<std::mutex> lock(g_call_mutex);

    std::string msg;
    if (!EnsureIl2CppApi(msg)) {
        g_fail_count++;
        SetStatus("失败：" + msg);
        outMessage = msg;
        return false;
    }

    Il2CppDomain* domain = GetDomain(msg);
    if (!domain) {
        g_fail_count++;
        SetStatus("失败：" + msg);
        outMessage = msg;
        return false;
    }

    AttachCurrentThreadIfNeeded(domain);

    if (!ResolveSpitBallMembers(msg)) {
        g_fail_count++;
        SetStatus("失败：" + msg);
        outMessage = msg;
        return false;
    }

    void* instance = GetGameCoreInstance(msg);
    if (!instance) {
        g_fail_count++;
        SetStatus("失败：" + msg);
        outMessage = msg;
        return false;
    }

    // Step 1: SetFreeTypeFlag(1) — 设 flag=1（吐球方向，0 是分身方向）
    int32_t flag = 1;
    void* flagParams[1] = { &flag };
    if (!RuntimeInvoke(g_set_free_type_flag_method, instance, flagParams, nullptr, "SetFreeTypeFlag(1)", msg)) {
        g_fail_count++;
        SetStatus("失败：" + msg);
        outMessage = msg;
        return false;
    }

    // Step 2: FreeTypeClick(true) — force=true 跳过 DisableFeedControl 检查
    // 内部走 ReqFreeType(0, freeTypeFlag=1) → UDP FeedType，适用于正常对战模式（isNewSync）
    // IL2CPP bool 参数以 int32_t 传递，值为 1 表示 true
    int32_t force = 1;
    void* clickParams[1] = { &force };
    // 判断 FreeTypeClick 是否带参数（带 force 参数则传，不带则不传）
    bool hasForceParam = (g_api.class_get_method_from_name(g_game_core_class, "FreeTypeClick", 1) != nullptr);
    if (!hasForceParam) {
        // 0 参数版本：直接调用
        if (!RuntimeInvoke(g_free_type_click_method, instance, nullptr, nullptr, "FreeTypeClick()", msg)) {
            g_fail_count++;
            SetStatus("失败：" + msg);
            outMessage = msg;
            return false;
        }
    } else {
        // 1 参数版本：传 force=true
        if (!RuntimeInvoke(g_free_type_click_method, instance, clickParams, nullptr, "FreeTypeClick(true)", msg)) {
            g_fail_count++;
            SetStatus("失败：" + msg);
            outMessage = msg;
            return false;
        }
    }

    g_success_count++;
    SetMsg(outMessage,
           "成功：已调用 SetFreeTypeFlag(1) + FreeTypeClick(true) → ReqFreeType(0,1) inst=%p image=%s",
           instance, g_found_image_name.empty() ? "unknown" : g_found_image_name.c_str());
    SetStatus(outMessage);
    return true;
}

int GameCall_InvokeSpitBallFast(int count, std::string& outMessage) {
    if (count < 1) count = 1;
    if (count > 20) count = 20;

    g_button_click_count++;
    std::lock_guard<std::mutex> lock(g_call_mutex);

    std::string msg;
    if (!EnsureIl2CppApi(msg)) {
        g_fail_count++;
        SetStatus("失败：" + msg);
        outMessage = msg;
        return 0;
    }

    Il2CppDomain* domain = GetDomain(msg);
    if (!domain) {
        g_fail_count++;
        SetStatus("失败：" + msg);
        outMessage = msg;
        return 0;
    }

    AttachCurrentThreadIfNeeded(domain);

    if (!ResolveSpitBallMembers(msg)) {
        g_fail_count++;
        SetStatus("失败：" + msg);
        outMessage = msg;
        return 0;
    }

    void* instance = GetGameCoreInstance(msg);
    if (!instance) {
        g_fail_count++;
        SetStatus("失败：" + msg);
        outMessage = msg;
        return 0;
    }

    // Step 1: SetFreeTypeFlag(1) — 只需设置一次
    int32_t flag = 1;
    void* flagParams[1] = { &flag };
    if (!RuntimeInvoke(g_set_free_type_flag_method, instance, flagParams, nullptr, "SetFreeTypeFlag(1)", msg)) {
        g_fail_count++;
        SetStatus("失败：" + msg);
        outMessage = msg;
        return 0;
    }

    // Step 2: 连续调用 FreeTypeClick(true) N 次
    // FreeTypeClick 内部走 ReqFreeType(0, freeTypeFlag) → ReqFeedType → ExternDllManager.FeedType (UDP)
    // 无客户端时间限制，不触发 spitCTTime 反作弊计数器
    int32_t force = 1;
    void* clickParams[1] = { &force };
    bool hasForceParam = (g_api.class_get_method_from_name(g_game_core_class, "FreeTypeClick", 1) != nullptr);

    int successCount = 0;
    for (int i = 0; i < count; ++i) {
        if (hasForceParam) {
            if (RuntimeInvoke(g_free_type_click_method, instance, clickParams, nullptr, "FreeTypeClick(true)", msg)) {
                successCount++;
            } else {
                break;
            }
        } else {
            if (RuntimeInvoke(g_free_type_click_method, instance, nullptr, nullptr, "FreeTypeClick()", msg)) {
                successCount++;
            } else {
                break;
            }
        }
    }

    g_success_count++;
    SetMsg(outMessage,
           "加速吐球：SetFreeTypeFlag(1) + FreeTypeClick(true)×%d 成功%d次 inst=%p",
           count, successCount, instance);
    SetStatus(outMessage);
    return successCount;
}

int GameCall_InvokeSpitBallDirect(int count, std::string& outMessage) {
    if (count < 1) count = 1;
    if (count > 20) count = 20;

    g_button_click_count++;
    std::lock_guard<std::mutex> lock(g_call_mutex);

    std::string msg;
    if (!EnsureIl2CppApi(msg)) {
        g_fail_count++;
        SetStatus("失败：" + msg);
        outMessage = msg;
        return 0;
    }

    Il2CppDomain* domain = GetDomain(msg);
    if (!domain) {
        g_fail_count++;
        SetStatus("失败：" + msg);
        outMessage = msg;
        return 0;
    }

    AttachCurrentThreadIfNeeded(domain);

    if (!ResolveNetworkUpdaterMembers(msg)) {
        g_fail_count++;
        SetStatus("失败：" + msg);
        outMessage = msg;
        return 0;
    }

    void* nuInstance = GetNetworkUpdaterInstance(msg);
    if (!nuInstance) {
        g_fail_count++;
        SetStatus("失败：" + msg);
        outMessage = msg;
        return 0;
    }

    // 直接调用 ReqFreeType(0, 1) N 次
    // type=0: 单次吐球 (FeedType → ExternDllManager.FeedType → UDP)
    // flag=1: 吐球方向
    // 绕过 HaveSelfBall()、DisableFeedControl、BtnIsFeeding、SpitingDeltaTime 所有检查
    int32_t type = 0;
    int32_t flag = 1;
    void* params[2] = { &type, &flag };

    int successCount = 0;
    for (int i = 0; i < count; ++i) {
        if (RuntimeInvoke(g_nu_req_free_type_method, nuInstance, params, nullptr, "ReqFreeType(0,1)", msg)) {
            successCount++;
        } else {
            break;
        }
    }

    g_success_count++;
    SetMsg(outMessage,
           "直接吐球：NetworkUpdater.ReqFreeType(0,1)×%d 成功%d次 inst=%p",
           count, successCount, nuInstance);
    SetStatus(outMessage);
    return successCount;
}

// ==================== 视野修改 ====================

static Il2CppClass* g_camera_follow_class = nullptr;
static Il2CppField* g_self_camera_field = nullptr;
static Il2CppField* g_grass_camera_field = nullptr;
static Il2CppField* g_cf_size_field = nullptr;
static Il2CppField* g_cf_width_field = nullptr;
static Il2CppField* g_cf_sizefen1_field = nullptr;
static Il2CppField* g_cf_rect_field = nullptr;
static Il2CppField* g_cf_scalesize_field = nullptr;
static Il2CppField* g_cf_view_field = nullptr;
static Il2CppField* g_cf_xupdate_field = nullptr;
static Il2CppClass* g_camera_class = nullptr;
static const Il2CppMethod* g_set_orthographic_size_method = nullptr;

// GameCoreCenter.IsGameOverPause
static Il2CppField* g_is_gameover_pause_field = nullptr;

// CameraFollow.get_instance（静态属性 getter，返回 CameraFollow 实例）
static const Il2CppMethod* g_cf_get_instance_method = nullptr;
// CameraFollow.GameOverPullUp(float, float, Ease) — 设置 xUpdate=null + DOTween 动画
static const Il2CppMethod* g_cf_gameover_pullup_method = nullptr;
// 标记是否已调用过 GameOverPullUp（避免每帧重复创建 DOTween）
static bool g_view_pullup_done = false;

bool ResolveViewMembers(std::string& out) {
    if (!EnsureIl2CppApi(out)) return false;

    if (!g_camera_follow_class) {
        g_camera_follow_class = FindClassEverywhere("", "CameraFollow", out);
        if (!g_camera_follow_class) return false;
    }

    if (!g_api.class_get_field_from_name) {
        SetMsg(out, "il2cpp_class_get_field_from_name 未加载");
        return false;
    }

    if (!g_self_camera_field)
        g_self_camera_field = g_api.class_get_field_from_name(g_camera_follow_class, "selfCamera");
    if (!g_grass_camera_field)
        g_grass_camera_field = g_api.class_get_field_from_name(g_camera_follow_class, "GrassCamera");
    if (!g_cf_size_field)
        g_cf_size_field = g_api.class_get_field_from_name(g_camera_follow_class, "Size");
    if (!g_cf_width_field)
        g_cf_width_field = g_api.class_get_field_from_name(g_camera_follow_class, "Width");
    if (!g_cf_sizefen1_field)
        g_cf_sizefen1_field = g_api.class_get_field_from_name(g_camera_follow_class, "SizeFen1");
    if (!g_cf_rect_field)
        g_cf_rect_field = g_api.class_get_field_from_name(g_camera_follow_class, "Rect");
    if (!g_cf_scalesize_field)
        g_cf_scalesize_field = g_api.class_get_field_from_name(g_camera_follow_class, "ScaleSize");
    if (!g_cf_view_field)
        g_cf_view_field = g_api.class_get_field_from_name(g_camera_follow_class, "view");
    if (!g_cf_xupdate_field)
        g_cf_xupdate_field = g_api.class_get_field_from_name(g_camera_follow_class, "xUpdate");

    if (!g_self_camera_field) {
        SetMsg(out, "未找到 CameraFollow.selfCamera 字段");
        return false;
    }
    if (!g_cf_size_field || !g_cf_width_field || !g_cf_sizefen1_field || !g_cf_rect_field) {
        SetMsg(out, "未找到 CameraFollow 的 Size/Width/SizeFen1/Rect 字段");
        return false;
    }

    if (!g_camera_class) {
        g_camera_class = FindClassEverywhere("UnityEngine", "Camera", out);
        if (!g_camera_class) return false;
    }

    if (!g_set_orthographic_size_method)
        g_set_orthographic_size_method = g_api.class_get_method_from_name(g_camera_class, "set_orthographicSize", 1);
    if (!g_set_orthographic_size_method) {
        SetMsg(out, "未找到 Camera.set_orthographicSize");
        return false;
    }

    // 获取 CameraFollow.get_instance() 静态方法（用于获取实例）
    if (!g_cf_get_instance_method) {
        g_cf_get_instance_method = g_api.class_get_method_from_name(g_camera_follow_class, "get_instance", 0);
    }
    // 获取 GameOverPullUp(float, float, Ease) 方法
    if (!g_cf_gameover_pullup_method) {
        g_cf_gameover_pullup_method = g_api.class_get_method_from_name(g_camera_follow_class, "GameOverPullUp", 3);
    }
    // 查找 GameCoreCenter.IsGameOverPause（实例字段，需要 instance 才能设置）
    if (!g_is_gameover_pause_field && g_game_core_class) {
        g_is_gameover_pause_field = g_api.class_get_field_from_name(g_game_core_class, "IsGameOverPause");
    }
    return true;
}

void GameCall_ResetViewPullup() {
    // 重置 ViewScaleFactor 到默认值 0.5
    std::lock_guard<std::mutex> lock(g_call_mutex);
    std::string msg;
    if (!EnsureIl2CppApi(msg)) return;
    Il2CppDomain* domain = GetDomain(msg);
    if (!domain) return;
    AttachCurrentThreadIfNeeded(domain);
    if (!ResolveGameCoreMembers(msg)) return;
    void* gccInstance = GetGameCoreInstance(msg);
    if (!gccInstance) return;
    if (g_viewscale_factor_field && g_api.field_get_offset) {
        int32_t offset = g_api.field_get_offset(g_viewscale_factor_field);
        if (offset >= 0) {
            *(float*)((char*)gccInstance + offset) = 0.5f;
        }
    }
    g_view_pullup_done = false;
}

void GameCall_SetViewScale(float scale, std::string& outMessage) {
    std::lock_guard<std::mutex> lock(g_call_mutex);

    std::string msg;
    if (!EnsureIl2CppApi(msg)) {
        outMessage = "视野修改失败：" + msg;
        return;
    }
    Il2CppDomain* domain = GetDomain(msg);
    if (!domain) {
        outMessage = "视野修改失败：" + msg;
        return;
    }
    AttachCurrentThreadIfNeeded(domain);

    if (!ResolveGameCoreMembers(msg)) {
        outMessage = "视野修改失败：" + msg;
        return;
    }
    void* gccInstance = GetGameCoreInstance(msg);
    if (!gccInstance) {
        outMessage = "视野修改失败：" + msg;
        return;
    }

    if (!g_viewscale_factor_field || !g_api.field_get_offset) {
        outMessage = "视野修改失败：未找到 ViewScaleFactor";
        return;
    }

    // 通过字段偏移量直接写内存（不依赖 il2cpp_field_set_value）
    int32_t offset = g_api.field_get_offset(g_viewscale_factor_field);
    if (offset < 0) {
        outMessage = "视野修改失败：字段偏移量无效";
        return;
    }

    // ViewScaleFactor 默认 0.5，值越大视野越大
    float value = 0.5f * scale;
    *(float*)((char*)gccInstance + offset) = value;

    SetMsg(outMessage, "ViewScaleFactor=%.3f (%.1fx)", value, scale);
}

// ==================== 粘合修改 ====================

bool ResolveDrawCircleMembers(std::string& out) {
    if (!EnsureIl2CppApi(out)) return false;

    if (!g_drawcircle_class) {
        g_drawcircle_class = FindClassEverywhere("", "DrawCircle", out);
        if (!g_drawcircle_class) return false;
    }

    if (!g_api.class_get_field_from_name) {
        SetMsg(out, "il2cpp_class_get_field_from_name 未加载");
        return false;
    }

    if (!g_atime_setting_offet_field) {
        g_atime_setting_offet_field = g_api.class_get_field_from_name(g_drawcircle_class, "ATime_SettingOffet");
    }
    if (!g_atime_setting_offet_field) {
        SetMsg(out, "未找到 DrawCircle.ATime_SettingOffet 字段");
        return false;
    }
    return true;
}

void GameCall_ResetMerge() {
    std::lock_guard<std::mutex> lock(g_call_mutex);
    std::string msg;
    if (!EnsureIl2CppApi(msg)) return;
    Il2CppDomain* domain = GetDomain(msg);
    if (!domain) return;
    AttachCurrentThreadIfNeeded(domain);
    if (!ResolveDrawCircleMembers(msg)) return;
    // 重置为默认值 0
    float defaultVal = 0.0f;
    g_api.field_static_set_value(g_atime_setting_offet_field, &defaultVal);
}

void GameCall_SetMerge(float value, std::string& outMessage) {
    std::lock_guard<std::mutex> lock(g_call_mutex);

    std::string msg;
    if (!EnsureIl2CppApi(msg)) {
        outMessage = "粘合修改失败：" + msg;
        return;
    }
    Il2CppDomain* domain = GetDomain(msg);
    if (!domain) {
        outMessage = "粘合修改失败：" + msg;
        return;
    }
    AttachCurrentThreadIfNeeded(domain);

    if (!ResolveDrawCircleMembers(msg)) {
        outMessage = "粘合修改失败：" + msg;
        return;
    }

    // ATimeFen1 = 0.588 + ATime_SettingOffet
    // 用户设置的值就是 ATimeFen1 的目标值
    // 所以 ATime_SettingOffet = value - 0.588
    float offset = value - 0.588f;
    g_api.field_static_set_value(g_atime_setting_offet_field, &offset);

    SetMsg(outMessage, "ATimeFen1=%.3f (offset=%.3f)", value, offset);
}

// ==================== 排名名字修改（独特 rank_id，禁止排行榜） ====================
// 稳定版（Self 已验证不闪退）基础上扩展到所有人：
//   - 独特 rank：LastRank>0 优先，否则 PlayerBase.ID
//   - 只写 Name 字段（托管 string 引用）
//   - 绝不调用 Rename / UpdateLable / HandleName（渲染线程会崩）
//   - 绝不使用 TopPlayers 名次
//   - 分批改：每轮最多 kBatch 个，从 cursor 续跑，避免一次改太多

static Il2CppClass* g_playerbase_class = nullptr;
static Il2CppField* g_playerdic_field = nullptr;
static Il2CppField* g_id_field = nullptr;
static int32_t g_id_offset = 16;
static Il2CppField* g_name_field = nullptr;
static int32_t g_name_offset = 24;
static int32_t g_oriname_offset = 32; // dump: OriName@32
static int32_t g_namelables_offset = 72; // dump: NameLables@72
static Il2CppField* g_nametext_field = nullptr;
static int32_t g_nametext_offset = -1; // TargetNameScale2.NameText
static Il2CppField* g_lelable_fontsize_field = nullptr;
static int32_t g_lelable_fontsize_offset = -1;
static Il2CppField* g_lelable_maxlength_field = nullptr;
static int32_t g_lelable_maxlength_offset = -1;
static const Il2CppMethod* g_list_count_method = nullptr;
static const Il2CppMethod* g_list_item_method = nullptr;
static const Il2CppMethod* g_setinfo_method = nullptr;
static bool g_rename_show_self_prev = false;
static char g_saved_self_name[256] = {0}; // 开启显示自我前备份的原名（UTF-8）
static bool g_saved_self_name_valid = false;
static Il2CppField* g_netplayer_field = nullptr;
static int32_t g_netplayer_offset = 464;
static int32_t g_msg_lastrank_offset = 64;
static const Il2CppMethod* g_get_selfplayer_method = nullptr;
static const Il2CppMethod* g_dict_get_count_method = nullptr;
static const Il2CppMethod* g_dict_get_values_method = nullptr;
static const Il2CppMethod* g_vc_copyto_method = nullptr;
static bool g_rename_symbols_logged = false;
static std::atomic<bool> g_rename_running{false};
static int g_rename_cursor = 0;
static const int kRenameBatch = 16; // 每轮最多改 16 人

bool LooksLikeValidManagedObject(void* p) {
    if (!p) return false;
    uintptr_t v = (uintptr_t)p;
    if (v < 0x10000ULL) return false;
    void* klass = *(void**)p;
    if (!klass) return false;
    if ((uintptr_t)klass < 0x10000ULL) return false;
    return true;
}

bool ResolveRankRenameMembers(std::string& out) {
    if (!EnsureIl2CppApi(out)) return false;
    if (!g_game_core_class) {
        g_game_core_class = FindClassEverywhere("", "GameCoreCenter", out);
        if (!g_game_core_class) return false;
    }
    if (!g_api.class_get_field_from_name || !g_api.field_get_offset || !g_api.string_new) {
        SetMsg(out, "字段/string API 不可用");
        return false;
    }
    if (!g_playerdic_field)
        g_playerdic_field = g_api.class_get_field_from_name(g_game_core_class, "PlayerDic");
    if (!g_playerdic_field) {
        SetMsg(out, "未找到 PlayerDic");
        return false;
    }

    if (!g_playerbase_class) {
        g_playerbase_class = FindClassEverywhere("", "PlayerBase", out);
        if (!g_playerbase_class) return false;
    }

    if (!g_id_field) {
        g_id_field = g_api.class_get_field_from_name(g_playerbase_class, "ID");
        if (g_id_field) {
            int32_t off = g_api.field_get_offset(g_id_field);
            if (off > 0) g_id_offset = off;
        }
    }
    if (!g_name_field) {
        g_name_field = g_api.class_get_field_from_name(g_playerbase_class, "Name");
        if (g_name_field) {
            int32_t off = g_api.field_get_offset(g_name_field);
            if (off > 0) g_name_offset = off;
        }
    }
    {
        Il2CppField* f = g_api.class_get_field_from_name(g_playerbase_class, "OriName");
        if (f) {
            int32_t off = g_api.field_get_offset(f);
            if (off > 0) g_oriname_offset = off;
        }
    }
    {
        Il2CppField* f = g_api.class_get_field_from_name(g_playerbase_class, "NameLables");
        if (f) {
            int32_t off = g_api.field_get_offset(f);
            if (off > 0) g_namelables_offset = off;
        }
    }
    if (!g_netplayer_field) {
        g_netplayer_field = g_api.class_get_field_from_name(g_playerbase_class, "NetPlayer");
        if (g_netplayer_field) {
            int32_t off = g_api.field_get_offset(g_netplayer_field);
            if (off > 0) g_netplayer_offset = off;
        }
    }
    if (!g_get_selfplayer_method)
        g_get_selfplayer_method = g_api.class_get_method_from_name(g_playerbase_class, "get_SelfPlayer", 0);

    if (g_name_offset < 0x10) {
        SetMsg(out, "Name 偏移无效");
        return false;
    }
    if (!g_api.array_new || !g_api.runtime_invoke || !g_api.object_get_class) {
        SetMsg(out, "IL2CPP API 不完整");
        return false;
    }

    if (!g_rename_symbols_logged) {
        g_rename_symbols_logged = true;
    }
    return true;
}

uint64_t ReadPlayerUniqueId(Il2CppObject* playerBase) {
    if (!playerBase || !LooksLikeValidManagedObject(playerBase)) return 0;
    return *(uint64_t*)((char*)playerBase + g_id_offset);
}

int32_t ReadNetLastRank(Il2CppObject* playerBase) {
    if (!playerBase || !LooksLikeValidManagedObject(playerBase)) return 0;
    Il2CppObject* np = *(Il2CppObject**)((char*)playerBase + g_netplayer_offset);
    if (!LooksLikeValidManagedObject(np)) return 0;
    return *(int32_t*)((char*)np + g_msg_lastrank_offset);
}

// 粗略显示：999→999，1000→1千，150032→15万，100000000→1亿
// 详细显示：完整数字
static void FormatRankDisplay(uint64_t n, char* out, size_t outSz, bool detailed) {
    if (!out || outSz == 0) return;
    out[0] = 0;
    if (detailed || n < 1000ULL) {
        snprintf(out, outSz, "%llu", (unsigned long long)n);
        return;
    }
    // 亿
    if (n >= 100000000ULL) {
        uint64_t yi = n / 100000000ULL;
        uint64_t rem = n % 100000000ULL;
        // 有余数时带一位小数（向下到 0.1 亿），否则整数
        if (rem == 0) {
            snprintf(out, outSz, "%llu亿", (unsigned long long)yi);
        } else {
            // 一位小数：rem / 1e7
            int frac = (int)(rem / 10000000ULL); // 0..9
            if (frac <= 0)
                snprintf(out, outSz, "%llu亿", (unsigned long long)yi);
            else
                snprintf(out, outSz, "%llu.%d亿", (unsigned long long)yi, frac);
        }
        return;
    }
    // 万（>=10000）
    if (n >= 10000ULL) {
        uint64_t wan = n / 10000ULL; // 150032 → 15
        snprintf(out, outSz, "%llu万", (unsigned long long)wan);
        return;
    }
    // 千（1000..9999）
    uint64_t qian = n / 1000ULL; // 1000 → 1
    snprintf(out, outSz, "%llu千", (unsigned long long)qian);
}

bool BuildUniqueRankString(Il2CppObject* playerBase, char* outBuf, size_t outSz, const char** outSrc) {
    if (!playerBase || !outBuf || outSz == 0) return false;
    outBuf[0] = 0;
    if (outSrc) *outSrc = "?";

    uint64_t value = 0;
    int32_t lastRank = ReadNetLastRank(playerBase);
    if (lastRank > 0 && lastRank <= 100000) {
        value = (uint64_t)lastRank;
        if (outSrc) *outSrc = "LastRank";
    } else {
        uint64_t id = ReadPlayerUniqueId(playerBase);
        if (id == 0) return false;
        value = id;
        if (outSrc) *outSrc = "ID";
    }

    char raw[64];
    FormatRankDisplay(value, raw, sizeof(raw), g_rename_detailed_rank);

    // NGUI 颜色前缀 [RRGGBB]
    if (g_rename_use_color) {
        auto toByte = [](float x) -> int {
            if (x < 0.f) x = 0.f;
            if (x > 1.f) x = 1.f;
            return (int)(x * 255.0f + 0.5f);
        };
        int r = toByte(g_rename_name_color.x);
        int g = toByte(g_rename_name_color.y);
        int b = toByte(g_rename_name_color.z);
        snprintf(outBuf, outSz, "[%02X%02X%02X]%s", r, g, b, raw);
    } else {
        snprintf(outBuf, outSz, "%s", raw);
    }
    return true;
}

bool WriteNameFieldOnly(Il2CppObject* playerBase, const char* name) {
    if (!playerBase || !name || !name[0] || !g_api.string_new) return false;
    if (!LooksLikeValidManagedObject(playerBase)) return false;

    Il2CppObject* nameStr = g_api.string_new(name);
    if (!nameStr || !LooksLikeValidManagedObject(nameStr)) return false;

    *(Il2CppObject**)((char*)playerBase + g_name_offset) = nameStr;
    return true;
}

// 从托管 String 粗读 UTF-16 → UTF-8（仅 ASCII/常见中文 BMP）
static bool ReadIl2CppStringUtf8(Il2CppObject* strObj, char* out, size_t outSz) {
    if (!strObj || !out || outSz == 0 || !LooksLikeValidManagedObject(strObj)) return false;
    out[0] = 0;
    // layout: +0 klass, +8 monitor, +16 length(int32), +20 chars utf16
    int32_t len = *(int32_t*)((char*)strObj + 16);
    if (len < 0) len = 0;
    if (len > 200) len = 200;
    const uint16_t* chars = (const uint16_t*)((char*)strObj + 20);
    size_t o = 0;
    for (int i = 0; i < len && o + 4 < outSz; ++i) {
        uint32_t cp = chars[i];
        if (cp < 0x80) {
            out[o++] = (char)cp;
        } else if (cp < 0x800) {
            out[o++] = (char)(0xC0 | (cp >> 6));
            out[o++] = (char)(0x80 | (cp & 0x3F));
        } else {
            out[o++] = (char)(0xE0 | (cp >> 12));
            out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[o++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    out[o] = 0;
    return o > 0;
}

// 优先读 OriName，其次当前 Name，备份到 g_saved_self_name
static void BackupSelfOriginalName(Il2CppObject* selfPlayer) {
    if (!selfPlayer || !LooksLikeValidManagedObject(selfPlayer)) return;
    if (g_saved_self_name_valid && g_saved_self_name[0]) return; // 已有备份不覆盖

    Il2CppObject* s = nullptr;
    if (g_oriname_offset > 0) {
        s = *(Il2CppObject**)((char*)selfPlayer + g_oriname_offset);
    }
    if (!s || !LooksLikeValidManagedObject(s)) {
        s = *(Il2CppObject**)((char*)selfPlayer + g_name_offset);
    }
    if (ReadIl2CppStringUtf8(s, g_saved_self_name, sizeof(g_saved_self_name))) {
        g_saved_self_name_valid = true;
    }
}

static void RestoreSelfOriginalName(Il2CppObject* selfPlayer) {
    if (!selfPlayer || !LooksLikeValidManagedObject(selfPlayer)) return;
    const char* restore = nullptr;
    char tmp[256];

    // 1) 优先用我们备份的
    if (g_saved_self_name_valid && g_saved_self_name[0]) {
        restore = g_saved_self_name;
    } else {
        // 2) 回退 OriName 字段
        Il2CppObject* s = nullptr;
        if (g_oriname_offset > 0)
            s = *(Il2CppObject**)((char*)selfPlayer + g_oriname_offset);
        if (ReadIl2CppStringUtf8(s, tmp, sizeof(tmp))) {
            restore = tmp;
        }
    }
    if (!restore || !restore[0]) {
        return;
    }
    WriteNameFieldOnly(selfPlayer, restore);
}

void GameCall_RenameToRank(std::string& outMessage) {
    if (g_rename_running.exchange(true)) {
        outMessage = "改名进行中，跳过";
        return;
    }

    std::lock_guard<std::mutex> lock(g_call_mutex);

    std::string msg;
    auto finish = [&](const std::string& m) {
        outMessage = m;
        g_rename_running = false;
    };

    if (!EnsureIl2CppApi(msg)) { finish("失败:" + msg); return; }
    Il2CppDomain* domain = GetDomain(msg);
    if (!domain) { finish("失败:" + msg); return; }
    AttachCurrentThreadIfNeeded(domain);
    if (!ResolveRankRenameMembers(msg)) { finish("失败:" + msg); return; }

    void* gcc = GetGameCoreInstance(msg);
    if (!gcc) { finish("失败:" + msg); return; }

    Il2CppObject* playerDic = nullptr;
    int32_t pdOff = g_api.field_get_offset(g_playerdic_field);
    if (pdOff >= 0) playerDic = *(Il2CppObject**)((char*)gcc + pdOff);
    if (!playerDic || !LooksLikeValidManagedObject(playerDic)) {
        finish("PlayerDic 无效（请进对局）");
        return;
    }

    Il2CppClass* dictClass = g_api.object_get_class(playerDic);
    if (!g_dict_get_count_method && dictClass)
        g_dict_get_count_method = g_api.class_get_method_from_name(dictClass, "get_Count", 0);
    if (!g_dict_get_values_method && dictClass)
        g_dict_get_values_method = g_api.class_get_method_from_name(dictClass, "get_Values", 0);
    if (!g_dict_get_count_method || !g_dict_get_values_method) {
        finish("Dictionary API 缺失");
        return;
    }

    Il2CppException* exc = nullptr;
    Il2CppObject* countObj = g_api.runtime_invoke(g_dict_get_count_method, playerDic, nullptr, &exc);
    if (exc || !countObj) { finish("Count 失败"); return; }
    int dictCount = *(int*)((char*)countObj + 2 * sizeof(void*));
    if (dictCount < 0) dictCount = 0;
    if (dictCount > 200) dictCount = 200;
    if (dictCount == 0) { finish("PlayerDic 为空"); return; }

    if (g_rename_cursor >= dictCount) g_rename_cursor = 0;

    exc = nullptr;
    Il2CppObject* valuesObj = g_api.runtime_invoke(g_dict_get_values_method, playerDic, nullptr, &exc);
    if (exc || !valuesObj || !LooksLikeValidManagedObject(valuesObj)) {
        finish("Values 失败");
        return;
    }

    Il2CppObject* arr = g_api.array_new(g_playerbase_class, dictCount);
    if (!arr || !LooksLikeValidManagedObject(arr)) {
        finish("array_new 失败");
        return;
    }

    Il2CppClass* vcClass = g_api.object_get_class(valuesObj);
    if (!g_vc_copyto_method && vcClass)
        g_vc_copyto_method = g_api.class_get_method_from_name(vcClass, "CopyTo", 2);
    if (!g_vc_copyto_method) { finish("CopyTo 缺失"); return; }

    int32_t zero = 0;
    void* copyParams[2] = { arr, &zero };
    exc = nullptr;
    g_api.runtime_invoke(g_vc_copyto_method, valuesObj, copyParams, &exc);
    if (exc) { finish("CopyTo 异常"); return; }

    int32_t arrLen = *(int32_t*)((char*)arr + 24);
    if (arrLen < 0) arrLen = 0;
    if (arrLen > dictCount) arrLen = dictCount;
    if (arrLen == 0) { finish("数组长度0"); return; }
    if (g_rename_cursor >= arrLen) g_rename_cursor = 0;

    int endIdx = g_rename_cursor + kRenameBatch;
    if (endIdx > arrLen) endIdx = arrLen;

    int renamed = 0, skipped = 0, failed = 0;
    int usedLastRank = 0, usedId = 0;

    // 解析自身 ID：默认不改自己，除非勾选「显示自我」
    uint64_t selfId = 0;
    Il2CppObject* selfPlayerCached = nullptr;
    if (g_get_selfplayer_method) {
        exc = nullptr;
        selfPlayerCached = (Il2CppObject*)g_api.runtime_invoke(
            g_get_selfplayer_method, nullptr, nullptr, &exc);
        if (exc || !LooksLikeValidManagedObject(selfPlayerCached)) {
            selfPlayerCached = nullptr;
        } else {
            selfId = ReadPlayerUniqueId(selfPlayerCached);
        }
    }

    // 显示自我：开→关 时立刻恢复原名；关→开 时先备份原名
    if (selfPlayerCached) {
        if (g_rename_show_self && !g_rename_show_self_prev) {
            // 刚打开：备份当前原名（只备份一次，直到恢复）
            g_saved_self_name_valid = false;
            g_saved_self_name[0] = 0;
            BackupSelfOriginalName(selfPlayerCached);
        } else if (!g_rename_show_self && g_rename_show_self_prev) {
            // 刚关闭：恢复原名
            RestoreSelfOriginalName(selfPlayerCached);
            // 恢复后清备份，便于下次重新备份
            // 保留备份内容也可；这里保留 valid，避免 OriName 已被污染时丢名
        } else if (!g_rename_show_self) {
            // 持续关闭：每轮确保自己不是排名名（防止被其它逻辑误改）
            // 若当前看起来仍是我们写的排名，再恢复一次
            if (g_saved_self_name_valid && g_saved_self_name[0]) {
                // 轻量：直接写回备份，成本低
                WriteNameFieldOnly(selfPlayerCached, g_saved_self_name);
            }
        }
        g_rename_show_self_prev = g_rename_show_self;
    }


    for (int i = g_rename_cursor; i < endIdx; ++i) {
        Il2CppObject* pb = *(Il2CppObject**)((char*)arr + 32 + (size_t)i * 8);
        if (!LooksLikeValidManagedObject(pb)) {
            skipped++;
            continue;
        }

        uint64_t pid = ReadPlayerUniqueId(pb);
        // 默认跳过自己
        if (!g_rename_show_self && selfId != 0 && pid == selfId) {
            skipped++;
            continue;
        }

        char rankStr[64];
        const char* src = nullptr;
        if (!BuildUniqueRankString(pb, rankStr, sizeof(rankStr), &src)) {
            skipped++;
            continue;
        }

        

        if (WriteNameFieldOnly(pb, rankStr)) {
            renamed++;
            if (src && strcmp(src, "LastRank") == 0) usedLastRank++;
            else usedId++;
        } else {
            failed++;
        }
    }

    // 仅当勾选「显示自我」时才把自身改成排名名
    if (g_rename_show_self && selfPlayerCached) {
        // 确保有备份
        BackupSelfOriginalName(selfPlayerCached);
        char rankStr[64];
        const char* src = nullptr;
        if (BuildUniqueRankString(selfPlayerCached, rankStr, sizeof(rankStr), &src)) {
            WriteNameFieldOnly(selfPlayerCached, rankStr);
        }
    }

    g_rename_cursor = endIdx;
    if (g_rename_cursor >= arrLen) {
        g_rename_cursor = 0;
    }

    SetMsg(outMessage,
           "全员独特rank：总数=%d 本轮[%d人] ok=%d skip=%d fail=%d next_cursor=%d (LastRank=%d ID=%d)",
           arrLen, endIdx - (endIdx > 0 ? (endIdx - renamed - skipped - failed < 0 ? 0 : 0) : 0),
           renamed, skipped, failed, g_rename_cursor, usedLastRank, usedId);
    // 上面 endIdx 展示略绕，改成清晰文案
    char clearMsg[256];
    snprintf(clearMsg, sizeof(clearMsg),
             "全员独特rank：总数=%d 本轮ok=%d skip=%d fail=%d next=%d (LastRank源=%d ID源=%d)",
             arrLen, renamed, skipped, failed, g_rename_cursor, usedLastRank, usedId);
    finish(clearMsg);
}


void GameCall_RestoreAllNames(std::string& outMessage) {
    if (g_rename_running.exchange(true)) {
        outMessage = "恢复进行中，跳过";
        return;
    }
    std::lock_guard<std::mutex> lock(g_call_mutex);

    std::string msg;
    auto finish = [&](const std::string& m) {
        outMessage = m;
        g_rename_running = false;
    };

    if (!EnsureIl2CppApi(msg)) { finish("恢复失败:" + msg); return; }
    Il2CppDomain* domain = GetDomain(msg);
    if (!domain) { finish("恢复失败:" + msg); return; }
    AttachCurrentThreadIfNeeded(domain);
    if (!ResolveRankRenameMembers(msg)) { finish("恢复失败:" + msg); return; }

    void* gcc = GetGameCoreInstance(msg);
    if (!gcc) { finish("恢复失败:" + msg); return; }

    Il2CppObject* playerDic = nullptr;
    int32_t pdOff = g_api.field_get_offset(g_playerdic_field);
    if (pdOff >= 0) playerDic = *(Il2CppObject**)((char*)gcc + pdOff);
    if (!playerDic || !LooksLikeValidManagedObject(playerDic)) {
        finish("恢复失败：PlayerDic 无效");
        return;
    }

    Il2CppClass* dictClass = g_api.object_get_class(playerDic);
    if (!g_dict_get_count_method && dictClass)
        g_dict_get_count_method = g_api.class_get_method_from_name(dictClass, "get_Count", 0);
    if (!g_dict_get_values_method && dictClass)
        g_dict_get_values_method = g_api.class_get_method_from_name(dictClass, "get_Values", 0);
    if (!g_dict_get_count_method || !g_dict_get_values_method) {
        finish("恢复失败：Dictionary API 缺失");
        return;
    }

    Il2CppException* exc = nullptr;
    Il2CppObject* countObj = g_api.runtime_invoke(g_dict_get_count_method, playerDic, nullptr, &exc);
    if (exc || !countObj) { finish("恢复失败：Count"); return; }
    int dictCount = *(int*)((char*)countObj + 2 * sizeof(void*));
    if (dictCount < 0) dictCount = 0;
    if (dictCount > 200) dictCount = 200;
    if (dictCount == 0) { finish("恢复：PlayerDic 为空"); return; }

    exc = nullptr;
    Il2CppObject* valuesObj = g_api.runtime_invoke(g_dict_get_values_method, playerDic, nullptr, &exc);
    if (exc || !valuesObj) { finish("恢复失败：Values"); return; }

    Il2CppObject* arr = g_api.array_new(g_playerbase_class, dictCount);
    if (!arr) { finish("恢复失败：array_new"); return; }

    Il2CppClass* vcClass = g_api.object_get_class(valuesObj);
    if (!g_vc_copyto_method && vcClass)
        g_vc_copyto_method = g_api.class_get_method_from_name(vcClass, "CopyTo", 2);
    if (!g_vc_copyto_method) { finish("恢复失败：CopyTo"); return; }

    int32_t zero = 0;
    void* copyParams[2] = { arr, &zero };
    exc = nullptr;
    g_api.runtime_invoke(g_vc_copyto_method, valuesObj, copyParams, &exc);
    if (exc) { finish("恢复失败：CopyTo 异常"); return; }

    int32_t arrLen = *(int32_t*)((char*)arr + 24);
    if (arrLen < 0) arrLen = 0;
    if (arrLen > dictCount) arrLen = dictCount;

    int ok = 0, skip = 0, fail = 0;
    for (int i = 0; i < arrLen; ++i) {
        Il2CppObject* pb = *(Il2CppObject**)((char*)arr + 32 + (size_t)i * 8);
        if (!LooksLikeValidManagedObject(pb)) { skip++; continue; }

        // 读 OriName
        Il2CppObject* ori = nullptr;
        if (g_oriname_offset > 0)
            ori = *(Il2CppObject**)((char*)pb + g_oriname_offset);
        char nameBuf[256];
        if (!ReadIl2CppStringUtf8(ori, nameBuf, sizeof(nameBuf)) || !nameBuf[0]) {
            skip++;
            continue;
        }
        // 去掉可能的颜色前缀再写？OriName 一般是干净原名
        if (WriteNameFieldOnly(pb, nameBuf)) {
            ok++;
        } else {
            fail++;
        }
    }

    // Self 再保证用备份/OriName
    if (g_get_selfplayer_method) {
        exc = nullptr;
        Il2CppObject* selfPlayer = (Il2CppObject*)g_api.runtime_invoke(
            g_get_selfplayer_method, nullptr, nullptr, &exc);
        if (selfPlayer && !exc && LooksLikeValidManagedObject(selfPlayer)) {
            RestoreSelfOriginalName(selfPlayer);
        }
    }

    // 复位状态，便于下次开启重新备份自己
    g_rename_show_self_prev = false;
    g_rename_cursor = 0;
    // 保留 g_saved_self_name 直到下次开启显示自我再重备；全关时可清
    g_saved_self_name_valid = false;
    g_saved_self_name[0] = 0;

    char clearMsg[256];
    snprintf(clearMsg, sizeof(clearMsg),
             "全员恢复原名：total=%d ok=%d skip=%d fail=%d",
             arrLen, ok, skip, fail);
    finish(clearMsg);
}


// ==================== 球上名字大小（独立功能） ====================
// 源码结论（真正可见的名字）：
//   LeLable.Init: 若 NeedLeLableInstanced → meshRenderer.enabled=false
//   LeLable.DrawMeshInstanced: 用 leLable.SelfTF.localToWorldMatrix 画字
//   即：可见变换是 NameText(LeLable).SelfTF，不是 TargetNameScale2 根节点 alone
//
// TargetNameScale2.Update 会设置根节点:
//   SelfTF.localScale = Clamp(...)*1.875f
// 但我们的 Hook 日志 calls=0 → 此机 NameUpdate/Update 路径可能未走到我们 hook 的指针
//
// 有效且不双名字的做法：
//   每帧只改 NameText.SelfTF.localScale = (coeff/1.875)
//   根节点仍由游戏控制；世界缩放 = 根*(coeff/1.875) = Clamp(...)*coeff
//   默认 coeff=1.875 → NameText.localScale=1 → 与原版一致
//   只缩放文字节点，不会复制第二份名字

static constexpr float kGameNameScaleCoeff = 1.875f;

struct UnityVec3NS { float x, y, z; };

static const Il2CppMethod* g_ns_get_ls = nullptr;
static const Il2CppMethod* g_ns_set_ls = nullptr;
static const Il2CppMethod* g_ns_set_lp = nullptr;
static Il2CppClass* g_ns_transform_cls = nullptr;
static int32_t g_ns_nametext_off = -1; // TargetNameScale2.NameText
static int32_t g_ns_le_selftf_off = -1; // LeLable.SelfTF
static const Il2CppMethod* g_ns_list_cnt = nullptr;
static const Il2CppMethod* g_ns_list_at = nullptr;
static bool g_ns_init_logged = false;
static int g_ns_tick_count = 0;
static int g_ns_last_applied = 0;
static int g_ns_last_fail = 0;

static bool NS_ResolveTransform() {
    if (g_ns_set_ls && g_ns_get_ls) return true;
    std::string msg;
    g_ns_transform_cls = FindClassEverywhere("UnityEngine", "Transform", msg);
    if (!g_ns_transform_cls) return false;
    g_ns_get_ls = g_api.class_get_method_from_name(g_ns_transform_cls, "get_localScale", 0);
    g_ns_set_ls = g_api.class_get_method_from_name(g_ns_transform_cls, "set_localScale", 1);
    g_ns_set_lp = g_api.class_get_method_from_name(g_ns_transform_cls, "set_localPosition", 1);
    return g_ns_set_ls && g_ns_get_ls;
}

static int32_t g_ns_le_length_off = -1; // LeLable.Length (float)

static bool NS_SetLocalScale(void* transform, float s) {
    if (!transform || !g_ns_set_ls || !g_api.runtime_invoke) return false;
    UnityVec3NS v{ s, s, s };
    void* params[1] = { &v };
    Il2CppException* exc = nullptr;
    g_api.runtime_invoke(g_ns_set_ls, transform, params, &exc);
    return exc == nullptr;
}

static bool NS_SetLocalPositionX(void* transform, float x, float y, float z) {
    if (!transform || !g_api.runtime_invoke) return false;
    if (!g_ns_set_lp) {
        if (!g_ns_transform_cls) return false;
        g_ns_set_lp = g_api.class_get_method_from_name(g_ns_transform_cls, "set_localPosition", 1);
    }
    if (!g_ns_set_lp) return false;
    UnityVec3NS v{ x, y, z };
    void* params[1] = { &v };
    Il2CppException* exc = nullptr;
    g_api.runtime_invoke(g_ns_set_lp, transform, params, &exc);
    return exc == nullptr;
}

static bool NS_ResolveLeLength(void* le) {
    if (g_ns_le_length_off >= 0) return true;
    if (!le || !g_api.object_get_class) return false;
    Il2CppClass* cls = g_api.object_get_class((Il2CppObject*)le);
    if (!cls) return false;
    Il2CppField* f = g_api.class_get_field_from_name(cls, "Length");
    if (!f) return false;
    int32_t off = g_api.field_get_offset(f);
    if (off < 0) return false;
    g_ns_le_length_off = off;
    return true;
}

static bool NS_ResolveNameTextOff(void* tns) {
    if (g_ns_nametext_off >= 0) return true;
    if (!tns || !g_api.object_get_class) return false;
    Il2CppClass* cls = g_api.object_get_class((Il2CppObject*)tns);
    if (!cls) return false;
    Il2CppField* f = g_api.class_get_field_from_name(cls, "NameText");
    if (!f) return false;
    int32_t off = g_api.field_get_offset(f);
    if (off < 0) return false;
    g_ns_nametext_off = off;
    return true;
}

static bool NS_ResolveLeSelfTF(void* le) {
    if (g_ns_le_selftf_off >= 0) return true;
    if (!le || !g_api.object_get_class) return false;
    Il2CppClass* cls = g_api.object_get_class((Il2CppObject*)le);
    if (!cls) return false;
    Il2CppField* f = g_api.class_get_field_from_name(cls, "SelfTF");
    if (!f) return false;
    int32_t off = g_api.field_get_offset(f);
    if (off < 0) return false;
    g_ns_le_selftf_off = off;
    return true;
}

static void NS_ApplyOneTns(void* tns, float nameLocalScale) {
    if (!tns || !LooksLikeValidManagedObject(tns)) return;
    if (!NS_ResolveNameTextOff(tns)) { g_ns_last_fail++; return; }
    void* nameText = *(void**)((char*)tns + g_ns_nametext_off);
    if (!nameText || !LooksLikeValidManagedObject(nameText)) { g_ns_last_fail++; return; }
    if (!NS_ResolveLeSelfTF(nameText)) { g_ns_last_fail++; return; }
    void* leTF = *(void**)((char*)nameText + g_ns_le_selftf_off);
    if (!leTF || !LooksLikeValidManagedObject(leTF)) { g_ns_last_fail++; return; }

    // 1) 缩放文字节点
    if (!NS_SetLocalScale(leTF, nameLocalScale)) {
        g_ns_last_fail++;
        return;
    }

    // 2) 位置补偿：游戏用 pos.x = -Length*0.5 做居中（未缩放）
    //    缩放后网格从 0 延伸到 Length*s，中心偏移 Length/2*(s-1)
    //    正确居中：pos.x = -Length * 0.5 * s
    float length = 0.f;
    if (NS_ResolveLeLength(nameText) && g_ns_le_length_off >= 0) {
        length = *(float*)((char*)nameText + g_ns_le_length_off);
    }
    if (length > 0.01f) {
        float x = -length * 0.5f * nameLocalScale;
        // y/z 保持 0（与常规 NameText 一致；游戏有时只改 x）
        if (!NS_SetLocalPositionX(leTF, x, 0.f, 0.f)) {
            // 位置失败不算整次失败，缩放已生效
        }
    }

    g_ns_last_applied++;
}

// coeff: 用户滑条值，默认 1.875；<=0 表示强制 NameText.localScale=1 并返回
void GameCall_TickNameScale(float coeff) {
    g_ns_tick_count++;
    g_ns_last_applied = 0;
    g_ns_last_fail = 0;

    // restore mode
    bool restore = (coeff <= 0.0001f);
    float nameLocal = 1.0f;
    if (!restore) {
        if (coeff < 0.3f) coeff = 0.3f;
        if (coeff > 8.0f) coeff = 8.0f;
        // 根节点仍是 *1.875，子节点 * (coeff/1.875) → 世界 *coeff
        nameLocal = coeff / kGameNameScaleCoeff;
    }

    if (!g_call_mutex.try_lock()) {
        return;
    }

    std::string msg;
    if (!EnsureIl2CppApi(msg)) {
        g_call_mutex.unlock();
        return;
    }
    Il2CppDomain* domain = GetDomain(msg);
    if (!domain) { g_call_mutex.unlock(); return; }
    AttachCurrentThreadIfNeeded(domain);

    if (!NS_ResolveTransform()) {
        g_call_mutex.unlock();
        return;
    }

    if (!g_game_core_class) {
        g_game_core_class = FindClassEverywhere("", "GameCoreCenter", msg);
    }
    if (!g_playerdic_field && g_game_core_class)
        g_playerdic_field = g_api.class_get_field_from_name(g_game_core_class, "PlayerDic");
    if (!g_playerbase_class)
        g_playerbase_class = FindClassEverywhere("", "PlayerBase", msg);
    if (g_namelables_offset < 0 && g_playerbase_class) {
        Il2CppField* f = g_api.class_get_field_from_name(g_playerbase_class, "NameLables");
        if (f) {
            int32_t off = g_api.field_get_offset(f);
            if (off > 0) g_namelables_offset = off;
        }
        if (g_namelables_offset < 0) g_namelables_offset = 72;
    }

    void* gcc = GetGameCoreInstance(msg);
    if (!gcc || !g_playerdic_field || !g_playerbase_class) {
        g_call_mutex.unlock();
        return;
    }

    Il2CppObject* playerDic = nullptr;
    int32_t pdOff = g_api.field_get_offset(g_playerdic_field);
    if (pdOff >= 0) playerDic = *(Il2CppObject**)((char*)gcc + pdOff);
    if (!playerDic || !LooksLikeValidManagedObject(playerDic)) {
        g_call_mutex.unlock();
        return;
    }

    Il2CppClass* dictClass = g_api.object_get_class(playerDic);
    if (!g_dict_get_count_method && dictClass)
        g_dict_get_count_method = g_api.class_get_method_from_name(dictClass, "get_Count", 0);
    if (!g_dict_get_values_method && dictClass)
        g_dict_get_values_method = g_api.class_get_method_from_name(dictClass, "get_Values", 0);
    if (!g_dict_get_count_method || !g_dict_get_values_method || !g_api.array_new) {
        g_call_mutex.unlock();
        return;
    }

    Il2CppException* exc = nullptr;
    Il2CppObject* countObj = g_api.runtime_invoke(g_dict_get_count_method, playerDic, nullptr, &exc);
    if (exc || !countObj) { g_call_mutex.unlock(); return; }
    int dictCount = *(int*)((char*)countObj + 2 * sizeof(void*));
    if (dictCount <= 0) { g_call_mutex.unlock(); return; }
    if (dictCount > 128) dictCount = 128;

    exc = nullptr;
    Il2CppObject* valuesObj = g_api.runtime_invoke(g_dict_get_values_method, playerDic, nullptr, &exc);
    if (exc || !valuesObj) { g_call_mutex.unlock(); return; }

    Il2CppObject* arr = g_api.array_new(g_playerbase_class, dictCount);
    if (!arr) { g_call_mutex.unlock(); return; }

    Il2CppClass* vcClass = g_api.object_get_class(valuesObj);
    if (!g_vc_copyto_method && vcClass)
        g_vc_copyto_method = g_api.class_get_method_from_name(vcClass, "CopyTo", 2);
    if (!g_vc_copyto_method) { g_call_mutex.unlock(); return; }

    int32_t zero = 0;
    void* copyParams[2] = { arr, &zero };
    exc = nullptr;
    g_api.runtime_invoke(g_vc_copyto_method, valuesObj, copyParams, &exc);
    if (exc) { g_call_mutex.unlock(); return; }

    int32_t arrLen = *(int32_t*)((char*)arr + 24);
    if (arrLen > dictCount) arrLen = dictCount;

    int labelsTotal = 0;
    for (int i = 0; i < arrLen; ++i) {
        Il2CppObject* pb = *(Il2CppObject**)((char*)arr + 32 + (size_t)i * 8);
        if (!LooksLikeValidManagedObject(pb)) continue;
        Il2CppObject* labels = *(Il2CppObject**)((char*)pb + g_namelables_offset);
        if (!labels || !LooksLikeValidManagedObject(labels)) continue;

        if (!g_ns_list_cnt || !g_ns_list_at) {
            Il2CppClass* listCls = g_api.object_get_class(labels);
            if (!listCls) continue;
            if (!g_ns_list_cnt)
                g_ns_list_cnt = g_api.class_get_method_from_name(listCls, "get_Count", 0);
            if (!g_ns_list_at)
                g_ns_list_at = g_api.class_get_method_from_name(listCls, "get_Item", 1);
        }
        if (!g_ns_list_cnt || !g_ns_list_at) continue;

        exc = nullptr;
        Il2CppObject* cntObj = g_api.runtime_invoke(g_ns_list_cnt, labels, nullptr, &exc);
        if (exc || !cntObj) continue;
        int n = *(int*)((char*)cntObj + 2 * sizeof(void*));
        if (n < 0) n = 0;
        if (n > 24) n = 24;
        labelsTotal += n;

        for (int j = 0; j < n; ++j) {
            int32_t idx = j;
            void* ip[1] = { &idx };
            exc = nullptr;
            Il2CppObject* tns = g_api.runtime_invoke(g_ns_list_at, labels, ip, &exc);
            if (exc || !tns) continue;
            NS_ApplyOneTns(tns, nameLocal);
        }
    }

    

    g_call_mutex.unlock();
}


// ==================== 三角合球 ====================

void GameCall_StopTriangleMerge() {
    g_triangle_running = false;
}

void GameCall_TriangleMerge(std::string& outMessage) {
    if (g_triangle_running) {
        GameCall_StopTriangleMerge();
        SetMsg(outMessage, "三角合球已停止");
        SetStatus(outMessage);
        return;
    }

    // 前置检查：确保 IL2CPP 可用且方法已缓存
    std::string msg;
    if (!EnsureIl2CppApi(msg)) {
        outMessage = "三角合球失败：" + msg;
        SetStatus(outMessage);
        return;
    }
    Il2CppDomain* domain = GetDomain(msg);
    if (!domain) {
        outMessage = "三角合球失败：" + msg;
        SetStatus(outMessage);
        return;
    }
    AttachCurrentThreadIfNeeded(domain);
    if (!ResolveGameCoreMembers(msg) || !ResolveMoveMembers(msg)) {
        outMessage = "三角合球失败：" + msg;
        SetStatus(outMessage);
        return;
    }
    void* gccInstance = GetGameCoreInstance(msg);
    if (!gccInstance) {
        outMessage = "三角合球失败：" + msg;
        SetStatus(outMessage);
        return;
    }

    // 暂时关闭吐球加速，避免并发 IL2CPP 调用导致崩溃
    bool wasSpitAccel = g_spitball_accelerate;
    g_spitball_accelerate = false;

    g_triangle_running = true;
    SetMsg(outMessage, "三角合球已启动");
    SetStatus(outMessage);

    void* gcc = gccInstance;
    Il2CppDomain* dom = domain;

    std::thread([gcc, dom, wasSpitAccel]() {
        AttachCurrentThreadIfNeeded(dom);

        auto sleep = [](int ms) {
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        };

        auto doMoveVec = [](void* inst, float x, float y, float z) {
            std::lock_guard<std::mutex> lock(g_call_mutex);
            UnityVector3 dir = { x, y, z };
            void* params[1] = { &dir };
            std::string m;
            RuntimeInvoke(g_move_method, inst, params, nullptr, "Move", m);
        };

        auto doDevide = [](void* inst) {
            std::lock_guard<std::mutex> lock(g_call_mutex);
            std::string m;
            RuntimeInvoke(g_send_devide_method, inst, nullptr, nullptr, "SendDevide", m);
        };

        auto doForceSendMove = [](void* inst) {
            if (!g_force_send_move_method) return;
            std::lock_guard<std::mutex> lock(g_call_mutex);
            std::string m;
            RuntimeInvoke(g_force_send_move_method, inst, nullptr, nullptr, "ForceSendMove", m);
        };

        // ── 三角合球 ──
        // 左摇杆分身 → 右摇杆分身 → 摇杆指向上方持续分身
        // 坐标系：x=左右, y=上下（上为正）, z=前后

        // Step 1: 左移摇杆 + 分身
        doMoveVec(gcc, -1.0f, 0.0f, 0.0f);  // 左
        doForceSendMove(gcc);
        sleep(80);
        doDevide(gcc);
        sleep(80);

        // Step 2: 右移摇杆 + 分身
        doMoveVec(gcc, 1.0f, 0.0f, 0.0f);   // 右
        doForceSendMove(gcc);
        sleep(80);
        doDevide(gcc);
        sleep(80);

        // Step 3: 摇杆指向上方，持续分身
        doMoveVec(gcc, 0.0f, 1.0f, 0.0f);   // 上
        doForceSendMove(gcc);
        sleep(50);

        for (int i = 0; i < 20 && g_triangle_running; ++i) {
            doDevide(gcc);
            sleep(50);
        }

        // 停止摇杆
        doMoveVec(gcc, 0.0f, 0.0f, 0.0f);
        doForceSendMove(gcc);

        g_triangle_running = false;
        g_spitball_accelerate = wasSpitAccel;
        SetStatus("三角合球完成");
    }).detach();
}

bool GameCall_InvokeMove(float x, float y, float z, std::string& outMessage) {
    return InvokeMoveInternal(x, y, z, false, outMessage);
}

bool GameCall_StopMove(std::string& outMessage) {
    return InvokeMoveInternal(0.0f, 0.0f, 0.0f, true, outMessage);
}

std::string GameCall_GetLastStatus() {
    std::lock_guard<std::mutex> lock(g_status_mutex);
    return g_last_status;
}

std::string GameCall_GetHookDebugInfo() {
    Il2CppDomain* domain = nullptr;
    if (g_api.domain_get) domain = g_api.domain_get();

    char buf[1024];
    snprintf(buf, sizeof(buf),
             "runtime_invoke模式 | dl=%p xdl=%p domain=%p asm=%zu image=%s\nGCC: class=%p get_instance=%p SendDevide=%p Move=%p ForceSendMove=%p SetFreeTypeFlag=%p FreeTypeClick=%p\nNU: class=%p get_instance=%p ReqFreeType=%p\ninstance=%p lastMove=(%.2f,%.2f,%.2f) thread=%p attach=%d splitClick=%d moveCall=%d ok=%d fail=%d",
             g_api.dl_handle,
             g_api.xdl_handle,
             domain,
             g_last_assembly_count,
             g_found_image_name.empty() ? "-" : g_found_image_name.c_str(),
             g_game_core_class,
             g_get_instance_method,
             g_send_devide_method,
             g_move_method,
             g_force_send_move_method,
             g_set_free_type_flag_method,
             g_free_type_click_method,
             g_network_updater_class,
             g_nu_get_instance_method,
             g_nu_req_free_type_method,
             g_last_instance,
             g_last_move_dir.x,
             g_last_move_dir.y,
             g_last_move_dir.z,
             g_last_thread,
             g_attach_count.load(),
             g_button_click_count.load(),
             g_move_call_count.load(),
             g_success_count.load(),
             g_fail_count.load());
    return std::string(buf);
}
