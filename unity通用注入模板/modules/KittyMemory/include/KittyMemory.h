//
//  KittyMemory.h
//
//  Created by MJ (Ruit) on 1/1/19.
//  Enhanced with safe memory operations
//

#pragma once

#include <stdio.h>
#include <string>
#include <unistd.h>
#include <sys/mman.h>
#include <vector>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

#define _SYS_PAGE_SIZE_ (sysconf(_SC_PAGE_SIZE))

#define _PAGE_START_OF_(x)    ((uintptr_t)x & ~(uintptr_t)(_SYS_PAGE_SIZE_ - 1))
#define _PAGE_END_OF_(x, len) (_PAGE_START_OF_((uintptr_t)x + len - 1))
#define _PAGE_LEN_OF_(x, len) (_PAGE_END_OF_(x, len) - _PAGE_START_OF_(x) + _SYS_PAGE_SIZE_)
#define _PAGE_OFFSET_OF_(x)   ((uintptr_t)x - _PAGE_START_OF_(x))

#define _PROT_RWX_ (PROT_READ | PROT_WRITE | PROT_EXEC)
#define _PROT_RX_  (PROT_READ | PROT_EXEC)

#define EMPTY_VEC_OFFSET std::vector<int>()

// ==================== KittyUtils ====================
namespace KittyUtils {
    bool validateHexString(std::string &xstr);
    void toHex(void *const data, const size_t dataLength, std::string &dest);
    void fromHex(const std::string &in, void *const data);
}

// ==================== KittyMemory ====================
namespace KittyMemory {

    typedef enum {
        FAILED = 0,
        SUCCESS = 1,
        INV_ADDR = 2,
        INV_LEN = 3,
        INV_BUF = 4,
        INV_PROT = 5,
        INV_MAP = 6        // 新增：地址不在有效映射范围
    } Memory_Status;

    struct ProcMap {
        void *startAddr;
        void *endAddr;
        size_t length;
        std::string perms;
        long offset;
        std::string dev;
        int inode;
        std::string pathname;

        bool isValid() const { 
            return (startAddr != nullptr && endAddr != nullptr && length > 0); 
        }
        
        bool isReadable() const { return !perms.empty() && perms[0] == 'r'; }
        bool isWritable() const { return perms.length() > 1 && perms[1] == 'w'; }
        bool isExecutable() const { return perms.length() > 2 && perms[2] == 'x'; }
    };

    // ==================== 内存映射查询 ====================
    
    /**
     * 获取指定库的内存映射信息
     */
    ProcMap getLibraryMap(const char *libraryName);
    
    /**
     * 获取包含指定地址的内存映射信息
     */
    ProcMap getAddressMap(uintptr_t address);
    
    /**
     * 获取所有内存映射
     */
    std::vector<ProcMap> getAllMaps();
    
    /**
     * 计算库的相对地址对应的绝对地址
     */
    uintptr_t getAbsoluteAddress(const char *libraryName, uintptr_t relativeAddr, bool useCache = false);

    // ==================== 地址有效性检查 ====================
    
    /**
     * 检查地址是否可读
     */
    bool isAddressReadable(uintptr_t address);
    
    /**
     * 检查地址是否可写
     */
    bool isAddressWritable(uintptr_t address);
    
    /**
     * 检查地址范围是否可读
     */
    bool isRangeReadable(uintptr_t address, size_t length);
    
    /**
     * 检查地址范围是否可写
     */
    bool isRangeWritable(uintptr_t address, size_t length);

    // ==================== 内存保护 ====================
    
    /**
     * 修改内存区域的保护属性
     */
    bool ProtectAddr(void *addr, size_t length, int protection);

    // ==================== 基础内存操作 ====================
    
    /**
     * 写入内存（不检查有效性，直接写入）
     */
    Memory_Status memWrite(void *addr, const void *buffer, size_t len);
    
    /**
     * 读取内存（不检查有效性，直接读取）
     */
    Memory_Status memRead(void *buffer, const void *addr, size_t len);
    
    /**
     * 读取内存并返回十六进制字符串
     */
    std::string read2HexStr(const void *addr, size_t len);

    // ==================== 安全内存操作 ====================
    
    /**
     * 安全写入内存（先检查地址有效性）
     */
    Memory_Status safeMemWrite(void *addr, const void *buffer, size_t len);
    
    /**
     * 安全读取内存（先检查地址有效性）
     */
    Memory_Status safeMemRead(void *buffer, const void *addr, size_t len);

    // ==================== 指针操作模板 ====================
    
    /**
     * 读取指针指向的值（不安全，可能崩溃）
     */
    template<typename Type>
    Type readPtr(void *ptr) {
        Type defaultVal = {};
        if (ptr == nullptr)
            return defaultVal;
        return *reinterpret_cast<Type *>(ptr);
    }

    /**
     * 写入指针指向的值（不安全，可能崩溃）
     */
    template<typename Type>
    bool writePtr(void *ptr, Type val) {
        if (ptr == nullptr)
            return false;
        *reinterpret_cast<Type *>(ptr) = val;
        return true;
    }
    
    /**
     * 安全读取指针指向的值（先检查地址有效性）
     */
    template<typename Type>
    bool safeReadPtr(void *ptr, Type &outValue) {
        if (ptr == nullptr)
            return false;
        if (!isAddressReadable(reinterpret_cast<uintptr_t>(ptr)))
            return false;
        outValue = *reinterpret_cast<Type *>(ptr);
        return true;
    }
    
    /**
     * 安全写入指针指向的值（先检查地址有效性）
     */
    template<typename Type>
    bool safeWritePtr(void *ptr, Type val) {
        if (ptr == nullptr)
            return false;
        if (!isAddressWritable(reinterpret_cast<uintptr_t>(ptr)))
            return false;
        *reinterpret_cast<Type *>(ptr) = val;
        return true;
    }

    // ==================== 多级指针操作 ====================
    
    /**
     * 遍历多级指针并读取最终值（不安全，可能崩溃）
     */
    template<typename Type>
    Type readMultiPtr(void *ptr, std::vector<int> offsets) {
        Type defaultVal = {};
        if (ptr == nullptr)
            return defaultVal;

        uintptr_t finalPtr = reinterpret_cast<uintptr_t>(ptr);
        int offsetsSize = offsets.size();
        
        if (offsetsSize > 0) {
            for (int i = 0; finalPtr != 0 && i < offsetsSize; i++) {
                if (i == (offsetsSize - 1))
                    return *reinterpret_cast<Type *>(finalPtr + offsets[i]);
                finalPtr = *reinterpret_cast<uintptr_t *>(finalPtr + offsets[i]);
            }
        }

        if (finalPtr == 0)
            return defaultVal;

        return *reinterpret_cast<Type *>(finalPtr);
    }

    /**
     * 遍历多级指针并写入最终值（不安全，可能崩溃）
     */
    template<typename Type>
    bool writeMultiPtr(void *ptr, std::vector<int> offsets, Type val) {
        if (ptr == nullptr)
            return false;

        uintptr_t finalPtr = reinterpret_cast<uintptr_t>(ptr);
        int offsetsSize = offsets.size();
        
        if (offsetsSize > 0) {
            for (int i = 0; finalPtr != 0 && i < offsetsSize; i++) {
                if (i == (offsetsSize - 1)) {
                    *reinterpret_cast<Type *>(finalPtr + offsets[i]) = val;
                    return true;
                }
                finalPtr = *reinterpret_cast<uintptr_t *>(finalPtr + offsets[i]);
            }
        }

        if (finalPtr == 0)
            return false;

        *reinterpret_cast<Type *>(finalPtr) = val;
        return true;
    }
    
    /**
     * 安全遍历多级指针并读取最终值（每一级都检查有效性）
     */
    template<typename Type>
    bool safeReadMultiPtr(void *ptr, std::vector<int> offsets, Type &outValue) {
        if (ptr == nullptr)
            return false;

        uintptr_t currentPtr = reinterpret_cast<uintptr_t>(ptr);
        int offsetsSize = offsets.size();
        
        for (int i = 0; i < offsetsSize; i++) {
            uintptr_t targetAddr = currentPtr + offsets[i];
            
            // 检查地址是否可读
            if (!isAddressReadable(targetAddr))
                return false;
            
            if (i == (offsetsSize - 1)) {
                // 最后一级，读取目标类型的值
                outValue = *reinterpret_cast<Type *>(targetAddr);
                return true;
            }
            
            // 中间级，读取指针值
            currentPtr = *reinterpret_cast<uintptr_t *>(targetAddr);
            if (currentPtr == 0)
                return false;
        }
        
        // offsets 为空时，直接读取 ptr 指向的值
        if (!isAddressReadable(currentPtr))
            return false;
        outValue = *reinterpret_cast<Type *>(currentPtr);
        return true;
    }
    
    /**
     * 安全遍历多级指针并写入最终值（每一级都检查有效性）
     */
    template<typename Type>
    bool safeWriteMultiPtr(void *ptr, std::vector<int> offsets, Type val) {
        if (ptr == nullptr)
            return false;

        uintptr_t currentPtr = reinterpret_cast<uintptr_t>(ptr);
        int offsetsSize = offsets.size();
        
        for (int i = 0; i < offsetsSize; i++) {
            uintptr_t targetAddr = currentPtr + offsets[i];
            
            if (i == (offsetsSize - 1)) {
                // 最后一级，写入目标值
                if (!isAddressWritable(targetAddr))
                    return false;
                *reinterpret_cast<Type *>(targetAddr) = val;
                return true;
            }
            
            // 中间级，读取指针值
            if (!isAddressReadable(targetAddr))
                return false;
            currentPtr = *reinterpret_cast<uintptr_t *>(targetAddr);
            if (currentPtr == 0)
                return false;
        }
        
        // offsets 为空时，直接写入 ptr 指向的地址
        if (!isAddressWritable(currentPtr))
            return false;
        *reinterpret_cast<Type *>(currentPtr) = val;
        return true;
    }
    
    /**
     * 安全遍历多级指针，返回最终地址（不读取值）
     * 成功返回最终地址，失败返回 0
     */
    uintptr_t safeGetMultiPtrAddress(void *ptr, std::vector<int> offsets);

}; // namespace KittyMemory

// ==================== MemoryPatch ====================
class MemoryPatch {
private:
    uintptr_t _address;
    size_t    _size;
    std::vector<uint8_t> _orig_code;
    std::vector<uint8_t> _patch_code;
    std::string _hexString;

public:
    MemoryPatch();
    MemoryPatch(const char *libraryName, uintptr_t address,
                const void *patch_code, size_t patch_size, bool useMapCache = true);
    MemoryPatch(uintptr_t absolute_address, const void *patch_code, size_t patch_size);
    ~MemoryPatch();

    static MemoryPatch createWithHex(const char *libraryName, uintptr_t address, 
                                     std::string hex, bool useMapCache = true);
    static MemoryPatch createWithHex(uintptr_t absolute_address, std::string hex);

    bool isValid() const;
    size_t get_PatchSize() const;
    uintptr_t get_TargetAddress() const;
    bool Restore();
    bool Modify();
    std::string get_CurrBytes();
};

// ==================== MemoryBackup ====================
class MemoryBackup {
private:
    uintptr_t _address;
    size_t    _size;
    std::vector<uint8_t> _orig_code;
    std::string _hexString;

public:
    MemoryBackup();
    MemoryBackup(const char *libraryName, uintptr_t address, size_t backup_size, bool useMapCache = true);
    MemoryBackup(uintptr_t absolute_address, size_t backup_size);
    ~MemoryBackup();

    bool isValid() const;
    size_t get_BackupSize() const;
    uintptr_t get_TargetAddress() const;
    bool Restore();
    std::string get_CurrBytes();
};