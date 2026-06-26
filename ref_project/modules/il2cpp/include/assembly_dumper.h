#ifndef ASSEMBLY_DUMPER_H
#define ASSEMBLY_DUMPER_H

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>
#include "il2cpp-class.h"
#include "logger.h"

// =====================================================================
// AssemblyDumper v2 — 安全早期 hook
//
// 问题：hook il2cpp_runtime_invoke 在 PAC 设备上崩溃 (SIGILL)
// 解决：不 hook 高频/复杂函数，改为：
//
// 方案 A: hook il2cpp_image_get_name — 每次获取 image 名字时触发，
//         可以检测到程序集加载完成的时机，间接捕获信息
//
// 方案 B: hook il2cpp_domain_get_assemblies — 程序集枚举时触发，
//         在这个时机 dump 所有已加载的程序集 metadata
//
// 方案 C (核心): hook 文件 IO 层 (open/read) — 截获游戏读取
//         加密 DLL 文件的过程。但这太底层了。
//
// 方案 D (最终采用): 在 il2cpp 完整初始化后，通过 IL2CPP API
//         遍历所有已加载的程序集，对热更 DLL 做二次 dump。
//         热更 DLL 的 metadata 在运行时是解密的，所以 dump.cs
//         能拿到方法签名。而如果想拿原始 DLL 字节：
//         hook Assembly.Load 的 **具体 methodPointer**（不用
//         runtime_invoke），在 il2cpp 初始化后安装。
//
// 兼容 PAC/BTI: 只 hook 导出的 C 函数符号，不 hook 内部函数
// =====================================================================

struct CapturedAssembly {
    std::string name;
    size_t size;
    uint64_t timestamp;
};

class AssemblyDumper {
private:
    std::atomic<bool> hooked{false};
    std::atomic<bool> late_hooked{false};
    std::string dump_dir;

    std::vector<CapturedAssembly> captured;
    std::mutex mtx;

    // ===== 方案1: 早期 hook (安全符号) =====
    // hook il2cpp_class_from_name — 每次按名字查找类时触发
    // HybridCLR 加载 DLL 后会注册类，之后一定会调用 class_from_name
    // 我们用它来检测 "新程序集出现了"
    static void* original_class_from_name;
    static void* hooked_class_from_name_func(void* image, const char* ns, const char* name);

    // ===== 方案2: 晚期 hook (il2cpp 初始化后) =====
    // 直接 hook Assembly.Load(byte[]) / Assembly.Load(byte[], byte[]) 
    // 的 methodPointer
    static void* original_load_bytes1;
    static void* original_load_bytes2;
    static void* hooked_load_bytes1_func(void* thisPtr, void* bytesArray, const MethodInfo* method);
    static void* hooked_load_bytes2_func(void* thisPtr, void* bytesArray, void* pdbArray, const MethodInfo* method);

    // 工具
    static bool looksLikePE(const uint8_t* data, size_t len);
    static void saveBytesToFile(const uint8_t* data, size_t size,
                                const std::string& dir, const std::string& hint);
    static bool extractAndSaveByteArray(void* il2cppArray, const std::string& dir,
                                         const std::string& hint);

public:
    AssemblyDumper() = default;
    ~AssemblyDumper() = default;

    // 早期安装 — libil2cpp.so 加载后立即调用
    // 只 hook 安全的导出符号 (il2cpp_class_from_name)
    bool installEarly(void* il2cppHandle, const std::string& dumpDir);

    // 晚期安装 — il2cpp 完整初始化后调用
    // hook Assembly.Load(byte[]) 的具体 methodPointer
    // 需要传入 IL2CPPRuntimeTool 找到的方法指针
    bool installLate(void* loadBytes1_addr, void* loadBytes2_addr);

    bool isInstalled() const { return hooked.load(); }
    bool isLateInstalled() const { return late_hooked.load(); }
    size_t getCapturedCount();
    std::vector<CapturedAssembly> getCapturedList();
    const std::string& getDumpDir() const { return dump_dir; }
};

extern AssemblyDumper g_assembly_dumper;

#endif