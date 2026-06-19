#include "jvm_helper.h"
#include "dex_loader.h"
#include "native_register_vk.h"
#include "logger.h"

#include <pthread.h>
#include <unistd.h>

static pthread_mutex_t g_InitMutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_InitDone = false;


static void* native_thread_func(void*) {
    

    pthread_mutex_lock(&g_InitMutex);
    if (g_InitDone) { pthread_mutex_unlock(&g_InitMutex); return nullptr; }
    g_InitDone = true;
    pthread_mutex_unlock(&g_InitMutex);

    for (int i = 0; i < 20 && !g_JavaVM; i++) {
        g_JavaVM = obtainJavaVM();
        if (g_JavaVM && validateJavaVM(g_JavaVM)) break;
        g_JavaVM = nullptr;
        usleep(500 * 1000);
    }
    if (!g_JavaVM) { LOGE("[初始化] 未找到 JavaVM"); return nullptr; }

    sleep(2);

    jobject act = nullptr;
    for (int i = 0; i < 10 && !act; i++) { act = getCurrentActivityInstance(); sleep(1); }
    if (!act) { LOGE("[初始化] 未找到 Activity"); return nullptr; }

    JNIEnv *env = getJNIEnv();
    if (!env) return nullptr;
    g_ActivityInstance = env->NewGlobalRef(act);
    env->DeleteLocalRef(act);

    sleep(1);

    jclass cls = loadImGuiClassFromDex();
    if (!cls) { LOGE("[初始化] Dex 加载失败"); return nullptr; }
    sleep(1);
    if (!registerNativeMethods()) { LOGE("[初始化] 方法注册失败"); return nullptr; }
    sleep(1);
    if (!callImGuiSetupView(cls)) { LOGE("[初始化] 视图设置失败"); return nullptr; }

    LOGI("[初始化] ImGui 就绪 (Vulkan)");

    sleep(5);

    LOGI("[初始化] 主线程初始化完成");
    return nullptr;
}

__attribute__((constructor))
static void so_entry() {
    LOGI("[构造器] SO 已加载 (Vulkan build)");

    g_JavaVM = obtainJavaVM();

    pthread_t t;
    pthread_create(&t, nullptr, native_thread_func, nullptr);
    pthread_detach(t);
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    return JNI_VERSION_1_6;
}