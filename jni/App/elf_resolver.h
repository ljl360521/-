// =============================================================================
// elf_resolver.h — 绕过 dlopen 命名空间隔离的符号查找
//
// 问题: Android 7+ 的 linker 命名空间隔离, 外部注入的 .so 用 dlopen("libil2cpp.so")
//       会失败 ("library not found"), 即使游戏进程已经加载了该库。
//
// 方案: 直接从 /proc/self/maps 找到已加载库的基址, 然后手动解析 ELF 文件的
//       .dynsym / .symtab 符号表, 计算符号的运行时地址。
//
// 这样无需 dlopen/dlsym, 直接拿到游戏已加载的 libil2cpp.so 里的函数地址。
// =============================================================================

#pragma once

#include <jni.h>
#include <android/log.h>
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdio>

#define ELF_LOG_TAG "ELFResolver"
#define ELF_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  ELF_LOG_TAG, __VA_ARGS__)
#define ELF_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, ELF_LOG_TAG, __VA_ARGS__)
#define ELF_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, ELF_LOG_TAG, __VA_ARGS__)

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

// -----------------------------------------------------------------------------
// 从 /proc/self/maps 查找已加载库的基址
// 返回库在内存中的起始加载地址 (第一个可执行段的地址), 0 表示未找到
// -----------------------------------------------------------------------------
inline uintptr_t FindLibraryBase(const char* libName) {
    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) {
        ELF_LOGE("无法打开 /proc/self/maps");
        return 0;
    }

    char line[512];
    uintptr_t base = 0;
    bool found = false;

    while (fgets(line, sizeof(line), fp)) {
        // 查找包含 libName 的行
        if (strstr(line, libName) == nullptr) continue;

        // 解析行首的地址范围: "start-end perms offset ..."
        uintptr_t start, end;
        char perms[8];
        if (sscanf(line, "%lx-%lx %7s", &start, &end, perms) >= 3) {
            // 找到第一个该库的映射段 (通常是 r--p 的 ELF header 段)
            base = start;
            found = true;
            ELF_LOGD("找到 %s @ 0x%lx (perms=%s)", libName, start, perms);
            break;
        }
    }

    fclose(fp);
    if (!found) {
        ELF_LOGE("在 /proc/self/maps 中未找到 %s", libName);
    }
    return base;
}

// -----------------------------------------------------------------------------
// 获取已加载库的 ELF 文件路径 (从 /proc/self/maps 找第一行的路径)
// -----------------------------------------------------------------------------
inline std::string FindLibraryPath(const char* libName) {
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
            break;
        }
    }

    fclose(fp);
    return path;
}

// -----------------------------------------------------------------------------
// 在 ELF 内存映像中查找符号
// base: 库的加载基址 (从 FindLibraryBase 获取)
// symName: 要查找的符号名 (如 "il2cpp_domain_get")
// 返回符号的运行时地址, 0 表示未找到
// -----------------------------------------------------------------------------
inline void* LookupSymbol(uintptr_t base, const char* symName) {
    if (base == 0 || !symName) return nullptr;

    // ELF header 在基址处
    Elf64_Ehdr* ehdr = (Elf64_Ehdr*)base;

    // 验证 ELF magic
    if (memcmp(ehdr->e_ident, "\x7f""ELF", 4) != 0) {
        ELF_LOGE("无效的 ELF magic (base=0x%lx)", base);
        return nullptr;
    }

    // section header table
    Elf64_Shdr* shdrs = (Elf64_Shdr*)(base + ehdr->e_shoff);
    if (ehdr->e_shoff == 0 || ehdr->e_shnum == 0) {
        ELF_LOGE("无 section header");
        return nullptr;
    }

    // 找 .shstrtab (section name string table)
    if (ehdr->e_shstrndx >= ehdr->e_shnum) {
        ELF_LOGE("e_shstrndx 无效");
        return nullptr;
    }
    Elf64_Shdr* shstrtab = &shdrs[ehdr->e_shstrndx];
    const char* strtabBase = (const char*)(base + shstrtab->sh_offset);

    // 遍历所有 section, 查找 DYNSYM 和 SYMTAB
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
                return addr;
            }
        }
    }

    ELF_LOGD("未找到符号 %s", symName);
    return nullptr;
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
        return nullptr;
    }
    return LookupSymbol(base, symName);
}

} // namespace ELFResolver
