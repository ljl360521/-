#ifndef dobby_h
#define dobby_h
#include <log.h>
// 混淆接口 (实际使用时需替换为真实函数名)
#if 0
#define DobbyBuildVersion c343f74888dffad84d9ad08d9c433456
#define DobbyHook c8dc3ffa44f22dbd10ccae213dd8b1f8
#define DobbyInstrument b71e27bca2c362de90c1034f19d839f9
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

// 日志设置函数
void log_set_level(int level);           // 设置日志级别
void log_switch_to_syslog();             // 切换到系统日志
void log_switch_to_file(const char *path); // 切换到文件日志

// 内存操作错误码
typedef enum {
  kMemoryOperationSuccess,                 // 内存操作成功
  kMemoryOperationError,                   // 内存操作错误
  kNotSupportAllocateExecutableMemory,     // 不支持分配可执行内存
  kNotEnough,                              // 内存不足
  kNone                                    // 无错误
} MemoryOperationError;

// 代码补丁工具接口
#define PLATFORM_INTERFACE_CODE_PATCH_TOOL_H
MemoryOperationError CodePatch(void *address, uint8_t *buffer, uint32_t buffer_size);

// 地址类型定义
typedef uintptr_t addr_t;
typedef uint32_t addr32_t;
typedef uint64_t addr64_t;

// 平台相关寄存器上下文定义
#if defined(__arm64__) || defined(__aarch64__)  // ARM64架构

#define ARM64_TMP_REG_NDX_0 17  // ARM64临时寄存器索引

// 浮点寄存器联合体
typedef union _FPReg {
  __int128_t q;  // 128位四字寄存器
  struct {
    double d1;   // 双精度浮点
    double d2;
  } d;
  struct {
    float f1;    // 单精度浮点
    float f2;
    float f3;
    float f4;
  } f;
} FPReg;

// ARM64寄存器上下文结构体
typedef struct _RegisterContext {
  uint64_t dmmpy_0; // 占位符
  uint64_t sp;      // 栈指针

  uint64_t dmmpy_1; // 占位符
  union {
    uint64_t x[29]; // 通用寄存器数组
    struct {        // 命名的通用寄存器
      uint64_t x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17, x18, x19, x20, x21, x22,
          x23, x24, x25, x26, x27, x28;
    } regs;
  } general;

  uint64_t fp;  // 帧指针
  uint64_t lr;  // 链接寄存器

  union {
    FPReg q[32];  // 浮点寄存器数组
    struct {      // 命名的浮点寄存器
      FPReg q0, q1, q2, q3, q4, q5, q6, q7;
      // [注意] 在ARM64架构下，默认不能访问q8-q31寄存器
      // 除非启用完整的浮点寄存器包
      FPReg q8, q9, q10, q11, q12, q13, q14, q15, q16, q17, q18, q19, q20, q21, q22, q23, q24, q25, q26, q27, q28, q29,
          q30, q31;
    } regs;
  } floating;
} RegisterContext;

#elif defined(__arm__)  // ARM32架构
typedef struct _RegisterContext {
  uint32_t dummy_0;
  uint32_t dummy_1;

  uint32_t dummy_2;
  uint32_t sp;  // 栈指针

  union {
    uint32_t r[13];  // 通用寄存器数组
    struct {         // 命名的通用寄存器
      uint32_t r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12;
    } regs;
  } general;

  uint32_t lr;  // 链接寄存器
} RegisterContext;

#elif defined(_M_IX86) || defined(__i386__)  // x86架构
typedef struct _RegisterContext {
  uint32_t dummy_0;
  uint32_t esp;  // 栈指针

  uint32_t dummy_1;
  uint32_t flags;  // 标志寄存器

  union {
    struct {      // 命名的通用寄存器
      uint32_t eax, ebx, ecx, edx, ebp, esp, edi, esi;
    } regs;
  } general;

} RegisterContext;

#elif defined(_M_X64) || defined(__x86_64__)  // x64架构
typedef struct _RegisterContext {
  uint64_t dummy_0;
  uint64_t rsp;  // 栈指针

  union {
    struct {      // 命名的通用寄存器
      uint64_t rax, rbx, rcx, rdx, rbp, rsp, rdi, rsi, r8, r9, r10, r11, r12, r13, r14, r15;
    } regs;
  } general;

  uint64_t dummy_1;
  uint64_t flags;  // 标志寄存器
} RegisterContext;
#endif

// 返回状态定义
#define RT_FAILED -1   // 操作失败
#define RT_SUCCESS 0   // 操作成功
typedef enum _RetStatus { RS_FAILED = -1, RS_SUCCESS = 0 } RetStatus;

// 钩子入口信息结构体
typedef struct _HookEntryInfo {
  int hook_id;  // 钩子ID
  union {
    void *target_address;      // 目标地址
    void *function_address;    // 函数地址
    void *instruction_address; // 指令地址
  };
} HookEntryInfo;

// DobbyWrap 已弃用，改用 DobbyInstrument
#if 0
// 包装函数类型定义
typedef void (*PreCallTy)(RegisterContext *ctx, const HookEntryInfo *info);  // 调用前回调
typedef void (*PostCallTy)(RegisterContext *ctx, const HookEntryInfo *info); // 调用后回调
int DobbyWrap(void *function_address, PreCallTy pre_call, PostCallTy post_call);
#endif

// 核心API函数

/**
 * 获取Dobby构建版本信息
 * @return 返回构建日期字符串
 */
const char *DobbyBuildVersion();

/**
 * 函数钩子
 * @param address 要钩取的函数地址
 * @param replace_call 替换函数地址
 * @param origin_call 保存原始函数指针的指针
 * @return 返回操作状态
 */
int DobbyHook(void *address, void *replace_call, void **origin_call);

/**
 * 动态二进制插桩(DBI)
 * [注意] 在ARM64架构下，默认不能访问q8-q31寄存器
 * @param address 要插桩的地址
 * @param dbi_call 插桩回调函数
 * @return 返回操作状态
 */
typedef void (*DBICallTy)(RegisterContext *ctx, const HookEntryInfo *info);
int DobbyInstrument(void *address, DBICallTy dbi_call);

/**
 * 销毁并恢复钩子
 * @param address 要恢复的函数地址
 * @return 返回操作状态
 */
int DobbyDestroy(void *address);

/**
 * 符号解析器
 * @param image_name 镜像名称(可空)
 * @param symbol_name 符号名称
 * @return 返回符号地址
 */
void *DobbySymbolResolver(const char *image_name, const char *symbol_name);

/**
 * 全局偏移表(GOT)替换
 * @param image_name 镜像名称
 * @param symbol_name 符号名称
 * @param fake_func 伪造函数地址
 * @param orig_func 保存原始函数指针的指针
 * @return 返回操作状态
 */
int DobbyGlobalOffsetTableReplace(char *image_name, char *symbol_name, void *fake_func, void **orig_func);

// 平台特定功能
#if defined(__arm__) || defined(__arm64__) || defined(__aarch64__) || defined(_M_X64) || defined(__x86_64__)
/**
 * 启用近分支跳板
 */
void dobby_enable_near_branch_trampoline();

/**
 * 禁用近分支跳板
 */
void dobby_disable_near_branch_trampoline();
#endif

/**
 * 注册镜像加载回调
 * @param func 回调函数指针
 */
typedef void (*linker_load_callback_t)(const char *image_name, void *handle);
void dobby_register_image_load_callback(linker_load_callback_t func);

#ifdef __cplusplus
}
#endif

#endif
