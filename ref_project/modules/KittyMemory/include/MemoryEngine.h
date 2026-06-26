#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <sys/types.h>
#include <vector>

namespace mem_engine {

/**
 * @brief 支持搜索/读写的数据类型。
 *
 * 取值保持与旧版本兼容：
 * - BYTE/WORD/DWORD/QWORD：整数类型
 * - FLOAT/DOUBLE：浮点类型，搜索时使用小误差匹配
 */
enum ValueType {
    TYPE_BYTE   = 1,
    TYPE_WORD   = 2,
    TYPE_DWORD  = 3,
    TYPE_FLOAT  = 4,
    TYPE_QWORD  = 5,
    TYPE_DOUBLE = 6,
};

/**
 * @brief 内存区域过滤类型。
 *
 * 默认搜索匿名区域。需要精确范围时可调用 setCustomRange()。
 */
enum MemoryRegion {
    REGION_ALL = 0,
    REGION_ANONYMOUS,
    REGION_HEAP,
    REGION_STACK,
    REGION_CODE_APP,
    REGION_CODE_SYS,
    REGION_BAD,
    REGION_ASHMEM,
    REGION_CUSTOM = 99,
};

/**
 * @brief 当前使用的读写后端。
 *
 * KERNEL    : 使用内核驱动 ioctl 读写；
 * LOCAL     : 注入/本进程模式；
 * USERSPACE : process_vm_readv/writev 或 /proc/<pid>/mem。
 */
enum class BackendType {
    NONE,
    KERNEL,
    LOCAL,
    USERSPACE,
};

/**
 * @brief 预留错误码，便于业务层扩展统一错误处理。
 */
enum class Error {
    OK = 0,
    NO_PROCESS,
    NO_MAPS,
    NO_BACKEND,
    NO_RESULTS,
    VERIFY_FAILED,
    READ_FAILED,
    WRITE_FAILED,
};

/**
 * @brief 单条搜索结果。
 *
 * bytes 最多保存 8 字节原始数据，覆盖当前支持的最大类型 QWORD/DOUBLE。
 */
struct SearchResult {
    uintptr_t address = 0;
    char      bytes[8]{};
    ValueType type = TYPE_DWORD;
    bool      excluded = false;
};

/**
 * @brief /proc/<pid>/maps 中的一段内存映射。
 */
struct MapEntry {
    uintptr_t   start = 0;
    uintptr_t   end = 0;
    char        perms[5]{};
    std::string name;
    bool        readable = false;
    bool        writable = false;
};

/**
 * @brief 冻结写入项。
 *
 * active=false 时保留记录但冻结线程不会写入。
 */
struct FreezeItem {
    uintptr_t address = 0;
    ValueType type = TYPE_DWORD;
    char      data[8]{};
    bool      active = true;
};

/**
 * @brief 特征码校验项。
 *
 * value  为待匹配数值的字符串形式；
 * offset 为相对主特征码地址的偏移。
 */
struct SignatureEntry {
    std::string value;
    ValueType   type = TYPE_DWORD;
    int         offset = 0;
};

/**
 * @brief 修改项。
 *
 * freeze=true 时，写入成功后会加入冻结列表。
 */
struct ModifyEntry {
    double    value = 0;
    ValueType type = TYPE_DWORD;
    int       offset = 0;
    bool      freeze = false;
};

/**
 * @brief 批量任务描述，便于业务层组织 SearchWrite/SearchRestore。
 */
struct SearchTask {
    std::string                 name;
    std::vector<SignatureEntry> sigs;
    std::vector<ModifyEntry>    mods;
    bool                        isRestore = false;
};

/**
 * @brief 单例内存搜索/读写引擎。
 *
 * 典型流程：
 * 1. MemoryEngine::instance().attach(pid/packageName) 或 attachSelf();
 * 2. setRegion()/setCustomRange() 设置搜索范围；
 * 3. searchNumber()/refineSearch() 搜索；
 * 4. read()/write()/SearchWrite()/SearchRestore() 读写或批量修改。
 *
 * 注意：
 * - read<T>() / write<T>() 是模板，必须保留在头文件中；
 * - 其他实现细节已移动到 MemoryEngine.cpp，降低头文件体积和编译耦合。
 */
class MemoryEngine {
public:
    /** @brief 获取全局单例实例。 */
    static MemoryEngine& instance();

    MemoryEngine(const MemoryEngine&) = delete;
    MemoryEngine& operator=(const MemoryEngine&) = delete;

    /** @brief 析构时会停止冻结线程并关闭已打开的 fd。 */
    ~MemoryEngine();

    /** @brief 通过包名查找 PID 并附加。成功返回 true。 */
    bool attach(const char* packageName);

    /** @brief 通过 PID 附加目标进程。成功返回 true。 */
    bool attach(pid_t pid);

    /** @brief 附加当前进程，适合注入/本地读写模式。 */
    bool attachSelf();

    /** @brief 当前是否已成功附加进程。 */
    [[nodiscard]] bool isAttached() const;

    /** @brief 返回当前附加的 PID，未附加时通常为 -1。 */
    [[nodiscard]] pid_t getPid() const;

    /** @brief 返回当前读写后端类型。 */
    [[nodiscard]] BackendType backend() const;

    /** @brief 返回当前后端名称："kernel" / "local" / "userspace" / "none"。 */
    [[nodiscard]] const char* backendName() const;

    /** @brief 设置搜索区域。 */
    void setRegion(MemoryRegion region);

    /** @brief 设置自定义搜索地址范围，同时会把 region 设置为 REGION_CUSTOM。 */
    void setCustomRange(uintptr_t start, uintptr_t end);

    /**
     * @brief 设置进度回调。
     *
     * pct 范围通常为 0~100；msg 为当前状态文本。
     */
    void setProgressCallback(std::function<void(int pct, const char* msg)> cb);

    /** @brief 初始化驱动密钥。仅 KERNEL 后端或已打开驱动时有效。 */
    bool initDriverKey(const char* key);

    /** @brief 调用驱动隐藏当前进程。驱动不支持时无效果。 */
    void hideProcess();

    /** @brief 获取模块基址；驱动失败时会回退到 maps 查询。 */
    uintptr_t getModuleBase(const char* name);

    /** @brief 读取原始字节。模板 read<T>() 基于此接口实现。 */
    bool readBytes(uintptr_t addr, void* buffer, size_t size);

    /** @brief 写入原始字节。模板 write<T>() 基于此接口实现。 */
    bool writeBytes(uintptr_t addr, const void* buffer, size_t size);

    /** @brief 按类型读取一个值；失败时返回 T{}。 */
    template <typename T>
    T read(uintptr_t addr) {
        T value{};
        readBytes(addr, &value, sizeof(T));
        return value;
    }

    /** @brief 按类型写入一个值。 */
    template <typename T>
    bool write(uintptr_t addr, const T& value) {
        return writeBytes(addr, &value, sizeof(T));
    }

    /** @brief 清空当前搜索结果。 */
    void clearResults();

    /** @brief 获取当前搜索结果数量。 */
    [[nodiscard]] int getResultCount() const;

    /**
     * @brief 获取搜索结果。
     *
     * max < 0 表示返回全部；max >= 0 表示最多返回 max 条。
     */
    [[nodiscard]] std::vector<SearchResult> getResults(int max = -1) const;

    /** @brief 在当前区域内搜索指定数值，返回命中数量。 */
    int searchNumber(const char* value, ValueType type);

    /** @brief 在已有结果中继续筛选，返回剩余数量。 */
    int refineSearch(const char* value, ValueType type);

    /**
     * @brief 特征码校验后批量写入。
     *
     * sigs[0] 为主特征码；后续 sigs 作为偏移校验。
     */
    int SearchWrite(const std::vector<SignatureEntry>& sigs,
                    const std::vector<ModifyEntry>& mods,
                    const char* name = "");

    /** @brief 特征码校验后批量恢复，同时移除对应冻结项。 */
    int SearchRestore(const std::vector<SignatureEntry>& sigs,
                      const std::vector<ModifyEntry>& restores,
                      const char* name = "");

    /** @brief 读取 UTF-16LE 字符串。 */
    std::u16string readUtf16(uintptr_t addr, int charCount);

    /** @brief 写入 UTF-16LE 字符串，不自动追加结束符。 */
    bool writeUtf16(uintptr_t addr, const std::u16string& str);

    /** @brief 读取以 \0 结尾的 UTF-8 字符串。 */
    std::string readUtf8(uintptr_t addr, int maxBytes = 1024);

    /** @brief 写入 UTF-8 字符串，并自动追加 \0。 */
    bool writeUtf8(uintptr_t addr, const std::string& str);

    /** @brief 读取 Unity il2cpp System.String。 */
    std::string readUnityString(uintptr_t strObj);

    /** @brief 写入 Unity il2cpp System.String，长度不会超过对象原容量。 */
    bool writeUnityString(uintptr_t strObj, const std::string& str);

    /** @brief 读取 Unity String[]。count < 0 时自动读取数组长度。 */
    std::vector<std::string> readUnityStringArray(uintptr_t arrayObj, int count = -1);

    /** @brief 添加冻结项。 */
    void addFreezeItem(const FreezeItem& item);

    /** @brief 按地址移除冻结项。 */
    void removeFreezeByAddress(uintptr_t addr);

    /** @brief 清空冻结列表。 */
    void clearFreezeList();

    /** @brief 获取冻结项数量。 */
    [[nodiscard]] int getFreezeCount() const;

    /** @brief 重新解析并返回目标进程内存映射。 */
    std::vector<MapEntry> getMemoryMaps();

    /** @brief 返回当前区域过滤后的总字节数。 */
    [[nodiscard]] size_t getRegionTotalSize();

    /** @brief 简易 UTF-16 转 UTF-8。 */
    static std::string utf16ToUtf8(const std::u16string& u16);

    /** @brief 简易 UTF-8 转 UTF-16。 */
    static std::u16string utf8ToUtf16(const std::string& u8);

private:
    MemoryEngine();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mem_engine
