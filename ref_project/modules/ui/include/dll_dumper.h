#ifndef DLL_DUMPER_H
#define DLL_DUMPER_H

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

struct DumpedAssembly {
    std::string name;
    uintptr_t   address;
    size_t      fileSize;
    bool        isDotNet;
};

class DllDumper {
public:
    DllDumper();
    ~DllDumper();

    /// 扫描内存找 .NET 程序集 (快速, 用 memmem)
    int scan();

    /// dump 到目录 (末尾带/)
    int dumpAll(const std::string& outputDir);

    const std::vector<DumpedAssembly>& getResults() const { return m_assemblies; }
    const std::string& getStatusMessage() const { return m_statusMsg; }
    void clear();

private:
    static bool   checkPE(const uint8_t* d, size_t n);
    static bool   checkDotNet(const uint8_t* d, size_t n);
    static size_t getPEFileSize(const uint8_t* d, size_t n);
    static std::string getAssemblyName(const uint8_t* d, size_t sz);
    static uint32_t rvaToOffset(const uint8_t* d, uint32_t pe, uint32_t rva);

    struct TableSizeContext {
        uint32_t rowCounts[64];
        int strIdxSize, guidIdxSize, blobIdxSize;
    };
    static int getTableRowSize(int idx, const TableSizeContext& c);
    static int codedIndexSize(const int* t, int cnt, int bits, const uint32_t* rc);
    static int ridSize(int t, const uint32_t* rc);

    bool isDuplicate(uintptr_t addr, size_t size) const;

    std::vector<DumpedAssembly> m_assemblies;
    std::string m_statusMsg;
};

#endif