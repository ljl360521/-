#include "jvm_helper.h"
#include "dex_loader.h"
#include "native_register_vk.h"
#include "il2cpp.h"
#include "logger.h"
#include "egl_hook.h"

#include <pthread.h>
#include <unistd.h>

static pthread_mutex_t g_InitMutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_InitDone = false;

static void* native_thread_func(void*) {
    // 重试早期 hook
    if (!g_il2cpp_tool->isObjectTrackingActive()) {
        for (int i = 0; i < 30; i++) {
            if (g_il2cpp_tool->startEarlyTracking()) {
                LOGI("[初始化] 早期对象追踪已启动 (重试次数: %d)", i);
                break;
            }
            usleep(200 * 1000);
        }
    }

    pthread_mutex_lock(&g_InitMutex);
    if (g_InitDone) { pthread_mutex_unlock(&g_InitMutex); return nullptr; }
    g_InitDone = true;
    pthread_mutex_unlock(&g_InitMutex);

    // 1. 获取 JavaVM
    for (int i = 0; i < 20 && !g_JavaVM; i++) {
        g_JavaVM = obtainJavaVM();
        if (g_JavaVM && validateJavaVM(g_JavaVM)) break;
        g_JavaVM = nullptr;
        usleep(500 * 1000);
    }
    if (!g_JavaVM) { LOGE("[初始化] 未找到 JavaVM"); return nullptr; }

    sleep(2);

    // 2. 获取 Activity
    jobject act = nullptr;
    for (int i = 0; i < 10 && !act; i++) { act = getCurrentActivityInstance(); sleep(1); }
    if (!act) { LOGE("[初始化] 未找到 Activity"); return nullptr; }

    JNIEnv *env = getJNIEnv();
    if (!env) return nullptr;
    g_ActivityInstance = env->NewGlobalRef(act);
    env->DeleteLocalRef(act);

    sleep(1);
    

    // 3. 加载 Dex → 注册 Native 方法 → 设置视图
    jclass cls = loadImGuiClassFromDex();
    if (!cls)                    { LOGE("[初始化] Dex 加载失败");    return nullptr; }
    sleep(1);
    if (!registerNativeMethods()){ LOGE("[初始化] 方法注册失败");   return nullptr; }
    sleep(1);
    if (!callImGuiSetupView(cls)){ LOGE("[初始化] 视图设置失败"); return nullptr; }

    LOGI("[初始化] ImGui 就绪");

    // 4. IL2CPP 完整初始化
    sleep(5);
    if (g_il2cpp_tool->initialize()) {
        LOGI("[初始化] IL2CPP 已初始化");
        g_il2cpp_tool->finalizeTracking();
    }
    //installEGLHooks();
    return nullptr;
}

__attribute__((constructor))
static void so_entry() {
    LOGI("[构造器] SO 已加载");

    g_il2cpp_tool = std::make_unique<IL2CPPRuntimeTool>();
    g_il2cpp_tool->startEarlyTracking();

    g_JavaVM = obtainJavaVM();

    pthread_t t;
    pthread_create(&t, nullptr, native_thread_func, nullptr);
    pthread_detach(t);
}
