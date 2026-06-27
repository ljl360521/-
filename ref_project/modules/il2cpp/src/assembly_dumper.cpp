#include "assembly_dumper.h"
#include "xdl.h"
#include "dobby.h"
#include <cstring>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <sys/stat.h>
#include <unordered_set>

AssemblyDumper g_assembly_dumper;
void* AssemblyDumper::original_class_from_name = nullptr;
void* AssemblyDumper::original_load_bytes1 = nullptr;
void* AssemblyDumper::original_load_bytes2 = nullptr;

// 已见过的 image 指针，用于检测新程序集
static std::unordered_set<void*> g_seen_images;
static std::mutex g_seen_mutex;

// 用于从 il2cpp API 获取 image name
static const char* (*g_il2cpp_image_get_name)(void*) = nullptr;

// =====================================================================
// 工具函数
// =====================================================================

bool AssemblyDumper::looksLikePE(const uint8_t* data, size_t len) {
    if (!data || len < 64) return false;
    if (data[0] != 'M' || data[1] != 'Z') return false;
    return true;
}

void AssemblyDumper::saveBytesToFile(const uint8_t* data, size_t size,
                                      const std::string& dir, const std::string& hint) {
    if (!data || size == 0) return;

    time_t now = time(nullptr);
    struct tm tm_info;
    localtime_r(&now, &tm_info);

    std::stringstream ss;
    ss << dir;
    if (!dir.empty() && dir.back() != '/') ss << "/";
    ss << "dumped_";
    if (!hint.empty()) ss << hint << "_";
    ss << std::put_time(&tm_info, "%H%M%S") << "_" << size << ".dll";

    std::string path = ss.str();
    FILE* fp = fopen(path.c_str(), "wb");
    if (fp) {
        fwrite(data, 1, size, fp);
        fclose(fp);
        LOGI("[AsmDumper] SAVED %zu bytes -> %s", size, path.c_str());
    } else {
        LOGE("[AsmDumper] write failed: %s errno=%d", path.c_str(), errno);
    }
}

bool AssemblyDumper::extractAndSaveByteArray(void* il2cppArray, const std::string& dir,
                                               const std::string& hint) {
    if (!il2cppArray) return false;

    auto* base = (uint8_t*)il2cppArray;
    if ((uintptr_t)base < 0x10000) return false;

    // 验证 klass 指针
    void* klass = *(void**)base;
    if (!klass || (uintptr_t)klass < 0x10000) return false;

    // Il2CppArray layout (arm64):
    // +0x00 klass, +0x08 monitor, +0x10 bounds, +0x18 max_length, +0x20 data
    size_t length = *(size_t*)(base + 0x18);

    if (length < 64 || length > 100 * 1024 * 1024) return false;

    uint8_t* data = base + 0x20;

    if (!looksLikePE(data, length)) return false;

    LOGI("[AsmDumper] *** PE CAPTURED *** hint=%s, size=%zu", hint.c_str(), length);
    saveBytesToFile(data, length, dir, hint);

    {
        std::lock_guard<std::mutex> lock(g_assembly_dumper.mtx);
        CapturedAssembly cap;
        cap.name = hint;
        cap.size = length;
        cap.timestamp = (uint64_t)time(nullptr);
        g_assembly_dumper.captured.push_back(std::move(cap));
    }

    return true;
}

// =====================================================================
// 方案1: 早期 hook — il2cpp_class_from_name
//
// 这是一个简单的 C 导出函数：
//   Il2CppClass* il2cpp_class_from_name(Il2CppImage*, const char*, const char*)
//
// HybridCLR 加载热更 DLL 后，游戏代码会通过 class_from_name
// 查找类，我们在这里记录出现的 image 指针。
// 当发现新的 image 时，说明有新程序集被加载了。
// =====================================================================

void* AssemblyDumper::hooked_class_from_name_func(void* image, const char* ns, const char* name) {
    // 调用原始函数
    typedef void* (*OrigFunc)(void*, const char*, const char*);
    void* result = ((OrigFunc)original_class_from_name)(image, ns, name);

    if (image) {
        bool isNew = false;
        {
            std::lock_guard<std::mutex> lock(g_seen_mutex);
            if (g_seen_images.find(image) == g_seen_images.end()) {
                g_seen_images.insert(image);
                isNew = true;
            }
        }

        if (isNew && g_il2cpp_image_get_name) {
            const char* imgName = g_il2cpp_image_get_name(image);
            if (imgName) {
                LOGI("[AsmDumper] NEW assembly detected: %s (via class_from_name: %s.%s)",
                     imgName, ns ? ns : "", name ? name : "");
            }
        }
    }

    return result;
}

bool AssemblyDumper::installEarly(void* il2cppHandle, const std::string& dumpDir) {
    if (hooked.load()) return true;
    if (!il2cppHandle) return false;

    dump_dir = dumpDir;
    mkdir(dump_dir.c_str(), 0777);

    // 获取 image_get_name 用于日志
    g_il2cpp_image_get_name = (const char*(*)(void*))
        xdl_sym(il2cppHandle, "il2cpp_image_get_name", nullptr);

    // hook il2cpp_class_from_name — 安全的 C 导出函数
    void* cfn_addr = xdl_sym(il2cppHandle, "il2cpp_class_from_name", nullptr);
    if (!cfn_addr) {
        LOGE("[AsmDumper] il2cpp_class_from_name not found");
        return false;
    }

    int ret = DobbyHook(cfn_addr, (void*)hooked_class_from_name_func, &original_class_from_name);
    if (ret != 0) {
        LOGE("[AsmDumper] DobbyHook class_from_name failed, error=%d", ret);
        return false;
    }

    hooked.store(true);
    LOGI("[AsmDumper] early hook installed (class_from_name at %p)", cfn_addr);
    LOGI("[AsmDumper] dump dir: %s", dump_dir.c_str());
    return true;
}

// =====================================================================
// 方案2: 晚期 hook — Assembly.Load(byte[]) 的 methodPointer
//
// IL2CPP 中 C# 方法被编译为 native 函数，调用约定 (arm64):
//   static 方法: x0=arg0, x1=arg1, ..., 最后一个参数=MethodInfo*
//   instance 方法: x0=this, x1=arg0, ..., 最后一个参数=MethodInfo*
//
// Assembly.Load(byte[]) 是 static:
//   x0 = byte[] array
//   x1 = MethodInfo*
//
// Assembly.Load(byte[], byte[]) 是 static:
//   x0 = byte[] dllBytes
//   x1 = byte[] pdbBytes  
//   x2 = MethodInfo*
//
// 注意: HybridCLR 可能用不同签名，但核心一定有 byte[] 参数
// =====================================================================

void* AssemblyDumper::hooked_load_bytes1_func(void* thisPtr, void* bytesArray, const MethodInfo* method) {
    // IL2CPP static 方法: thisPtr 实际上是第一个参数 (byte[])
    // bytesArray 实际上是 MethodInfo*
    // 因为 static 方法没有 this
    void* actualBytes = thisPtr;  // x0 = byte[]

    LOGI("[AsmDumper] Assembly.Load(byte[]) called!");

    // 先截获
    extractAndSaveByteArray(actualBytes, g_assembly_dumper.dump_dir, "Load1");

    // 调用原始
    typedef void* (*OrigFunc)(void*, void*, const MethodInfo*);
    return ((OrigFunc)original_load_bytes1)(thisPtr, bytesArray, method);
}

void* AssemblyDumper::hooked_load_bytes2_func(void* thisPtr, void* bytesArray, void* pdbArray, const MethodInfo* method) {
    // static: x0=byte[] dll, x1=byte[] pdb, x2=MethodInfo*
    void* actualDllBytes = thisPtr;  // x0
    void* actualPdbBytes = bytesArray; // x1

    LOGI("[AsmDumper] Assembly.Load(byte[], byte[]) called!");

    extractAndSaveByteArray(actualDllBytes, g_assembly_dumper.dump_dir, "Load2_dll");
    extractAndSaveByteArray(actualPdbBytes, g_assembly_dumper.dump_dir, "Load2_pdb");

    typedef void* (*OrigFunc)(void*, void*, void*, const MethodInfo*);
    return ((OrigFunc)original_load_bytes2)(thisPtr, bytesArray, pdbArray, method);
}

bool AssemblyDumper::installLate(void* loadBytes1_addr, void* loadBytes2_addr) {
    if (late_hooked.load()) return true;

    bool any = false;

    if (loadBytes1_addr) {
        int ret = DobbyHook(loadBytes1_addr, (void*)hooked_load_bytes1_func, &original_load_bytes1);
        if (ret == 0) {
            LOGI("[AsmDumper] Assembly.Load(byte[]) hooked at %p", loadBytes1_addr);
            any = true;
        } else {
            LOGE("[AsmDumper] hook Load(byte[]) failed, error=%d", ret);
        }
    }

    if (loadBytes2_addr) {
        int ret = DobbyHook(loadBytes2_addr, (void*)hooked_load_bytes2_func, &original_load_bytes2);
        if (ret == 0) {
            LOGI("[AsmDumper] Assembly.Load(byte[],byte[]) hooked at %p", loadBytes2_addr);
            any = true;
        } else {
            LOGE("[AsmDumper] hook Load(byte[],byte[]) failed, error=%d", ret);
        }
    }

    if (any) late_hooked.store(true);
    return any;
}

size_t AssemblyDumper::getCapturedCount() {
    std::lock_guard<std::mutex> lock(mtx);
    return captured.size();
}

std::vector<CapturedAssembly> AssemblyDumper::getCapturedList() {
    std::lock_guard<std::mutex> lock(mtx);
    return captured;
}