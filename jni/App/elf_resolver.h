// =============================================================================
// elf_resolver.h — 绕过 dlopen 命名空间隔离的符号查找
//
// 问题: Android 7+ 的 linker 命名空间隔离, 外部注入的 .so 用 dlopen("libil2cpp.so")
//       会失败 ("library not found"), 即使游戏进程已经加载了该库。
//
// 方案: 直接从 /proc/self/maps 找到已加载库的基址, 然后解析 ELF 文件的符号表。
//       由于 Android linker 只映射 PT_LOAD 段, 节头表 (.symtab / .shstrtab) 通常
//       不在内存中 — 因此 LookupSymbolMemory() 会失败。改用 LookupSymbolFile()
//       从磁盘读取 .so 文件解析, 稳定可用。
//
// 这样无需 dlopen/dlsym, 直接拿到游戏已加载的 libil2cpp.so 里的函数地址。
// =============================================================================

#pragma once

#include <jni.h>
#include <android/log.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cerrno>
#include "app_log.h"
#include "xdl.h"

#define ELF_LOG_TAG "ELFResolver"
#define ELF_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  ELF_LOG_TAG, __VA_ARGS__)
#define ELF_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, ELF_LOG_TAG, __VA_ARGS__)
#define ELF_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, ELF_LOG_TAG, __VA_ARGS__)

// 模块级标记: 控制符号转储只执行一次 (避免每帧重试时反复打印)
// 定义在头文件中, 所有 include 此头文件的编译单元共享 (inline 变量 C++17)
namespace ELFResolver {
    inline bool g_symbolsDumped = false;
}

namespace ELFResolver {

// ELF64 类型定义 (Android arm64 是 ELF64)
typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} Elf64_Shdr;

typedef struct {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Elf64_Sym;

// SHT 类型常量
#define SHT_DYNSYM 11
#define SHT_SYMTAB  2

// 程序头类型
#define PT_LOAD 1
#define PT_DYNAMIC 2

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

// -----------------------------------------------------------------------------
// 从 /proc/self/maps 查找已加载库的基址
// 返回库在内存中的起始加载地址 (第一个映射段的地址), 0 表示未找到
// -----------------------------------------------------------------------------
inline uintptr_t FindLibraryBase(const char* libName) {
    APP_LOGI("ELF", "FindLibraryBase(%s) — 扫描 /proc/self/maps...", libName);
    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) {
        ELF_LOGE("无法打开 /proc/self/maps");
        APP_LOGE("ELF", "无法打开 /proc/self/maps (errno=%d)", errno);
        return 0;
    }

    char line[512];
    uintptr_t base = 0;
    bool found = false;
    int matchCount = 0;

    while (fgets(line, sizeof(line), fp)) {
        // 查找包含 libName 的行
        if (strstr(line, libName) == nullptr) continue;
        matchCount++;

        // 解析行首的地址范围: "start-end perms offset ..."
        uintptr_t start, end;
        char perms[8];
        if (sscanf(line, "%lx-%lx %7s", &start, &end, perms) >= 3) {
            // 找到第一个该库的映射段 (通常是 r--p 的 ELF header 段)
            base = start;
            found = true;
            // 去掉末尾换行
            char* nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            APP_LOGI("ELF", "  找到 %s @ 0x%lx (perms=%s) — %s", libName, start, perms, line);
            ELF_LOGD("找到 %s @ 0x%lx (perms=%s)", libName, start, perms);
            break;
        }
    }

    fclose(fp);
    if (!found) {
        ELF_LOGE("在 /proc/self/maps 中未找到 %s", libName);
        APP_LOGE("ELF", "在 /proc/self/maps 中未找到 %s (匹配行数=%d)", libName, matchCount);
        APP_LOGE("ELF", "  → 可能游戏尚未加载该库, 或库名拼写不同");
    } else {
        APP_LOGI("ELF", "FindLibraryBase 返回 base=0x%lx (共 %d 行匹配)", base, matchCount);
    }
    return base;
}

// -----------------------------------------------------------------------------
// 获取已加载库的 ELF 文件路径 (从 /proc/self/maps 找第一行的路径)
// -----------------------------------------------------------------------------
inline std::string FindLibraryPath(const char* libName) {
    APP_LOGI("ELF", "FindLibraryPath(%s) — 提取磁盘路径...", libName);
    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) return "";

    char line[512];
    std::string path;

    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, libName) == nullptr) continue;

        // 提取路径 (行的最后一列)
        // 格式: addr perms offset dev inode pathname
        char* p = line;
        // 跳过 5 个字段
        int spaces = 0;
        while (*p && spaces < 5) {
            if (*p == ' ') {
                spaces++;
                while (*p == ' ') p++;
            } else {
                p++;
            }
        }
        // 现在 p 指向 pathname (可能有前导空格已跳过)
        // 去掉末尾换行
        char* nl = strchr(p, '\n');
        if (nl) *nl = '\0';
        if (*p) {
            path = p;
            APP_LOGI("ELF", "  磁盘路径: %s", path.c_str());
            break;
        }
    }

    fclose(fp);
    if (path.empty()) {
        APP_LOGW("ELF", "FindLibraryPath 未找到 %s 的磁盘路径", libName);
    }
    return path;
}

// -----------------------------------------------------------------------------
// 在 ELF 内存映像中查找符号 (兼容方式 — 节头若被剥离则失败)
// base: 库的加载基址 (从 FindLibraryBase 获取)
// symName: 要查找的符号名 (如 "il2cpp_domain_get")
// 返回符号的运行时地址, 0 表示未找到
// -----------------------------------------------------------------------------
inline void* LookupSymbolMemory(uintptr_t base, const char* symName) {
    if (base == 0 || !symName) return nullptr;

    APP_LOGI("ELF", "LookupSymbolMemory(base=0x%lx, sym=%s)", base, symName);

    // ELF header 在基址处
    Elf64_Ehdr* ehdr = (Elf64_Ehdr*)base;

    // 验证 ELF magic
    if (memcmp(ehdr->e_ident, "\x7f""ELF", 4) != 0) {
        ELF_LOGE("无效的 ELF magic (base=0x%lx)", base);
        APP_LOGE("ELF", "  无效的 ELF magic (base=0x%lx) — 不是 ELF 文件?", base);
        return nullptr;
    }
    APP_LOGI("ELF", "  ELF magic OK, e_shoff=0x%lx, e_shnum=%d, e_shstrndx=%d",
        (unsigned long)ehdr->e_shoff, ehdr->e_shnum, ehdr->e_shstrndx);

    // section header table
    if (ehdr->e_shoff == 0 || ehdr->e_shnum == 0) {
        ELF_LOGE("无 section header (e_shoff=0 — Android linker 不映射节头)");
        APP_LOGE("ELF", "  ✗ e_shoff=0 或 e_shnum=0 — 节头表未加载到内存 (Android 常见情况)");
        APP_LOGE("ELF", "    → 改用 LookupSymbolFile() 从磁盘读取 .so 文件");
        return nullptr;
    }

    Elf64_Shdr* shdrs = (Elf64_Shdr*)(base + ehdr->e_shoff);

    // 找 .shstrtab (section name string table)
    if (ehdr->e_shstrndx >= ehdr->e_shnum) {
        ELF_LOGE("e_shstrndx 无效");
        APP_LOGE("ELF", "  e_shstrndx=%d >= e_shnum=%d (无效)", ehdr->e_shstrndx, ehdr->e_shnum);
        return nullptr;
    }
    Elf64_Shdr* shstrtab = &shdrs[ehdr->e_shstrndx];
    (void)shstrtab; // 当前实现不使用 .shstrtab, 仅做校验

    // 遍历所有 section, 查找 DYNSYM 和 SYMTAB
    int dynsymCount = 0, symtabCount = 0;
    for (uint16_t i = 0; i < ehdr->e_shnum; i++) {
        Elf64_Shdr* sh = &shdrs[i];

        if (sh->sh_type != SHT_DYNSYM && sh->sh_type != SHT_SYMTAB) continue;

        // 符号表
        Elf64_Sym* syms = (Elf64_Sym*)(base + sh->sh_offset);
        size_t symCount = sh->sh_size / sizeof(Elf64_Sym);

        // 关联的字符串表
        if (sh->sh_link >= ehdr->e_shnum) continue;
        Elf64_Shdr* strSh = &shdrs[sh->sh_link];
        const char* symStrtab = (const char*)(base + strSh->sh_offset);

        const char* secType = (sh->sh_type == SHT_DYNSYM) ? "DYNSYM" : "SYMTAB";
        if (sh->sh_type == SHT_DYNSYM) dynsymCount++; else symtabCount++;
        APP_LOGI("ELF", "  节 #%d type=%s, 符号数=%zu", i, secType, symCount);

        for (size_t s = 0; s < symCount; s++) {
            Elf64_Sym* sym = &syms[s];
            if (sym->st_name == 0) continue;

            const char* name = symStrtab + sym->st_name;
            if (strcmp(name, symName) == 0) {
                // st_value 是相对基址的偏移 (对于共享库)
                // 运行时地址 = base + st_value
                void* addr = (void*)(base + sym->st_value);
                ELF_LOGD("找到符号 %s @ 0x%lx (base+0x%lx)",
                    symName, (uintptr_t)addr, sym->st_value);
                APP_LOGI("ELF", "  ✓ 内存查找: 找到符号 %s @ 0x%lx (base+0x%lx)",
                    symName, (uintptr_t)addr, (unsigned long)sym->st_value);
                return addr;
            }
        }
    }

    APP_LOGW("ELF", "  ✗ 内存查找未找到符号 %s (扫描了 %d 个 DYNSYM + %d 个 SYMTAB 节)",
        symName, dynsymCount, symtabCount);
    ELF_LOGD("未找到符号 %s", symName);
    return nullptr;
}

// -----------------------------------------------------------------------------
// 从磁盘读取 .so 文件解析符号 (稳定方式 — 不依赖内存中的节头)
// libPath: .so 文件在磁盘上的路径 (从 FindLibraryPath 获取)
// base: 库在内存中的加载基址 (用于把 st_value 转换为运行时地址)
// symName: 要查找的符号名
// 返回符号的运行时地址, nullptr 表示未找到
// -----------------------------------------------------------------------------
inline void* LookupSymbolFile(const std::string& libPath, uintptr_t base, const char* symName) {
    if (libPath.empty() || !symName) return nullptr;

    APP_LOGI("ELF", "LookupSymbolFile(path=%s, base=0x%lx, sym=%s)", libPath.c_str(), base, symName);
    FILE* fp = fopen(libPath.c_str(), "rb");
    if (!fp) {
        APP_LOGE("ELF", "  无法打开文件 %s (errno=%d)", libPath.c_str(), errno);
        return nullptr;
    }

    // 读取 ELF header
    Elf64_Ehdr ehdr;
    if (fread(&ehdr, sizeof(ehdr), 1, fp) != 1) {
        APP_LOGE("ELF", "  读取 ELF header 失败");
        fclose(fp);
        return nullptr;
    }

    if (memcmp(ehdr.e_ident, "\x7f""ELF", 4) != 0) {
        APP_LOGE("ELF", "  无效的 ELF magic");
        fclose(fp);
        return nullptr;
    }
    APP_LOGI("ELF", "  ELF header OK, e_shoff=0x%lx, e_shnum=%d",
        (unsigned long)ehdr.e_shoff, ehdr.e_shnum);

    if (ehdr.e_shoff == 0 || ehdr.e_shnum == 0) {
        APP_LOGE("ELF", "  ✗ .so 文件已 strip 节头 (e_shoff=0) — 无法用此方式查找");
        fclose(fp);
        return nullptr;
    }

    // 读取所有 section headers
    std::vector<Elf64_Shdr> shdrs(ehdr.e_shnum);
    if (fseek(fp, (long)ehdr.e_shoff, SEEK_SET) != 0 ||
        fread(shdrs.data(), sizeof(Elf64_Shdr), ehdr.e_shnum, fp) != ehdr.e_shnum) {
        APP_LOGE("ELF", "  读取 section headers 失败");
        fclose(fp);
        return nullptr;
    }

    // 遍历所有 section, 查找 DYNSYM 和 SYMTAB
    int dynsymCount = 0, symtabCount = 0;
    void* result = nullptr;

    for (uint16_t i = 0; i < ehdr.e_shnum && !result; i++) {
        Elf64_Shdr* sh = &shdrs[i];
        if (sh->sh_type != SHT_DYNSYM && sh->sh_type != SHT_SYMTAB) continue;

        // 关联的字符串表
        if (sh->sh_link >= ehdr.e_shnum) continue;
        Elf64_Shdr* strSh = &shdrs[sh->sh_link];

        // 读取字符串表
        std::vector<char> strtab(strSh->sh_size);
        if (fseek(fp, (long)strSh->sh_offset, SEEK_SET) != 0 ||
            fread(strtab.data(), 1, strSh->sh_size, fp) != strSh->sh_size) {
            APP_LOGW("ELF", "  读取字符串表 #%u 失败, 跳过", (unsigned)sh->sh_link);
            continue;
        }

        // 读取符号表
        size_t symCount = sh->sh_size / sizeof(Elf64_Sym);
        std::vector<Elf64_Sym> syms(symCount);
        if (fseek(fp, (long)sh->sh_offset, SEEK_SET) != 0 ||
            fread(syms.data(), sizeof(Elf64_Sym), symCount, fp) != symCount) {
            APP_LOGW("ELF", "  读取符号表 #%u 失败, 跳过", i);
            continue;
        }

        const char* secType = (sh->sh_type == SHT_DYNSYM) ? "DYNSYM" : "SYMTAB";
        if (sh->sh_type == SHT_DYNSYM) dynsymCount++; else symtabCount++;
        APP_LOGI("ELF", "  节 #%d type=%s, 符号数=%zu", i, secType, symCount);

        for (size_t s = 0; s < symCount; s++) {
            Elf64_Sym* sym = &syms[s];
            if (sym->st_name == 0 || sym->st_name >= strtab.size()) continue;

            const char* name = strtab.data() + sym->st_name;
            if (strcmp(name, symName) == 0) {
                void* addr = (void*)(base + sym->st_value);
                APP_LOGI("ELF", "  ✓ 文件查找: 找到符号 %s @ 0x%lx (base+0x%lx)",
                    symName, (uintptr_t)addr, (unsigned long)sym->st_value);
                result = addr;
                break;
            }
        }
    }

    fclose(fp);
    if (!result) {
        APP_LOGW("ELF", "  ✗ 文件查找未找到符号 %s (扫描 %d DYNSYM + %d SYMTAB)",
            symName, dynsymCount, symtabCount);
    }
    return result;
}

// -----------------------------------------------------------------------------
// 使用 xDL 库查找符号 (推荐 — 能搜索 .symtab, 找到 hidden/非导出符号)
// xDL 是 HexHacking 的增强 dlopen/dlsym 实现:
//   xdl_sym()  → 搜索 .dynsym (仅导出符号)
//   xdl_dsym() → 搜索 .symtab (包含 hidden 符号, 某些游戏 IL2CPP 符号在此)
//
// 关键: libil2cpp.so 的 il2cpp_* 函数可能 visibility=hidden (不在 .dynsym),
//       只在 .symtab 中 — 此时必须用 xdl_dsym()
//
// libName: 库名 (如 "libil2cpp.so")
// symName: 符号名 (如 "il2cpp_domain_get")
// 返回符号运行时地址, nullptr 表示未找到
// -----------------------------------------------------------------------------
inline void* LookupSymbolXDL(const char* libName, const char* symName) {
    APP_LOGI("ELF", "LookupSymbolXDL(lib=%s, sym=%s) — 用 xdl_dsym 搜索 .symtab", libName, symName);

    void* handle = xdl_open(libName, XDL_DEFAULT);
    if (!handle) {
        APP_LOGW("ELF", "  xdl_open(%s) 失败 — 库未加载或 xDL 内部错误", libName);
        return nullptr;
    }
    APP_LOGI("ELF", "  xdl_open(%s) 成功, handle=%p", libName, handle);

    size_t symSize = 0;

    // 优先用 xdl_dsym 搜索 .symtab (能找到 hidden 符号)
    void* addr = xdl_dsym(handle, symName, &symSize);
    if (addr) {
        APP_LOGI("ELF", "  ✓ xdl_dsym 找到 %s @ %p (size=%zu, 来自 .symtab)",
            symName, addr, symSize);
        xdl_close(handle);
        return addr;
    }
    APP_LOGW("ELF", "  xdl_dsym 未找到 %s (.symtab 中也无此符号)", symName);

    // 回退到 xdl_sym 搜索 .dynsym
    addr = xdl_sym(handle, symName, &symSize);
    if (addr) {
        APP_LOGI("ELF", "  ✓ xdl_sym 找到 %s @ %p (size=%zu, 来自 .dynsym)",
            symName, addr, symSize);
        xdl_close(handle);
        return addr;
    }

    APP_LOGW("ELF", "  xdl_sym 也未找到 %s — xDL 双查均失败", symName);
    xdl_close(handle);
    return nullptr;
}

// -----------------------------------------------------------------------------
// 转储 .dynsym 的前 N 个符号名到日志 (诊断用 — 看符号是否被重命名/混淆)
// 仅首次调用有效 (用 g_symbolsDumped 标记)
// libName: 库名
// maxCount: 最多转储的符号数量
// -----------------------------------------------------------------------------
inline void DumpDynSymbols(const char* libName, size_t maxCount = 50) {
    if (g_symbolsDumped) return;
    g_symbolsDumped = true;

    APP_LOGI("ELF", "=== 转储 %s 的前 %zu 个 DYNSYM 符号 (诊断用) ===", libName, maxCount);

    std::string libPath = FindLibraryPath(libName);
    if (libPath.empty()) {
        APP_LOGW("ELF", "  无法获取 %s 路径, 跳过转储", libName);
        return;
    }

    FILE* fp = fopen(libPath.c_str(), "rb");
    if (!fp) {
        APP_LOGW("ELF", "  无法打开 %s, 跳过转储", libPath.c_str());
        return;
    }

    Elf64_Ehdr ehdr;
    if (fread(&ehdr, sizeof(ehdr), 1, fp) != 1 ||
        memcmp(ehdr.e_ident, "\x7f""ELF", 4) != 0 ||
        ehdr.e_shoff == 0 || ehdr.e_shnum == 0) {
        APP_LOGW("ELF", "  ELF header 无效或已 strip, 跳过转储");
        fclose(fp);
        return;
    }

    std::vector<Elf64_Shdr> shdrs(ehdr.e_shnum);
    if (fseek(fp, (long)ehdr.e_shoff, SEEK_SET) != 0 ||
        fread(shdrs.data(), sizeof(Elf64_Shdr), ehdr.e_shnum, fp) != ehdr.e_shnum) {
        fclose(fp);
        return;
    }

    size_t dumped = 0;
    for (uint16_t i = 0; i < ehdr.e_shnum && dumped < maxCount; i++) {
        Elf64_Shdr* sh = &shdrs[i];
        if (sh->sh_type != SHT_DYNSYM) continue;
        if (sh->sh_link >= ehdr.e_shnum) continue;

        Elf64_Shdr* strSh = &shdrs[sh->sh_link];
        std::vector<char> strtab(strSh->sh_size);
        if (fseek(fp, (long)strSh->sh_offset, SEEK_SET) != 0 ||
            fread(strtab.data(), 1, strSh->sh_size, fp) != strSh->sh_size) continue;

        size_t symCount = sh->sh_size / sizeof(Elf64_Sym);
        std::vector<Elf64_Sym> syms(symCount);
        if (fseek(fp, (long)sh->sh_offset, SEEK_SET) != 0 ||
            fread(syms.data(), sizeof(Elf64_Sym), symCount, fp) != symCount) continue;

        APP_LOGI("ELF", "  DYNSYM 节 #%u 共 %zu 个符号, 转储前 %zu 个:", i, symCount, maxCount);

        for (size_t s = 0; s < symCount && dumped < maxCount; s++) {
            Elf64_Sym* sym = &syms[s];
            if (sym->st_name == 0 || sym->st_name >= strtab.size()) continue;
            const char* name = strtab.data() + sym->st_name;
            // 只打印以 il2cpp 开头的符号 (重点关注)
            if (strncmp(name, "il2cpp", 6) == 0) {
                APP_LOGI("ELF", "    [%zu] %s (value=0x%lx, size=%lu)",
                    dumped, name, (unsigned long)sym->st_value, (unsigned long)sym->st_size);
                dumped++;
            }
        }
        // 如果没找到 il2cpp 开头的, 打印前 N 个任意符号
        if (dumped == 0) {
            APP_LOGW("ELF", "  ⚠️ 没有以 'il2cpp' 开头的符号! 可能符号被混淆/重命名");
            APP_LOGI("ELF", "  转储前 %zu 个任意符号:", maxCount);
            for (size_t s = 0; s < symCount && dumped < maxCount; s++) {
                Elf64_Sym* sym = &syms[s];
                if (sym->st_name == 0 || sym->st_name >= strtab.size()) continue;
                const char* name = strtab.data() + sym->st_name;
                APP_LOGI("ELF", "    [%zu] %s", dumped, name);
                dumped++;
            }
        }
        break; // 只处理第一个 DYNSYM 节
    }

    APP_LOGI("ELF", "=== 符号转储完成 (共 %zu 个) ===", dumped);
    fclose(fp);
}

// -----------------------------------------------------------------------------
// 兼容旧接口: 在 ELF 内存映像中查找符号
// 查找优先级: 内存(.dynsym/.symtab) → xDL dsym(.symtab, 含 hidden) → 磁盘文件
// -----------------------------------------------------------------------------
inline void* LookupSymbol(uintptr_t base, const char* symName) {
    // 1. 先尝试内存查找 (如果节头被映射到内存了)
    void* addr = LookupSymbolMemory(base, symName);
    if (addr) return addr;

    // 2. 用 xDL 的 xdl_dsym() 查找 .symtab (能找到 hidden 符号 — 关键!)
    //    libil2cpp.so 的 il2cpp_* 函数常被 strip 出 .dynsym, 但保留在 .symtab
    APP_LOGI("ELF", "内存查找失败, 用 xDL 查找 .symtab (含 hidden 符号)...");
    addr = LookupSymbolXDL("libil2cpp.so", symName);
    if (addr) return addr;

    // 3. xDL 也失败 → 从磁盘文件读取 .so 解析 (兜底)
    APP_LOGI("ELF", "xDL 查找失败, 改用磁盘文件查找...");
    std::string libPath = FindLibraryPath("libil2cpp.so");
    if (libPath.empty()) {
        APP_LOGE("ELF", "无法获取 libil2cpp.so 的磁盘路径, 查找 %s 失败", symName);
        return nullptr;
    }
    return LookupSymbolFile(libPath, base, symName);
}

// -----------------------------------------------------------------------------
// 便捷: 一步到位查找已加载库中的符号
// libName: 库名 (如 "libil2cpp.so")
// symName: 符号名 (如 "il2cpp_domain_get")
// -----------------------------------------------------------------------------
inline void* ResolveSymbol(const char* libName, const char* symName) {
    uintptr_t base = FindLibraryBase(libName);
    if (base == 0) {
        ELF_LOGE("库 %s 未加载, 无法解析符号 %s", libName, symName);
        APP_LOGE("ELF", "库 %s 未加载, 无法解析符号 %s", libName, symName);
        return nullptr;
    }
    return LookupSymbol(base, symName);
}

} // namespace ELFResolver
