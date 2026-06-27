#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif

#include "MemoryEngine.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <mutex>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>

namespace mem_engine {
namespace {

constexpr size_t kSearchBufferSize = 256 * 1024;
constexpr size_t kPageSize = 4096;
constexpr useconds_t kFreezeIntervalUs = 100000;

const char* const kDriverWhitelist[] = {
    "wanbai", "CheckMe", "Ckanri", "lanran", "video188",
};

const char* const kExcludedDevNames[] = {
    "binder", "common", "ashmem", "stdin", "stdout", "stderr",
};

thread_local sigjmp_buf gSafeJumpBuffer;
thread_local bool gInSafeAccess = false;

} // namespace

struct MemoryEngine::Impl {
    struct CopyMemory {
        pid_t     pid;
        uintptr_t addr;
        void*     buffer;
        size_t    size;
    };

    struct ModuleBase {
        pid_t     pid;
        char*     name;
        uintptr_t base;
    };

    enum DriverOp {
        OP_INIT_KEY     = 0x800,
        OP_READ_MEM     = 0x801,
        OP_WRITE_MEM    = 0x802,
        OP_MODULE_BASE  = 0x803,
        OP_HIDE_PROCESS = 0x804,
    };

    int         driver_fd = -1;
    pid_t       pid = -1;
    bool        attached = false;
    BackendType backend = BackendType::NONE;

    int   mem_fd = -1;
    pid_t mem_fd_pid = -1;

    bool             handler_installed = false;
    struct sigaction old_segv {};
    struct sigaction old_bus {};

    std::vector<SearchResult> results;
    mutable std::mutex        results_mutex;

    std::vector<MapEntry> maps;
    std::mutex            maps_mutex;

    std::vector<FreezeItem> freeze_list;
    mutable std::mutex      freeze_mutex;
    pthread_t               freeze_thread = 0;
    std::atomic<bool>       freeze_running {false};

    MemoryRegion region = REGION_ANONYMOUS;
    uintptr_t custom_start = 0;
    uintptr_t custom_end = 0;

    std::function<void(int, const char*)> progress_cb;

    Impl() {
        tryOpenDriver();
    }

    ~Impl() {
        stopFreeze();
        closeDriver();
        closeMemFd();
        restoreFaultHandler();
    }

    bool attachByName(const char* packageName) {
        const pid_t found = findPid(packageName);
        if (found <= 0) {
            report(0, "附加失败: 找不到进程");
            return false;
        }
        return attachPid(found, false);
    }

    bool attachPid(pid_t targetPid, bool local) {
        pid = targetPid;
        attached = false;

        if (local) {
            backend = BackendType::LOCAL;
            installFaultHandler();
        } else if (driver_fd >= 0) {
            backend = BackendType::KERNEL;
        } else {
            backend = BackendType::USERSPACE;
        }

        {
            std::lock_guard<std::mutex> lk(maps_mutex);
            if (!parseMapsLocked()) {
                report(0, "附加失败: 无法读取 maps");
                return false;
            }
        }

        attached = true;

        char msg[128];
        std::snprintf(msg, sizeof(msg), "已附加 PID=%d [%s], %zu个区域",
                      pid, backendName(), maps.size());
        report(100, msg);

        startFreeze();
        return true;
    }

    const char* backendName() const {
        switch (backend) {
            case BackendType::KERNEL:    return "kernel";
            case BackendType::LOCAL:     return "local";
            case BackendType::USERSPACE: return "userspace";
            default:                     return "none";
        }
    }

    bool initDriverKey(const char* key) {
        if (driver_fd < 0 || key == nullptr) return false;

        char buf[0x100] {};
        std::snprintf(buf, sizeof(buf), "%s", key);
        return ioctl(driver_fd, OP_INIT_KEY, buf) == 0;
    }

    void hideProcess() {
        if (driver_fd >= 0) {
            ioctl(driver_fd, OP_HIDE_PROCESS);
        }
    }

    uintptr_t getModuleBase(const char* name) {
        if (name == nullptr || *name == '\0') return 0;

        if (driver_fd >= 0 && backend == BackendType::KERNEL) {
            ModuleBase mb {};
            char buf[0x100] {};
            std::snprintf(buf, sizeof(buf), "%s", name);
            mb.pid = pid;
            mb.name = buf;
            if (ioctl(driver_fd, OP_MODULE_BASE, &mb) == 0) {
                return mb.base;
            }
        }

        return getModuleBaseFromMaps(name);
    }

    bool memRead(uintptr_t addr, void* buf, size_t size) {
        if (buf == nullptr || size == 0) return false;

        switch (backend) {
            case BackendType::KERNEL:    return kernelRead(addr, buf, size);
            case BackendType::LOCAL:     return localRead(addr, buf, size);
            case BackendType::USERSPACE: return userspaceRead(addr, buf, size);
            default:                     return false;
        }
    }

    bool memWrite(uintptr_t addr, const void* buf, size_t size) {
        if (buf == nullptr || size == 0) return false;

        switch (backend) {
            case BackendType::KERNEL:    return kernelWrite(addr, buf, size);
            case BackendType::LOCAL:     return localWrite(addr, buf, size);
            case BackendType::USERSPACE: return userspaceWrite(addr, buf, size);
            default:                     return false;
        }
    }

    void clearResults() {
        std::lock_guard<std::mutex> lk(results_mutex);
        results.clear();
    }

    int getResultCount() const {
        std::lock_guard<std::mutex> lk(results_mutex);
        return static_cast<int>(results.size());
    }

    std::vector<SearchResult> getResults(int max) const {
        std::lock_guard<std::mutex> lk(results_mutex);
        if (max < 0 || max >= static_cast<int>(results.size())) {
            return results;
        }
        return {results.begin(), results.begin() + max};
    }

    int searchNumber(const char* value, ValueType type) {
        if (!attached || value == nullptr) return 0;

        parseMaps();

        const bool isLocal = (backend == BackendType::LOCAL);
        {
            std::lock_guard<std::mutex> lk(results_mutex);
            if (isLocal && !results.empty()) {
                for (auto& item : results) {
                    item = SearchResult{};
                }
                std::vector<SearchResult>().swap(results);
            } else {
                results.clear();
            }
        }

        const double searchVal = std::strtod(value, nullptr);
        const int valSize = typeSize(type);
        auto regions = filterMaps();

        if (regions.empty()) {
            report(0, "没有匹配的内存区域");
            return 0;
        }

        size_t totalSize = 0;
        for (const auto& item : regions) {
            totalSize += item.end - item.start;
        }

        char* buffer = nullptr;
        void* mmapBuffer = nullptr;
        std::vector<char> heapBuffer;

        if (isLocal) {
            mmapBuffer = mmap(nullptr, kSearchBufferSize, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (mmapBuffer == MAP_FAILED) return 0;
            buffer = static_cast<char*>(mmapBuffer);
        } else {
            heapBuffer.resize(kSearchBufferSize);
            buffer = heapBuffer.data();
        }

        size_t scanned = 0;
        int lastPct = -1;

        const float searchF = static_cast<float>(searchVal);
        const int32_t searchI = static_cast<int32_t>(searchVal);
        const bool exactFloat = (type == TYPE_FLOAT && searchVal == static_cast<double>(searchF));
        const bool exactDword = (type == TYPE_DWORD);

        std::vector<SearchResult> localResults;
        localResults.reserve(4096);

        for (const auto& map : regions) {
            uintptr_t addr = map.start;
            uintptr_t end = map.end;

            if (region == REGION_CUSTOM) {
                addr = std::max(addr, custom_start);
                end = std::min(end, custom_end);
            }

            while (addr + static_cast<size_t>(valSize) <= end) {
                const size_t chunk = std::min(static_cast<size_t>(end - addr), kSearchBufferSize);

                if (isLocal && !localResults.empty()) {
                    const uintptr_t vecStart = reinterpret_cast<uintptr_t>(localResults.data());
                    const uintptr_t vecEnd = vecStart + localResults.capacity() * sizeof(SearchResult);
                    if (addr < vecEnd && addr + chunk > vecStart) {
                        addr += chunk;
                        scanned += chunk;
                        continue;
                    }
                }

                if (!readBlock(addr, buffer, chunk)) {
                    addr += chunk;
                    scanned += chunk;
                    continue;
                }

                const char* base = buffer;

                if (exactDword) {
                    for (size_t off = 0; off + 4 <= chunk; off += 4) {
                        int32_t memI;
                        std::memcpy(&memI, base + off, sizeof(memI));
                        if (memI == searchI) {
                            localResults.push_back(makeResult(addr + off, base + off, 4, type));
                        }
                    }
                } else if (exactFloat) {
                    for (size_t off = 0; off + 4 <= chunk; off += 4) {
                        float memF;
                        std::memcpy(&memF, base + off, sizeof(memF));
                        if (memF == searchF ||
                            std::fabs(memF - searchF) < std::fabs(searchF) * 0.0001f) {
                            localResults.push_back(makeResult(addr + off, base + off, 4, type));
                        }
                    }
                } else {
                    for (size_t off = 0; off + static_cast<size_t>(valSize) <= chunk;
                         off += static_cast<size_t>(valSize)) {
                        const double memVal = readTyped(base + off, type);
                        if (matchVal(memVal, searchVal, type)) {
                            localResults.push_back(makeResult(addr + off, base + off, valSize, type));
                        }
                    }
                }

                scanned += chunk;
                addr += chunk;
                reportProgress(scanned, totalSize, localResults.size(), lastPct);
            }
        }

        if (mmapBuffer && mmapBuffer != MAP_FAILED) {
            munmap(mmapBuffer, kSearchBufferSize);
        }

        {
            std::lock_guard<std::mutex> lk(results_mutex);
            results = std::move(localResults);
        }

        char msg[128];
        std::snprintf(msg, sizeof(msg), "搜索完成, %zu个结果", results.size());
        report(100, msg);
        return static_cast<int>(results.size());
    }

    int refineSearch(const char* value, ValueType type) {
        if (!attached || value == nullptr) return 0;

        const double searchVal = std::strtod(value, nullptr);
        const int valSize = typeSize(type);

        std::lock_guard<std::mutex> lk(results_mutex);
        std::vector<SearchResult> kept;
        kept.reserve(results.size());

        for (auto& item : results) {
            if (item.excluded) continue;

            char buf[8] {};
            if (!memRead(item.address, buf, valSize)) continue;

            const double memVal = readTyped(buf, type);
            if (matchVal(memVal, searchVal, type)) {
                std::memcpy(item.bytes, buf, valSize);
                kept.push_back(item);
            }
        }

        results = std::move(kept);

        char msg[128];
        std::snprintf(msg, sizeof(msg), "筛选完成, %zu个结果", results.size());
        report(100, msg);
        return static_cast<int>(results.size());
    }

    int SearchWrite(const std::vector<SignatureEntry>& sigs,
                    const std::vector<ModifyEntry>& mods,
                    const char* name) {
        if (sigs.empty() || !attached) return 0;

        report(0, "主特征码搜索...");
        if (searchNumber(sigs[0].value.c_str(), sigs[0].type) == 0) {
            reportFail(name, "未找到主特征码");
            return 0;
        }

        std::lock_guard<std::mutex> lk(results_mutex);
        verifySigs(sigs);

        const int valid = countValid();
        if (valid == 0) {
            reportFail(name, "校验不通过");
            return 0;
        }

        int modCnt = 0;
        int freezeCnt = 0;
        applyMods(mods, modCnt, freezeCnt);

        char msg[256];
        std::snprintf(msg, sizeof(msg), "%s 成功, 修改%d条%s (匹配%d)",
                      name ? name : "", modCnt, freezeCnt > 0 ? " (含冻结)" : "", valid);
        report(100, msg);
        return modCnt;
    }

    int SearchRestore(const std::vector<SignatureEntry>& sigs,
                      const std::vector<ModifyEntry>& restores,
                      const char* name) {
        if (sigs.empty() || !attached) return 0;
        if (searchNumber(sigs[0].value.c_str(), sigs[0].type) == 0) return 0;

        std::lock_guard<std::mutex> lk(results_mutex);
        verifySigs(sigs);

        int count = 0;
        for (const auto& restore : restores) {
            const int size = typeSize(restore.type);
            for (auto& item : results) {
                if (item.excluded) continue;

                const uintptr_t writeAddr = item.address + restore.offset;
                char writeBuf[8] {};
                writeTyped(writeBuf, restore.value, restore.type);

                if (memWrite(writeAddr, writeBuf, size)) {
                    ++count;
                    removeFreezeByAddress(writeAddr);
                }
            }
        }

        char msg[128];
        std::snprintf(msg, sizeof(msg), "%s 已关闭, 恢复%d条", name ? name : "", count);
        report(100, msg);
        return count;
    }

    std::u16string readUtf16(uintptr_t addr, int charCount) {
        if (charCount <= 0 || charCount > 4096) return {};

        std::u16string buf(static_cast<size_t>(charCount), 0);
        if (!memRead(addr, &buf[0], static_cast<size_t>(charCount) * sizeof(char16_t))) {
            return {};
        }
        return buf;
    }

    bool writeUtf16(uintptr_t addr, const std::u16string& str) {
        if (str.empty()) return true;
        return memWrite(addr, str.data(), str.size() * sizeof(char16_t));
    }

    std::string readUtf8(uintptr_t addr, int maxBytes) {
        if (maxBytes <= 0 || maxBytes > 1024 * 1024) return {};

        std::string result;
        result.reserve(64);

        for (int i = 0; i < maxBytes; ++i) {
            char ch = 0;
            if (!memRead(addr + static_cast<uintptr_t>(i), &ch, sizeof(ch))) break;
            if (ch == '\0') break;
            result.push_back(ch);
        }

        return result;
    }

    bool writeUtf8(uintptr_t addr, const std::string& str) {
        return memWrite(addr, str.c_str(), str.size() + 1);
    }

    std::string readUnityString(uintptr_t strObj) {
        if (strObj == 0) return {};

        const int32_t len = MemoryEngine::instance().read<int32_t>(strObj + 0x10);
        if (len <= 0 || len > 4096) return {};

        return MemoryEngine::utf16ToUtf8(readUtf16(strObj + 0x14, len));
    }

    bool writeUnityString(uintptr_t strObj, const std::string& str) {
        if (strObj == 0) return false;

        const int32_t maxLen = MemoryEngine::instance().read<int32_t>(strObj + 0x10);
        if (maxLen <= 0) return false;

        const auto u16 = MemoryEngine::utf8ToUtf16(str);
        const int writeLen = std::min(static_cast<int>(u16.size()), static_cast<int>(maxLen));

        if (writeLen > 0 &&
            !memWrite(strObj + 0x14, u16.data(), static_cast<size_t>(writeLen) * sizeof(char16_t))) {
            return false;
        }

        char16_t zero = 0;
        memWrite(strObj + 0x14 + static_cast<uintptr_t>(writeLen * sizeof(char16_t)),
                 &zero, sizeof(zero));
        MemoryEngine::instance().write<int32_t>(strObj + 0x10, writeLen);
        return true;
    }

    std::vector<std::string> readUnityStringArray(uintptr_t arrayObj, int count) {
        if (arrayObj == 0) return {};

        if (count < 0) {
            count = MemoryEngine::instance().read<int32_t>(arrayObj + 0x18);
            if (count <= 0 || count > 4096) return {};
        }

        std::vector<std::string> output;
        output.reserve(static_cast<size_t>(count));

        for (int i = 0; i < count; ++i) {
            const uintptr_t strPtr =
                MemoryEngine::instance().read<uintptr_t>(arrayObj + 0x20 + i * sizeof(uintptr_t));
            output.push_back(readUnityString(strPtr));
        }

        return output;
    }

    void addFreezeItem(const FreezeItem& item) {
        std::lock_guard<std::mutex> lk(freeze_mutex);
        freeze_list.push_back(item);
    }

    void removeFreezeByAddress(uintptr_t addr) {
        std::lock_guard<std::mutex> lk(freeze_mutex);
        freeze_list.erase(
            std::remove_if(freeze_list.begin(), freeze_list.end(),
                           [addr](const FreezeItem& item) { return item.address == addr; }),
            freeze_list.end());
    }

    void clearFreezeList() {
        std::lock_guard<std::mutex> lk(freeze_mutex);
        freeze_list.clear();
    }

    int getFreezeCount() const {
        std::lock_guard<std::mutex> lk(freeze_mutex);
        return static_cast<int>(freeze_list.size());
    }

    std::vector<MapEntry> getMemoryMaps() {
        parseMaps();
        std::lock_guard<std::mutex> lk(maps_mutex);
        return maps;
    }

    size_t getRegionTotalSize() {
        parseMaps();

        size_t total = 0;
        for (const auto& item : filterMaps()) {
            total += item.end - item.start;
        }
        return total;
    }

private:
    void tryOpenDriver() {
        char* path = findDriverPath();
        if (path == nullptr) return;

        driver_fd = open(path, O_RDWR);
        free(path);
    }

    char* findDriverPath() {
        DIR* dir = opendir("/dev");
        if (dir == nullptr) return nullptr;

        struct dirent* entry = nullptr;
        while ((entry = readdir(dir)) != nullptr) {
            const char* name = entry->d_name;
            if (name[0] == '.') continue;

            for (const auto* white : kDriverWhitelist) {
                if (std::strcmp(name, white) == 0) {
                    char* path = static_cast<char*>(std::malloc(std::strlen(name) + 6));
                    if (path != nullptr) {
                        std::sprintf(path, "/dev/%s", name);
                    }
                    closedir(dir);
                    return path;
                }
            }

            if (std::strlen(name) != 6) continue;
            if (std::strchr(name, '_') || std::strchr(name, '-') || std::strchr(name, ':')) continue;

            bool skip = false;
            for (const auto* excluded : kExcludedDevNames) {
                if (std::strcmp(name, excluded) == 0) {
                    skip = true;
                    break;
                }
            }
            if (skip) continue;

            char pathBuf[512];
            std::snprintf(pathBuf, sizeof(pathBuf), "/dev/%s", name);

            struct stat info {};
            if (stat(pathBuf, &info) < 0) continue;
            if (!S_ISCHR(info.st_mode) && !S_ISBLK(info.st_mode)) continue;

            struct tm* timeInfo = localtime(&info.st_ctime);
            if (timeInfo == nullptr || timeInfo->tm_year + 1900 <= 1980) continue;

            if (info.st_atime == info.st_ctime &&
                info.st_size == 0 &&
                info.st_gid == 0 &&
                info.st_uid == 0) {
                closedir(dir);
                return strdup(pathBuf);
            }
        }

        closedir(dir);
        return nullptr;
    }

    void closeDriver() {
        if (driver_fd >= 0) {
            close(driver_fd);
            driver_fd = -1;
        }
    }

    static pid_t findPid(const char* packageName) {
        if (packageName == nullptr || *packageName == '\0') return -1;

        DIR* dir = opendir("/proc");
        if (dir == nullptr) return -1;

        pid_t found = -1;
        struct dirent* entry = nullptr;

        while ((entry = readdir(dir)) != nullptr) {
            if (!std::isdigit(static_cast<unsigned char>(entry->d_name[0]))) continue;

            char path[512];
            std::snprintf(path, sizeof(path), "/proc/%s/cmdline", entry->d_name);

            FILE* fp = std::fopen(path, "r");
            if (fp == nullptr) continue;

            char cmd[256] {};
            std::fread(cmd, 1, sizeof(cmd) - 1, fp);
            std::fclose(fp);

            if (std::strcmp(cmd, packageName) == 0) {
                found = static_cast<pid_t>(std::atoi(entry->d_name));
                break;
            }
        }

        closedir(dir);
        return found;
    }

    uintptr_t getModuleBaseFromMaps(const char* name) {
        std::lock_guard<std::mutex> lk(maps_mutex);

        auto findInMaps = [this, name]() -> uintptr_t {
            for (const auto& item : maps) {
                if (item.name.find(name) != std::string::npos) {
                    return item.start;
                }
            }
            return 0;
        };

        uintptr_t base = findInMaps();
        if (base != 0) return base;

        parseMapsLocked();
        return findInMaps();
    }

    bool kernelRead(uintptr_t addr, void* buf, size_t size) {
        if (driver_fd < 0) return false;

        CopyMemory cm {pid, addr, buf, size};
        return ioctl(driver_fd, OP_READ_MEM, &cm) == 0;
    }

    bool kernelWrite(uintptr_t addr, const void* buf, size_t size) {
        if (driver_fd < 0) return false;

        CopyMemory cm {pid, addr, const_cast<void*>(buf), size};
        return ioctl(driver_fd, OP_WRITE_MEM, &cm) == 0;
    }

    static void faultHandler(int) {
        if (gInSafeAccess) {
            siglongjmp(gSafeJumpBuffer, 1);
        }
    }

    void installFaultHandler() {
        if (handler_installed) return;

        struct sigaction sa {};
        sa.sa_handler = faultHandler;
        sigemptyset(&sa.sa_mask);

        sigaction(SIGSEGV, &sa, &old_segv);
        sigaction(SIGBUS, &sa, &old_bus);

        handler_installed = true;
    }

    void restoreFaultHandler() {
        if (!handler_installed) return;

        sigaction(SIGSEGV, &old_segv, nullptr);
        sigaction(SIGBUS, &old_bus, nullptr);
        handler_installed = false;
    }

    bool ensureMemFd() {
        if (mem_fd >= 0 && mem_fd_pid == pid) return true;

        closeMemFd();

        char path[64];
        if (backend == BackendType::LOCAL) {
            std::snprintf(path, sizeof(path), "/proc/self/mem");
        } else {
            std::snprintf(path, sizeof(path), "/proc/%d/mem", pid);
        }

        mem_fd = open(path, O_RDWR);
        if (mem_fd < 0) {
            mem_fd = open(path, O_RDONLY);
        }

        mem_fd_pid = pid;
        return mem_fd >= 0;
    }

    void closeMemFd() {
        if (mem_fd >= 0) {
            close(mem_fd);
            mem_fd = -1;
            mem_fd_pid = -1;
        }
    }

    bool localRead(uintptr_t addr, void* buf, size_t size) {
        if (ensureMemFd()) {
            if (pread64(mem_fd, buf, size, static_cast<off64_t>(addr)) == static_cast<ssize_t>(size)) {
                return true;
            }
        }

        if (size > kPageSize) return false;

        gInSafeAccess = true;
        const bool ok = (sigsetjmp(gSafeJumpBuffer, 1) == 0);
        if (ok) {
            std::memcpy(buf, reinterpret_cast<const void*>(addr), size);
        }
        gInSafeAccess = false;

        return ok;
    }

    bool localWrite(uintptr_t addr, const void* buf, size_t size) {
        gInSafeAccess = true;
        const bool ok = (sigsetjmp(gSafeJumpBuffer, 1) == 0);
        if (ok) {
            std::memcpy(reinterpret_cast<void*>(addr), buf, size);
        }
        gInSafeAccess = false;

        return ok;
    }

    bool userspaceRead(uintptr_t addr, void* buf, size_t size) {
        struct iovec local {buf, size};
        struct iovec remote {reinterpret_cast<void*>(addr), size};

        const ssize_t n = syscall(__NR_process_vm_readv, pid, &local, 1, &remote, 1, 0);
        if (n == static_cast<ssize_t>(size)) return true;

        if (!ensureMemFd()) return false;
        return pread64(mem_fd, buf, size, static_cast<off64_t>(addr)) == static_cast<ssize_t>(size);
    }

    bool userspaceWrite(uintptr_t addr, const void* buf, size_t size) {
        struct iovec local {const_cast<void*>(buf), size};
        struct iovec remote {reinterpret_cast<void*>(addr), size};

        const ssize_t n = syscall(__NR_process_vm_writev, pid, &local, 1, &remote, 1, 0);
        if (n == static_cast<ssize_t>(size)) return true;

        if (!ensureMemFd()) return false;
        return pwrite64(mem_fd, buf, size, static_cast<off64_t>(addr)) == static_cast<ssize_t>(size);
    }

    bool readBlock(uintptr_t addr, void* buf, size_t size) {
        if (memRead(addr, buf, size)) return true;

        bool anyOk = false;
        for (size_t off = 0; off < size; off += kPageSize) {
            const size_t chunk = std::min(kPageSize, size - off);
            if (memRead(addr + off, static_cast<char*>(buf) + off, chunk)) {
                anyOk = true;
            } else {
                std::memset(static_cast<char*>(buf) + off, 0, chunk);
            }
        }

        return anyOk;
    }

    bool parseMaps() {
        std::lock_guard<std::mutex> lk(maps_mutex);
        return parseMapsLocked();
    }

    bool parseMapsLocked() {
        maps.clear();

        char path[64];
        std::snprintf(path, sizeof(path), "/proc/%d/maps", pid);

        FILE* fp = std::fopen(path, "r");
        if (fp == nullptr) return false;

        char line[512];
        while (std::fgets(line, sizeof(line), fp)) {
            unsigned long start = 0;
            unsigned long end = 0;
            char perms[5] {};
            int namePos = 0;

            if (std::sscanf(line, "%lx-%lx %4s %*s %*s %*s %n",
                            &start, &end, perms, &namePos) >= 3) {
                MapEntry item;
                item.start = static_cast<uintptr_t>(start);
                item.end = static_cast<uintptr_t>(end);
                std::memcpy(item.perms, perms, sizeof(item.perms));
                item.readable = (perms[0] == 'r');
                item.writable = (perms[1] == 'w');

                if (namePos > 0 && namePos < static_cast<int>(std::strlen(line))) {
                    char* name = line + namePos;
                    char* newline = std::strchr(name, '\n');
                    if (newline) *newline = '\0';
                    while (*name == ' ') ++name;
                    item.name = name;
                }

                maps.push_back(std::move(item));
            }
        }

        std::fclose(fp);
        return !maps.empty();
    }

    std::vector<MapEntry> filterMaps() {
        std::lock_guard<std::mutex> lk(maps_mutex);

        std::vector<MapEntry> output;
        output.reserve(maps.size());

        for (const auto& item : maps) {
            if (!item.readable) continue;

            bool ok = false;
            switch (region) {
                case REGION_ALL:
                    ok = true;
                    break;
                case REGION_ANONYMOUS:
                    ok = item.name.empty() || item.name.find("[anon:") == 0;
                    break;
                case REGION_HEAP:
                    ok = item.name == "[heap]";
                    break;
                case REGION_STACK:
                    ok = item.name.find("[stack") == 0;
                    break;
                case REGION_CODE_APP:
                    ok = item.name.find("/data/app/") != std::string::npos && item.perms[2] == 'x';
                    break;
                case REGION_CODE_SYS:
                    ok = item.name.find("/system/") != std::string::npos && item.perms[2] == 'x';
                    break;
                case REGION_BAD:
                    ok = item.writable && (item.name.empty() || item.name[0] == '[');
                    break;
                case REGION_ASHMEM:
                    ok = item.name.find("/dev/ashmem") != std::string::npos;
                    break;
                case REGION_CUSTOM:
                    ok = item.end > custom_start && item.start < custom_end;
                    break;
                default:
                    ok = false;
                    break;
            }

            if (ok) output.push_back(item);
        }

        return output;
    }

    static int typeSize(ValueType type) {
        static const int sizes[] = {0, 1, 2, 4, 4, 8, 8};
        return (type >= TYPE_BYTE && type <= TYPE_DOUBLE) ? sizes[type] : 4;
    }

    static double readTyped(const void* p, ValueType type) {
        switch (type) {
            case TYPE_BYTE: {
                uint8_t v;
                std::memcpy(&v, p, sizeof(v));
                return v;
            }
            case TYPE_WORD: {
                uint16_t v;
                std::memcpy(&v, p, sizeof(v));
                return v;
            }
            case TYPE_DWORD: {
                int32_t v;
                std::memcpy(&v, p, sizeof(v));
                return v;
            }
            case TYPE_FLOAT: {
                float v;
                std::memcpy(&v, p, sizeof(v));
                return v;
            }
            case TYPE_QWORD: {
                int64_t v;
                std::memcpy(&v, p, sizeof(v));
                return static_cast<double>(v);
            }
            case TYPE_DOUBLE: {
                double v;
                std::memcpy(&v, p, sizeof(v));
                return v;
            }
            default:
                return 0;
        }
    }

    static void writeTyped(void* buf, double value, ValueType type) {
        switch (type) {
            case TYPE_BYTE: {
                const uint8_t v = static_cast<uint8_t>(value);
                std::memcpy(buf, &v, sizeof(v));
                break;
            }
            case TYPE_WORD: {
                const uint16_t v = static_cast<uint16_t>(value);
                std::memcpy(buf, &v, sizeof(v));
                break;
            }
            case TYPE_DWORD: {
                const int32_t v = static_cast<int32_t>(value);
                std::memcpy(buf, &v, sizeof(v));
                break;
            }
            case TYPE_FLOAT: {
                const float v = static_cast<float>(value);
                std::memcpy(buf, &v, sizeof(v));
                break;
            }
            case TYPE_QWORD: {
                const int64_t v = static_cast<int64_t>(value);
                std::memcpy(buf, &v, sizeof(v));
                break;
            }
            case TYPE_DOUBLE: {
                std::memcpy(buf, &value, sizeof(value));
                break;
            }
            default:
                break;
        }
    }

    static bool matchVal(double mem, double search, ValueType type) {
        if (type == TYPE_FLOAT || type == TYPE_DOUBLE) {
            if (search == 0) return std::fabs(mem) < 0.0001;
            return std::fabs(mem - search) / std::fabs(search) < 0.0001;
        }

        return static_cast<long long>(mem) == static_cast<long long>(search);
    }

    static SearchResult makeResult(uintptr_t addr, const void* data, int size, ValueType type) {
        SearchResult item;
        item.address = addr;
        item.type = type;
        std::memcpy(item.bytes, data, static_cast<size_t>(size));
        return item;
    }

    void verifySigs(const std::vector<SignatureEntry>& sigs) {
        for (size_t i = 1; i < sigs.size(); ++i) {
            const auto& sig = sigs[i];
            const double expected = std::strtod(sig.value.c_str(), nullptr);
            const int size = typeSize(sig.type);

            for (auto& item : results) {
                if (item.excluded) continue;

                char buf[8] {};
                if (!memRead(item.address + sig.offset, buf, size) ||
                    !matchVal(readTyped(buf, sig.type), expected, sig.type)) {
                    item.excluded = true;
                }
            }
        }
    }

    int countValid() const {
        int count = 0;
        for (const auto& item : results) {
            if (!item.excluded) ++count;
        }
        return count;
    }

    void applyMods(const std::vector<ModifyEntry>& mods, int& modCount, int& freezeCount) {
        for (const auto& mod : mods) {
            const int size = typeSize(mod.type);

            for (auto& item : results) {
                if (item.excluded) continue;

                const uintptr_t writeAddr = item.address + mod.offset;
                char writeBuf[8] {};
                writeTyped(writeBuf, mod.value, mod.type);

                if (memWrite(writeAddr, writeBuf, size)) {
                    ++modCount;

                    if (mod.freeze) {
                        FreezeItem freezeItem;
                        freezeItem.address = writeAddr;
                        freezeItem.type = mod.type;
                        freezeItem.active = true;
                        writeTyped(freezeItem.data, mod.value, freezeItem.type);
                        addFreezeItem(freezeItem);
                        ++freezeCount;
                    }
                }
            }
        }
    }

    static void* freezeLoop(void* arg) {
        auto* engine = static_cast<Impl*>(arg);

        while (engine->freeze_running.load()) {
            {
                std::lock_guard<std::mutex> lk(engine->freeze_mutex);
                for (auto& item : engine->freeze_list) {
                    if (!item.active) continue;
                    engine->memWrite(item.address, item.data, typeSize(item.type));
                }
            }
            usleep(kFreezeIntervalUs);
        }

        return nullptr;
    }

    void startFreeze() {
        if (freeze_running.load()) return;

        freeze_running.store(true);
        if (pthread_create(&freeze_thread, nullptr, freezeLoop, this) != 0) {
            freeze_running.store(false);
            freeze_thread = 0;
        }
    }

    void stopFreeze() {
        if (!freeze_running.load()) return;

        freeze_running.store(false);
        if (freeze_thread) {
            pthread_join(freeze_thread, nullptr);
            freeze_thread = 0;
        }
    }

    void report(int pct, const char* msg) {
        if (progress_cb) {
            progress_cb(pct, msg);
        }
    }

    void reportProgress(size_t scanned, size_t total, size_t count, int& lastPct) {
        const int pct = total > 0 ? static_cast<int>((static_cast<double>(scanned) / total) * 100) : 100;

        if (pct / 5 != lastPct / 5) {
            lastPct = pct;
            char msg[128];
            std::snprintf(msg, sizeof(msg), "搜索中 %d%% (%zu结果)", pct, count);
            report(pct, msg);
        }
    }

    void reportFail(const char* name, const char* reason) {
        char msg[128];
        std::snprintf(msg, sizeof(msg), "%s: %s", name ? name : "", reason ? reason : "");
        report(100, msg);
    }
};

MemoryEngine& MemoryEngine::instance() {
    static MemoryEngine inst;
    return inst;
}

MemoryEngine::MemoryEngine()
    : impl_(std::make_unique<Impl>()) {}

MemoryEngine::~MemoryEngine() = default;

bool MemoryEngine::attach(const char* packageName) {
    return impl_->attachByName(packageName);
}

bool MemoryEngine::attach(pid_t pid) {
    return impl_->attachPid(pid, false);
}

bool MemoryEngine::attachSelf() {
    return impl_->attachPid(getpid(), true);
}

bool MemoryEngine::isAttached() const {
    return impl_->attached;
}

pid_t MemoryEngine::getPid() const {
    return impl_->pid;
}

BackendType MemoryEngine::backend() const {
    return impl_->backend;
}

const char* MemoryEngine::backendName() const {
    return impl_->backendName();
}

void MemoryEngine::setRegion(MemoryRegion region) {
    impl_->region = region;
}

void MemoryEngine::setCustomRange(uintptr_t start, uintptr_t end) {
    if (start > end) std::swap(start, end);
    impl_->region = REGION_CUSTOM;
    impl_->custom_start = start;
    impl_->custom_end = end;
}

void MemoryEngine::setProgressCallback(std::function<void(int, const char*)> cb) {
    impl_->progress_cb = std::move(cb);
}

bool MemoryEngine::initDriverKey(const char* key) {
    return impl_->initDriverKey(key);
}

void MemoryEngine::hideProcess() {
    impl_->hideProcess();
}

uintptr_t MemoryEngine::getModuleBase(const char* name) {
    return impl_->getModuleBase(name);
}

bool MemoryEngine::readBytes(uintptr_t addr, void* buffer, size_t size) {
    return impl_->memRead(addr, buffer, size);
}

bool MemoryEngine::writeBytes(uintptr_t addr, const void* buffer, size_t size) {
    return impl_->memWrite(addr, buffer, size);
}

void MemoryEngine::clearResults() {
    impl_->clearResults();
}

int MemoryEngine::getResultCount() const {
    return impl_->getResultCount();
}

std::vector<SearchResult> MemoryEngine::getResults(int max) const {
    return impl_->getResults(max);
}

int MemoryEngine::searchNumber(const char* value, ValueType type) {
    return impl_->searchNumber(value, type);
}

int MemoryEngine::refineSearch(const char* value, ValueType type) {
    return impl_->refineSearch(value, type);
}

int MemoryEngine::SearchWrite(const std::vector<SignatureEntry>& sigs,
                              const std::vector<ModifyEntry>& mods,
                              const char* name) {
    return impl_->SearchWrite(sigs, mods, name);
}

int MemoryEngine::SearchRestore(const std::vector<SignatureEntry>& sigs,
                                const std::vector<ModifyEntry>& restores,
                                const char* name) {
    return impl_->SearchRestore(sigs, restores, name);
}

std::u16string MemoryEngine::readUtf16(uintptr_t addr, int charCount) {
    return impl_->readUtf16(addr, charCount);
}

bool MemoryEngine::writeUtf16(uintptr_t addr, const std::u16string& str) {
    return impl_->writeUtf16(addr, str);
}

std::string MemoryEngine::readUtf8(uintptr_t addr, int maxBytes) {
    return impl_->readUtf8(addr, maxBytes);
}

bool MemoryEngine::writeUtf8(uintptr_t addr, const std::string& str) {
    return impl_->writeUtf8(addr, str);
}

std::string MemoryEngine::readUnityString(uintptr_t strObj) {
    return impl_->readUnityString(strObj);
}

bool MemoryEngine::writeUnityString(uintptr_t strObj, const std::string& str) {
    return impl_->writeUnityString(strObj, str);
}

std::vector<std::string> MemoryEngine::readUnityStringArray(uintptr_t arrayObj, int count) {
    return impl_->readUnityStringArray(arrayObj, count);
}

void MemoryEngine::addFreezeItem(const FreezeItem& item) {
    impl_->addFreezeItem(item);
}

void MemoryEngine::removeFreezeByAddress(uintptr_t addr) {
    impl_->removeFreezeByAddress(addr);
}

void MemoryEngine::clearFreezeList() {
    impl_->clearFreezeList();
}

int MemoryEngine::getFreezeCount() const {
    return impl_->getFreezeCount();
}

std::vector<MapEntry> MemoryEngine::getMemoryMaps() {
    return impl_->getMemoryMaps();
}

size_t MemoryEngine::getRegionTotalSize() {
    return impl_->getRegionTotalSize();
}

std::string MemoryEngine::utf16ToUtf8(const std::u16string& u16) {
    std::string output;
    output.reserve(u16.size() * 3);

    for (char16_t ch : u16) {
        if (ch < 0x80) {
            output.push_back(static_cast<char>(ch));
        } else if (ch < 0x800) {
            output.push_back(static_cast<char>(0xC0 | (ch >> 6)));
            output.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        } else {
            output.push_back(static_cast<char>(0xE0 | (ch >> 12)));
            output.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        }
    }

    return output;
}

std::u16string MemoryEngine::utf8ToUtf16(const std::string& u8) {
    std::u16string output;
    output.reserve(u8.size());

    for (size_t i = 0; i < u8.size();) {
        uint32_t cp = 0;
        const uint8_t c = static_cast<uint8_t>(u8[i]);

        if (c < 0x80) {
            cp = c;
            i += 1;
        } else if ((c >> 5) == 0x6) {
            cp = static_cast<uint32_t>(c & 0x1F) << 6;
            if (i + 1 < u8.size()) {
                cp |= static_cast<uint8_t>(u8[i + 1]) & 0x3F;
            }
            i += 2;
        } else if ((c >> 4) == 0xE) {
            cp = static_cast<uint32_t>(c & 0x0F) << 12;
            if (i + 1 < u8.size()) {
                cp |= static_cast<uint32_t>(static_cast<uint8_t>(u8[i + 1]) & 0x3F) << 6;
            }
            if (i + 2 < u8.size()) {
                cp |= static_cast<uint8_t>(u8[i + 2]) & 0x3F;
            }
            i += 3;
        } else {
            ++i;
            continue;
        }

        output.push_back(static_cast<char16_t>(cp));
    }

    return output;
}

} // namespace mem_engine
