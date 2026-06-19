#include "obfuscate.h"
#include "KittyMemory.h"
#include "logger.h"

using KittyMemory::Memory_Status;
using KittyMemory::ProcMap;

// ==================== 内部缓存 ====================
struct mapsCache {
    std::string identifier;
    ProcMap map;
};

static std::vector<mapsCache> __mapsCache;

static ProcMap findMapInCache(const std::string &id) {
    ProcMap ret = {};
    for (size_t i = 0; i < __mapsCache.size(); i++) {
        if (__mapsCache[i].identifier == id) {
            ret = __mapsCache[i].map;
            break;
        }
    }
    return ret;
}

// ==================== KittyUtils 实现 ====================
namespace KittyUtils {

static void xtrim(std::string &hex) {
    if (hex.compare(0, 2, "0x") == 0) {
        hex.erase(0, 2);
    }
    hex.erase(std::remove_if(hex.begin(), hex.end(), [](char c) {
        return (c == ' ' || c == '\n' || c == '\r' ||
                c == '\t' || c == '\v' || c == '\f');
    }), hex.end());
}

bool validateHexString(std::string &xstr) {
    if (xstr.length() < 2) return false;
    xtrim(xstr);
    if (xstr.length() % 2 != 0) return false;
    for (size_t i = 0; i < xstr.length(); i++) {
        if (!std::isxdigit((unsigned char)xstr[i])) {
            return false;
        }
    }
    return true;
}

void toHex(void *const data, const size_t dataLength, std::string &dest) {
    unsigned char *byteData = reinterpret_cast<unsigned char*>(data);
    std::stringstream hexStringStream;
    
    hexStringStream << std::hex << std::setfill('0');
    for (size_t index = 0; index < dataLength; ++index)
        hexStringStream << std::setw(2) << static_cast<int>(byteData[index]);
    dest = hexStringStream.str();
}

void fromHex(const std::string &in, void *const data) {
    size_t length = in.length();
    unsigned char *byteData = reinterpret_cast<unsigned char*>(data);
    
    std::stringstream hexStringStream;
    hexStringStream >> std::hex;
    for (size_t strIndex = 0, dataIndex = 0; strIndex < length; ++dataIndex) {
        const char tmpStr[3] = { in[strIndex++], in[strIndex++], 0 };
        hexStringStream.clear();
        hexStringStream.str(tmpStr);
        int tmpValue = 0;
        hexStringStream >> tmpValue;
        byteData[dataIndex] = static_cast<unsigned char>(tmpValue);
    }
}

} // namespace KittyUtils

// ==================== KittyMemory 实现 ====================
namespace KittyMemory {

// ==================== 内存映射查询 ====================

ProcMap getLibraryMap(const char *libraryName) {
    ProcMap retMap = {};
    char line[512] = {0};

    FILE *fp = fopen(OBFUSCATE("/proc/self/maps"), OBFUSCATE("rt"));
    if (fp != nullptr) {
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, libraryName)) {
                char tmpPerms[5] = {0}, tmpDev[12] = {0}, tmpPathname[444] = {0};
                sscanf(line, "%llx-%llx %4s %ld %11s %d %443s",
                       (long long unsigned *)&retMap.startAddr,
                       (long long unsigned *)&retMap.endAddr,
                       tmpPerms, &retMap.offset, tmpDev, &retMap.inode, tmpPathname);

                retMap.length = (uintptr_t)retMap.endAddr - (uintptr_t)retMap.startAddr;
                retMap.perms = tmpPerms;
                retMap.dev = tmpDev;
                retMap.pathname = tmpPathname;
                break;
            }
        }
        fclose(fp);
    }
    return retMap;
}

ProcMap getAddressMap(uintptr_t address) {
    ProcMap retMap = {};
    char line[512] = {0};

    FILE *fp = fopen(OBFUSCATE("/proc/self/maps"), OBFUSCATE("rt"));
    if (fp != nullptr) {
        while (fgets(line, sizeof(line), fp)) {
            uintptr_t start = 0, end = 0;
            char tmpPerms[5] = {0}, tmpDev[12] = {0}, tmpPathname[444] = {0};
            long offset = 0;
            int inode = 0;
            
            int parsed = sscanf(line, "%llx-%llx %4s %ld %11s %d %443s",
                   (long long unsigned *)&start,
                   (long long unsigned *)&end,
                   tmpPerms, &offset, tmpDev, &inode, tmpPathname);
            
            if (parsed >= 5 && address >= start && address < end) {
                retMap.startAddr = (void*)start;
                retMap.endAddr = (void*)end;
                retMap.length = end - start;
                retMap.perms = tmpPerms;
                retMap.offset = offset;
                retMap.dev = tmpDev;
                retMap.inode = inode;
                retMap.pathname = (parsed >= 7) ? tmpPathname : "";
                break;
            }
        }
        fclose(fp);
    }
    return retMap;
}

std::vector<ProcMap> getAllMaps() {
    std::vector<ProcMap> maps;
    char line[512] = {0};

    FILE *fp = fopen(OBFUSCATE("/proc/self/maps"), OBFUSCATE("rt"));
    if (fp != nullptr) {
        while (fgets(line, sizeof(line), fp)) {
            ProcMap map = {};
            char tmpPerms[5] = {0}, tmpDev[12] = {0}, tmpPathname[444] = {0};
            
            int parsed = sscanf(line, "%llx-%llx %4s %ld %11s %d %443s",
                   (long long unsigned *)&map.startAddr,
                   (long long unsigned *)&map.endAddr,
                   tmpPerms, &map.offset, tmpDev, &map.inode, tmpPathname);
            
            if (parsed >= 5) {
                map.length = (uintptr_t)map.endAddr - (uintptr_t)map.startAddr;
                map.perms = tmpPerms;
                map.dev = tmpDev;
                map.pathname = (parsed >= 7) ? tmpPathname : "";
                maps.push_back(map);
            }
        }
        fclose(fp);
    }
    return maps;
}

uintptr_t getAbsoluteAddress(const char *libraryName, uintptr_t relativeAddr, bool useCache) {
    ProcMap libMap;

    if (useCache) {
        libMap = findMapInCache(libraryName);
        if (libMap.isValid())
            return (reinterpret_cast<uintptr_t>(libMap.startAddr) + relativeAddr);
    }

    libMap = getLibraryMap(libraryName);
    if (!libMap.isValid())
        return 0;

    if (useCache) {
        mapsCache cachedMap;
        cachedMap.identifier = libraryName;
        cachedMap.map = libMap;
        __mapsCache.push_back(cachedMap);
    }

    return (reinterpret_cast<uintptr_t>(libMap.startAddr) + relativeAddr);
}

// ==================== 地址有效性检查 ====================

bool isAddressReadable(uintptr_t address) {
    if (address == 0) return false;
    ProcMap map = getAddressMap(address);
    return map.isValid() && map.isReadable();
}

bool isAddressWritable(uintptr_t address) {
    if (address == 0) return false;
    ProcMap map = getAddressMap(address);
    return map.isValid() && map.isWritable();
}

bool isRangeReadable(uintptr_t address, size_t length) {
    if (address == 0 || length == 0) return false;
    
    // 检查起始地址
    ProcMap map = getAddressMap(address);
    if (!map.isValid() || !map.isReadable())
        return false;
    
    // 检查结束地址是否在同一个映射区域内
    uintptr_t endAddr = address + length - 1;
    if (endAddr >= (uintptr_t)map.endAddr) {
        // 跨越了映射边界，需要检查结束地址
        ProcMap endMap = getAddressMap(endAddr);
        if (!endMap.isValid() || !endMap.isReadable())
            return false;
    }
    
    return true;
}

bool isRangeWritable(uintptr_t address, size_t length) {
    if (address == 0 || length == 0) return false;
    
    ProcMap map = getAddressMap(address);
    if (!map.isValid() || !map.isWritable())
        return false;
    
    uintptr_t endAddr = address + length - 1;
    if (endAddr >= (uintptr_t)map.endAddr) {
        ProcMap endMap = getAddressMap(endAddr);
        if (!endMap.isValid() || !endMap.isWritable())
            return false;
    }
    
    return true;
}

// ==================== 内存保护 ====================

bool ProtectAddr(void *addr, size_t length, int protection) {
    uintptr_t pageStart = _PAGE_START_OF_(addr);
    uintptr_t pageLen = _PAGE_LEN_OF_(addr, length);
    return (mprotect(reinterpret_cast<void *>(pageStart), pageLen, protection) != -1);
}

// ==================== 基础内存操作 ====================

Memory_Status memWrite(void *addr, const void *buffer, size_t len) {
    if (addr == nullptr)
        return INV_ADDR;
    if (buffer == nullptr)
        return INV_BUF;
    if (len < 1 || len > INT_MAX)
        return INV_LEN;
    if (!ProtectAddr(addr, len, _PROT_RWX_))
        return INV_PROT;
    if (memcpy(addr, buffer, len) != nullptr && ProtectAddr(addr, len, _PROT_RX_))
        return SUCCESS;
    return FAILED;
}

Memory_Status memRead(void *buffer, const void *addr, size_t len) {
    if (addr == nullptr)
        return INV_ADDR;
    if (buffer == nullptr)
        return INV_BUF;
    if (len < 1 || len > INT_MAX)
        return INV_LEN;
    if (memcpy(buffer, addr, len) != nullptr)
        return SUCCESS;
    return FAILED;
}

std::string read2HexStr(const void *addr, size_t len) {
    std::string ret;
    if (addr == nullptr || len == 0)
        return ret;
        
    std::vector<char> temp(len, 0);
    if (memRead(temp.data(), addr, len) != SUCCESS)
        return ret;

    std::vector<char> buffer(len * 2 + 1, 0);
    for (size_t i = 0; i < len; i++) {
        sprintf(&buffer[i * 2], "%02X", (unsigned char)temp[i]);
    }

    ret = buffer.data();
    return ret;
}

// ==================== 安全内存操作 ====================

Memory_Status safeMemWrite(void *addr, const void *buffer, size_t len) {
    if (addr == nullptr)
        return INV_ADDR;
    if (buffer == nullptr)
        return INV_BUF;
    if (len < 1 || len > INT_MAX)
        return INV_LEN;
    
    // 先检查地址是否在有效映射范围内
    if (!isRangeReadable(reinterpret_cast<uintptr_t>(addr), len))
        return INV_MAP;
    
    return memWrite(addr, buffer, len);
}

Memory_Status safeMemRead(void *buffer, const void *addr, size_t len) {
    if (addr == nullptr)
        return INV_ADDR;
    if (buffer == nullptr)
        return INV_BUF;
    if (len < 1 || len > INT_MAX)
        return INV_LEN;
    
    // 先检查地址是否在有效映射范围内
    if (!isRangeReadable(reinterpret_cast<uintptr_t>(addr), len))
        return INV_MAP;
    
    return memRead(buffer, addr, len);
}

// ==================== 安全多级指针地址获取 ====================

uintptr_t safeGetMultiPtrAddress(void *ptr, std::vector<int> offsets) {
    if (ptr == nullptr)
        return 0;

    uintptr_t currentPtr = reinterpret_cast<uintptr_t>(ptr);
    int offsetsSize = offsets.size();
    
    for (int i = 0; i < offsetsSize; i++) {
        uintptr_t targetAddr = currentPtr + offsets[i];
        
        if (i == (offsetsSize - 1)) {
            // 最后一级，返回目标地址（不解引用）
            if (!isAddressReadable(targetAddr))
                return 0;
            return targetAddr;
        }
        
        // 中间级，读取指针值
        if (!isAddressReadable(targetAddr))
            return 0;
        currentPtr = *reinterpret_cast<uintptr_t *>(targetAddr);
        if (currentPtr == 0)
            return 0;
    }
    
    // offsets 为空时，返回 ptr 本身
    return currentPtr;
}

} // namespace KittyMemory

// ==================== MemoryPatch 实现 ====================

MemoryPatch::MemoryPatch() : _address(0), _size(0) {
    _orig_code.clear();
    _patch_code.clear();
}

MemoryPatch::MemoryPatch(const char *libraryName, uintptr_t address,
                         const void *patch_code, size_t patch_size, bool useMapCache) 
    : _address(0), _size(0) {
    
    if (libraryName == nullptr || address == 0 || patch_code == nullptr || patch_size < 1)
        return;

    _address = KittyMemory::getAbsoluteAddress(libraryName, address, useMapCache);
    if (_address == 0) return;

    _size = patch_size;
    _orig_code.resize(patch_size);
    _patch_code.resize(patch_size);

    KittyMemory::memRead(&_patch_code[0], patch_code, patch_size);
    KittyMemory::memRead(&_orig_code[0], reinterpret_cast<const void *>(_address), patch_size);
}

MemoryPatch::MemoryPatch(uintptr_t absolute_address, const void *patch_code, size_t patch_size) 
    : _address(0), _size(0) {
    
    if (absolute_address == 0 || patch_code == nullptr || patch_size < 1)
        return;

    _address = absolute_address;
    _size = patch_size;
    _orig_code.resize(patch_size);
    _patch_code.resize(patch_size);

    KittyMemory::memRead(&_patch_code[0], patch_code, patch_size);
    KittyMemory::memRead(&_orig_code[0], reinterpret_cast<const void *>(_address), patch_size);
}

MemoryPatch::~MemoryPatch() {
    _orig_code.clear();
    _patch_code.clear();
}

MemoryPatch MemoryPatch::createWithHex(const char *libraryName, uintptr_t address,
                                       std::string hex, bool useMapCache) {
    MemoryPatch patch;

    if (libraryName == nullptr || address == 0 || !KittyUtils::validateHexString(hex))
        return patch;

    patch._address = KittyMemory::getAbsoluteAddress(libraryName, address, useMapCache);
    if (patch._address == 0) return patch;

    patch._size = hex.length() / 2;
    patch._orig_code.resize(patch._size);
    patch._patch_code.resize(patch._size);

    KittyUtils::fromHex(hex, &patch._patch_code[0]);
    KittyMemory::memRead(&patch._orig_code[0], reinterpret_cast<const void *>(patch._address), patch._size);
    
    return patch;
}

MemoryPatch MemoryPatch::createWithHex(uintptr_t absolute_address, std::string hex) {
    MemoryPatch patch;

    if (absolute_address == 0 || !KittyUtils::validateHexString(hex))
        return patch;

    patch._address = absolute_address;
    patch._size = hex.length() / 2;
    patch._orig_code.resize(patch._size);
    patch._patch_code.resize(patch._size);

    KittyUtils::fromHex(hex, &patch._patch_code[0]);
    KittyMemory::memRead(&patch._orig_code[0], reinterpret_cast<const void *>(patch._address), patch._size);
    
    return patch;
}

bool MemoryPatch::isValid() const {
    return (_address != 0 && _size > 0 && 
            _orig_code.size() == _size && _patch_code.size() == _size);
}

size_t MemoryPatch::get_PatchSize() const {
    return _size;
}

uintptr_t MemoryPatch::get_TargetAddress() const {
    return _address;
}

bool MemoryPatch::Restore() {
    if (!isValid()) return false;
    return KittyMemory::memWrite(reinterpret_cast<void *>(_address), &_orig_code[0], _size) 
           == Memory_Status::SUCCESS;
}

bool MemoryPatch::Modify() {
    if (!isValid()) return false;
    return KittyMemory::memWrite(reinterpret_cast<void *>(_address), &_patch_code[0], _size) 
           == Memory_Status::SUCCESS;
}

std::string MemoryPatch::get_CurrBytes() {
    if (!isValid())
        _hexString = std::string(OBFUSCATE("0xInvalid"));
    else
        _hexString = KittyMemory::read2HexStr(reinterpret_cast<const void *>(_address), _size);
    return _hexString;
}

// ==================== MemoryBackup 实现 ====================

MemoryBackup::MemoryBackup() : _address(0), _size(0) {
    _orig_code.clear();
}

MemoryBackup::MemoryBackup(const char *libraryName, uintptr_t address, 
                           size_t backup_size, bool useMapCache) 
    : _address(0), _size(0) {
    
    if (libraryName == nullptr || address == 0 || backup_size < 1)
        return;

    _address = KittyMemory::getAbsoluteAddress(libraryName, address, useMapCache);
    if (_address == 0) return;

    _size = backup_size;
    _orig_code.resize(backup_size);
    KittyMemory::memRead(&_orig_code[0], reinterpret_cast<const void *>(_address), backup_size);
}

MemoryBackup::MemoryBackup(uintptr_t absolute_address, size_t backup_size) 
    : _address(0), _size(0) {
    
    if (absolute_address == 0 || backup_size < 1)
        return;

    _address = absolute_address;
    _size = backup_size;
    _orig_code.resize(backup_size);
    KittyMemory::memRead(&_orig_code[0], reinterpret_cast<const void *>(_address), backup_size);
}

MemoryBackup::~MemoryBackup() {
    _orig_code.clear();
}

bool MemoryBackup::isValid() const {
    return (_address != 0 && _size > 0 && _orig_code.size() == _size);
}

size_t MemoryBackup::get_BackupSize() const {
    return _size;
}

uintptr_t MemoryBackup::get_TargetAddress() const {
    return _address;
}

bool MemoryBackup::Restore() {
    if (!isValid()) return false;
    return KittyMemory::memWrite(reinterpret_cast<void *>(_address), &_orig_code[0], _size) 
           == Memory_Status::SUCCESS;
}

std::string MemoryBackup::get_CurrBytes() {
    if (!isValid())
        _hexString = std::string(OBFUSCATE("0xInvalid"));
    else
        _hexString = KittyMemory::read2HexStr(reinterpret_cast<const void *>(_address), _size);
    return _hexString;
}