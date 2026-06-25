#include <jni.h>
#include <android/log.h>
#include <pthread.h>
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cstring>
#include <string>
#include <errno.h>
#include "log.h"
#include "xdl.h"
#include "classes_dex.h"

// ============================================================================
// 【关键修改】使用环境变量 + 文件机制标记初始化状态
// 不依赖全局变量（会被重置），不依赖权限问题的 /dev/shm
// ============================================================================

#define INIT_FLAG_FILE "/data/user/0/com.ztgame.bob/cache/.imgui_initialized"
#define INIT_MARKER "INITIALIZED_20250208"

/**
 * 检查是否已初始化过
 * 这个函数会被两次调用：
 * 1. 构造函数中 dlopen() 时 (第一次)
 * 2. Java System.load() 时 (第二次)
 * 
 * 返回 true: 首次初始化
 * 返回 false: 已初始化过（跳过重复初始化）
 */
bool check_and_create_init_flag() {
    // 打开标记文件，如果存在则说明已初始化过
    int fd = open(INIT_FLAG_FILE, O_RDONLY);
    
    if (fd >= 0) {
        // 文件存在 - 已初始化过
        close(fd);
        LOGI("⚠️  初始化标记文件已存在，跳过重复初始化");
        return false;
    }
    
    // 文件不存在 - 首次初始化，创建标记文件
    fd = open(INIT_FLAG_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, INIT_MARKER, strlen(INIT_MARKER));
        close(fd);
        LOGI("✓ 创建初始化标记文件成功");
        return true;
    }
    
    // 无法创建文件 - 可能是权限问题，使用备用机制
    LOGW("⚠️  无法创建初始化标记文件，使用环境变量备用机制");
    
    // 备用方案：使用环境变量（进程内有效）
    const char* env_val = getenv("IMGUI_INITIALIZED");
    if (env_val != nullptr && strcmp(env_val, "1") == 0) {
        LOGI("⚠️  环境变量显示已初始化，跳过重复初始化");
        return false;
    }
    
    setenv("IMGUI_INITIALIZED", "1", 1);
    LOGI("✓ 设置环境变量标记，允许初始化");
    return true;
}

// ============================================================================
// 全局变量
// ============================================================================
JavaVM *g_JavaVM = nullptr;
jobject g_ActivityInstance = nullptr;
jobject g_MainLooperHandler = nullptr;

static pthread_mutex_t g_InitMutex = PTHREAD_MUTEX_INITIALIZER;

// ============================================================================
// JNI 环境获取
// ============================================================================

JNIEnv* getJNIEnv() {
    if (!g_JavaVM) {
        LOGE("JavaVM 未初始化");
        return nullptr;
    }
    
    JNIEnv *env = nullptr;
    jint ret = g_JavaVM->GetEnv((void**)&env, JNI_VERSION_1_6);
    
    if (ret == JNI_EDETACHED) {
        ret = g_JavaVM->AttachCurrentThread(&env, nullptr);
        if (ret != JNI_OK) {
            LOGE("附加线程失败: %d", ret);
            return nullptr;
        }
    }
    
    return env;
}

// ============================================================================
// 获取 JavaVM
// ============================================================================

bool getJavaVMViaReflection() {
    LOGI("尝试通过xdl获取JavaVM");
    
    const char* symbols[] = {
        "_ZN7android14AndroidRuntime9getJavaVMEv",
        "JNI_GetCreatedJavaVMs",
        "_Z20JNI_GetCreatedJavaVMs",
        nullptr
    };
    
    void* handle = xdl_open("libandroid_runtime.so", XDL_DEFAULT);
    if (!handle) {
        LOGI("无法打开libandroid_runtime.so");
        return false;
    }
    
    for (int i = 0; symbols[i] != nullptr; i++) {
        size_t symbol_size;
        void* symbol_addr = xdl_sym(handle, symbols[i], &symbol_size);
        
        if (!symbol_addr) {
            symbol_addr = xdl_dsym(handle, symbols[i], &symbol_size);
        }
        
        if (symbol_addr) {
            LOGI("找到符号: %s", symbols[i]);
            
            typedef jint (*JNI_GetCreatedJavaVMs_t)(JavaVM**, jsize, jsize*);
            auto jni_func = reinterpret_cast<JNI_GetCreatedJavaVMs_t>(symbol_addr);
            
            JavaVM* jvm_array[1];
            jint count = 0;
            jint result = jni_func(jvm_array, 1, &count);
            
            if (result == JNI_OK && count > 0 && jvm_array[0] != nullptr) {
                g_JavaVM = jvm_array[0];
                LOGI("✓ 成功获取JavaVM: %p", g_JavaVM);
                xdl_close(handle);
                return true;
            }
        }
    }
    
    xdl_close(handle);
    return false;
}

// ============================================================================
// 获取 Activity 实例
// ============================================================================

jobject getCurrentActivityInstance() {
    LOGI("获取当前Activity实例");
    
    JNIEnv *env = getJNIEnv();
    if (!env) {
        LOGE("无法获取JNI环境");
        return nullptr;
    }
    
    try {
        env->ExceptionClear();
        
        jclass activityThreadClass = env->FindClass("android/app/ActivityThread");
        if (!activityThreadClass) {
            env->ExceptionClear();
            LOGE("找不到ActivityThread类");
            return nullptr;
        }
        
        jmethodID currentActivityThreadMethod = env->GetStaticMethodID(
            activityThreadClass,
            "currentActivityThread",
            "()Landroid/app/ActivityThread;"
        );
        
        if (!currentActivityThreadMethod) {
            env->ExceptionClear();
            LOGE("找不到currentActivityThread方法");
            env->DeleteLocalRef(activityThreadClass);
            return nullptr;
        }
        
        jobject activityThread = env->CallStaticObjectMethod(
            activityThreadClass, 
            currentActivityThreadMethod
        );
        
        jfieldID activitiesField = env->GetFieldID(
            activityThreadClass, 
            "mActivities", 
            "Landroid/util/ArrayMap;"
        );
        
        if (!activitiesField) {
            activitiesField = env->GetFieldID(
                activityThreadClass, 
                "mActivities", 
                "Ljava/util/HashMap;"
            );
            env->ExceptionClear();
        }
        
        if (!activitiesField) {
            LOGE("找不到mActivities字段");
            env->DeleteLocalRef(activityThread);
            env->DeleteLocalRef(activityThreadClass);
            return nullptr;
        }
        
        jobject activitiesMap = env->GetObjectField(activityThread, activitiesField);
        if (!activitiesMap) {
            LOGE("mActivities为null");
            env->DeleteLocalRef(activityThread);
            env->DeleteLocalRef(activityThreadClass);
            return nullptr;
        }
        
        jclass mapClass = env->GetObjectClass(activitiesMap);
        jmethodID valuesMethod = env->GetMethodID(mapClass, "values", "()Ljava/util/Collection;");
        jobject values = env->CallObjectMethod(activitiesMap, valuesMethod);
        jclass collectionClass = env->GetObjectClass(values);
        jmethodID toArrayMethod = env->GetMethodID(collectionClass, "toArray", "()[Ljava/lang/Object;");
        jobjectArray array = (jobjectArray)env->CallObjectMethod(values, toArrayMethod);
        jsize length = env->GetArrayLength(array);
        
        LOGI("找到 %d 个Activity", (int)length);
        
        jobject mainActivity = nullptr;
        
        for (jsize i = 0; i < length; i++) {
            jobject record = env->GetObjectArrayElement(array, i);
            jclass recordClass = env->GetObjectClass(record);
            
            jfieldID activityField = env->GetFieldID(recordClass, "activity", "Landroid/app/Activity;");
            
            if (activityField) {
                jobject activity = env->GetObjectField(record, activityField);
                if (activity) {
                    jclass activityClass = env->GetObjectClass(activity);
                    jmethodID getClassMethod = env->GetMethodID(
                        env->FindClass("java/lang/Object"), "getClass", "()Ljava/lang/Class;"
                    );
                    
                    jclass cls = (jclass)env->CallObjectMethod(activity, getClassMethod);
                    jmethodID getNameMethod = env->GetMethodID(
                        env->FindClass("java/lang/Class"), "getName", "()Ljava/lang/String;"
                    );
                    
                    jstring className = (jstring)env->CallObjectMethod(cls, getNameMethod);
                    const char* classNameStr = env->GetStringUTFChars(className, nullptr);
                    
                    LOGI("Activity[%d] 类名: %s", (int)i, classNameStr);
                    
                    if (strstr(classNameStr, "UnityPlayerActivity") != nullptr) {
                        mainActivity = activity;
                        LOGI("✓ 找到UnityPlayerActivity");
                    } else if (mainActivity == nullptr) {
                        mainActivity = activity;
                    }
                    
                    env->ReleaseStringUTFChars(className, classNameStr);
                    env->DeleteLocalRef(className);
                    env->DeleteLocalRef(cls);
                    env->DeleteLocalRef(activityClass);
                }
            }
            
            env->DeleteLocalRef(recordClass);
            env->DeleteLocalRef(record);
            
            if (mainActivity != nullptr) break;
        }
        
        env->DeleteLocalRef(array);
        env->DeleteLocalRef(collectionClass);
        env->DeleteLocalRef(values);
        env->DeleteLocalRef(mapClass);
        env->DeleteLocalRef(activitiesMap);
        env->DeleteLocalRef(activityThread);
        env->DeleteLocalRef(activityThreadClass);
        
        return mainActivity;
        
    } catch (const std::exception &e) {
        LOGE("异常: %s", e.what());
        return nullptr;
    }
}

// ============================================================================
// 初始化 Handler
// ============================================================================

bool initMainThreadHandler() {
    LOGI("初始化主线程Handler");
    
    if (!g_ActivityInstance) {
        LOGE("Activity实例未初始化");
        return false;
    }
    
    JNIEnv *env = getJNIEnv();
    if (!env) {
        LOGE("无法获取JNI环境");
        return false;
    }
    
    try {
        env->ExceptionClear();
        
        jclass looperClass = env->FindClass("android/os/Looper");
        jmethodID getMainLooperMethod = env->GetStaticMethodID(
            looperClass, "getMainLooper", "()Landroid/os/Looper;"
        );
        
        jobject mainLooper = env->CallStaticObjectMethod(looperClass, getMainLooperMethod);
        if (!mainLooper) {
            LOGE("getMainLooper返回null");
            env->DeleteLocalRef(looperClass);
            return false;
        }
        
        jclass handlerClass = env->FindClass("android/os/Handler");
        jmethodID handlerConstructor = env->GetMethodID(
            handlerClass, "<init>", "(Landroid/os/Looper;)V"
        );
        
        jobject handler = env->NewObject(handlerClass, handlerConstructor, mainLooper);
        g_MainLooperHandler = env->NewGlobalRef(handler);
        
        LOGI("✓ Handler初始化成功");
        
        env->DeleteLocalRef(handler);
        env->DeleteLocalRef(handlerClass);
        env->DeleteLocalRef(mainLooper);
        env->DeleteLocalRef(looperClass);
        
        return true;
        
    } catch (const std::exception &e) {
        LOGE("异常: %s", e.what());
        return false;
    }
}

// ============================================================================
// 释放 Dex 文件
// ============================================================================

std::string releaseDexFile() {
    LOGI("释放Dex文件到缓存目录 (大小: %u 字节)", classes_dex_len);
    
    if (!g_ActivityInstance) {
        LOGE("Activity实例为空，无法释放Dex");
        return "";
    }
    
    JNIEnv *env = getJNIEnv();
    if (!env) {
        LOGE("无法获取JNI环境");
        return "";
    }
    
    try {
        env->ExceptionClear();
        
        jclass contextClass = env->FindClass("android/content/Context");
        jmethodID getCacheDirMethod = env->GetMethodID(
            contextClass, "getCacheDir", "()Ljava/io/File;"
        );
        
        jobject cacheDir = env->CallObjectMethod(g_ActivityInstance, getCacheDirMethod);
        
        jclass fileClass = env->FindClass("java/io/File");
        jmethodID getAbsolutePathMethod = env->GetMethodID(
            fileClass, "getAbsolutePath", "()Ljava/lang/String;"
        );
        
        jstring cacheDirPath = (jstring)env->CallObjectMethod(cacheDir, getAbsolutePathMethod);
        
        const char* cacheDirCStr = env->GetStringUTFChars(cacheDirPath, nullptr);
        std::string dexPath = std::string(cacheDirCStr) + "/injected.dex";
        env->ReleaseStringUTFChars(cacheDirPath, cacheDirCStr);
        
        LOGI("Dex文件路径: %s", dexPath.c_str());
        
        int fd = open(dexPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            LOGE("无法创建Dex文件: %s", strerror(errno));
            return "";
        }
        
        if (classes_dex_len > 0 && classes_dex != nullptr) {
            ssize_t written = write(fd, classes_dex, classes_dex_len);
            
            if (written != (ssize_t)classes_dex_len) {
                LOGE("写入Dex失败: 期望 %u 字节，实际写入 %zd 字节", 
                     classes_dex_len, written);
                close(fd);
                return "";
            }
            
            LOGI("✓ Dex已写入: %zd 字节", written);
        } else {
            LOGE("classes_dex为空或长度为0");
            close(fd);
            return "";
        }
        
        close(fd);
        
        struct stat st;
        if (stat(dexPath.c_str(), &st) == 0) {
            LOGI("✓ Dex文件已释放: %s (大小: %ld 字节)", dexPath.c_str(), st.st_size);
        }
        
        env->DeleteLocalRef(cacheDirPath);
        env->DeleteLocalRef(fileClass);
        env->DeleteLocalRef(cacheDir);
        env->DeleteLocalRef(contextClass);
        
        return dexPath;
        
    } catch (const std::exception &e) {
        LOGE("异常: %s", e.what());
        return "";
    }
}

// ============================================================================
// 加载 ImGui 类
// ============================================================================

jclass loadImGuiClassFromDex() {
    LOGI("创建DexClassLoader并加载ImGui类");
    
    JNIEnv *env = getJNIEnv();
    if (!env) {
        LOGE("无法获取JNI环境");
        return nullptr;
    }
    
    try {
        env->ExceptionClear();
        
        std::string dexPath = releaseDexFile();
        if (dexPath.empty()) {
            LOGE("释放Dex失败");
            return nullptr;
        }
        
        LOGI("创建DexClassLoader...");
        
        jclass dexClassLoaderClass = env->FindClass("dalvik/system/DexClassLoader");
        if (!dexClassLoaderClass) {
            env->ExceptionClear();
            LOGE("找不到DexClassLoader类");
            return nullptr;
        }
        
        jmethodID dexConstructor = env->GetMethodID(
            dexClassLoaderClass,
            "<init>",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V"
        );
        
        if (!dexConstructor) {
            env->ExceptionClear();
            LOGE("找不到DexClassLoader构造函数");
            return nullptr;
        }
        
        jclass classClass = env->FindClass("java/lang/Class");
        jmethodID getClassLoaderMethod = env->GetMethodID(
            classClass, "getClassLoader", "()Ljava/lang/ClassLoader;"
        );
        
        jobject parentClassLoader = env->CallObjectMethod(
            env->FindClass("java/lang/Object"), getClassLoaderMethod
        );
        
        jstring dexPathStr = env->NewStringUTF(dexPath.c_str());
        jstring optimizedDir = env->NewStringUTF("");
        jstring libraryPath = env->NewStringUTF("");
        
        jobject dexClassLoader = env->NewObject(
            dexClassLoaderClass, dexConstructor,
            dexPathStr, optimizedDir, libraryPath, parentClassLoader
        );
        
        if (!dexClassLoader) {
            env->ExceptionClear();
            LOGE("创建DexClassLoader失败");
            return nullptr;
        }
        
        LOGI("✓ DexClassLoader创建成功");
        
        LOGI("加载ImGui类...");
        
        jmethodID loadClassMethod = env->GetMethodID(
            dexClassLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;"
        );
        
        jstring classNameStr = env->NewStringUTF("com.example.imgui.ImGui");
        jclass imguiClass = (jclass)env->CallObjectMethod(
            dexClassLoader, loadClassMethod, classNameStr
        );
        
        if (!imguiClass) {
            env->ExceptionClear();
            LOGE("ImGui类加载失败");
            env->DeleteLocalRef(classNameStr);
            env->DeleteLocalRef(libraryPath);
            env->DeleteLocalRef(optimizedDir);
            env->DeleteLocalRef(dexPathStr);
            env->DeleteLocalRef(dexClassLoader);
            env->DeleteLocalRef(classClass);
            env->DeleteLocalRef(dexClassLoaderClass);
            return nullptr;
        }
        
        LOGI("✓ ImGui类加载成功: %p", imguiClass);
        
        env->DeleteLocalRef(classNameStr);
        env->DeleteLocalRef(libraryPath);
        env->DeleteLocalRef(optimizedDir);
        env->DeleteLocalRef(dexPathStr);
        env->DeleteLocalRef(dexClassLoader);
        env->DeleteLocalRef(classClass);
        env->DeleteLocalRef(dexClassLoaderClass);
        
        return imguiClass;
        
    } catch (const std::exception &e) {
        LOGE("异常: %s", e.what());
        return nullptr;
    }
}

// ============================================================================
// 获取缓存目录
// ============================================================================

std::string get_app_cache_dir() {
    JNIEnv *env = getJNIEnv();
    if (!env || !g_ActivityInstance) {
        LOGE("无法获取缓存目录");
        return "";
    }
    
    jclass contextClass = env->FindClass("android/content/Context");
    jmethodID getCacheDirMethod = env->GetMethodID(
        contextClass, "getCacheDir", "()Ljava/io/File;"
    );
    
    jobject cacheDir = env->CallObjectMethod(g_ActivityInstance, getCacheDirMethod);
    
    jclass fileClass = env->FindClass("java/io/File");
    jmethodID getAbsolutePathMethod = env->GetMethodID(
        fileClass, "getAbsolutePath", "()Ljava/lang/String;"
    );
    
    jstring cacheDirPath = (jstring)env->CallObjectMethod(cacheDir, getAbsolutePathMethod);
    const char* cacheDirCStr = env->GetStringUTFChars(cacheDirPath, nullptr);
    std::string result = std::string(cacheDirCStr);
    
    env->ReleaseStringUTFChars(cacheDirPath, cacheDirCStr);
    env->DeleteLocalRef(cacheDirPath);
    env->DeleteLocalRef(fileClass);
    env->DeleteLocalRef(cacheDir);
    env->DeleteLocalRef(contextClass);
    
    return result;
}

// ============================================================================
// 提取自身 .so 文件
// ============================================================================

bool extract_current_so(const std::string& targetPath) {
    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) {
        LOGE("无法打开/proc/self/maps");
        return false;
    }
    
    char line[512];
    std::string soPath;
    void* selfAddr = (void*)extract_current_so;
    
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, ".so") && strstr(line, "r-xp")) {
            void* start, *end;
            char perms[5], offset[9], dev[6], inode[9], path[256];
            
            if (sscanf(line, "%p-%p %4s %8s %5s %8s %255s", 
                      &start, &end, perms, offset, dev, inode, path) >= 6) {
                
                if (selfAddr >= start && selfAddr < end) {
                    soPath = path;
                    LOGI("找到自身.so映射: %s", soPath.c_str());
                    break;
                }
            }
        }
    }
    fclose(fp);
    
    if (soPath.empty()) {
        LOGE("未找到当前.so的内存映射");
        return false;
    }
    
    std::ifstream src(soPath, std::ios::binary);
    std::ofstream dst(targetPath, std::ios::binary);
    
    if (!src.is_open() || !dst.is_open()) {
        if (src.is_open()) src.close();
        if (dst.is_open()) dst.close();
        return false;
    }
    
    dst << src.rdbuf();
    src.close();
    dst.close();
    
    LOGI("✓ 已提取.so文件: %s -> %s", soPath.c_str(), targetPath.c_str());
    return true;
}

// ============================================================================
// 调用 ImGui.setupImGuiViewOnMainThread()
// ============================================================================

bool callImGuiSetupView(jclass imguiClass) {
    LOGI("调用ImGui.setupImGuiViewOnMainThread()方法");
    
    if (!imguiClass) {
        LOGE("ImGui类为空");
        return false;
    }
    
    if (!g_ActivityInstance) {
        LOGE("Activity实例未初始化");
        return false;
    }
    
    JNIEnv *env = getJNIEnv();
    if (!env) {
        LOGE("无法获取JNI环境");
        return false;
    }
    
    try {
        env->ExceptionClear();
        
        jmethodID setupMethod = env->GetStaticMethodID(
            imguiClass, "setupImGuiViewOnMainThread", "(Landroid/app/Activity;)V"
        );
        
        if (!setupMethod) {
            env->ExceptionClear();
            LOGE("找不到setupImGuiViewOnMainThread方法");
            return false;
        }
        
        LOGI("✓ 成功找到setupImGuiViewOnMainThread方法");
        LOGI("正在通过Handler在主线程中调用setupImGuiView...");
        
        env->CallStaticVoidMethod(imguiClass, setupMethod, g_ActivityInstance);
        
        if (env->ExceptionCheck()) {
            LOGE("调用setupImGuiViewOnMainThread时发生异常!");
            env->ExceptionDescribe();
            env->ExceptionClear();
            return false;
        }
        
        LOGI("✓✓✓ setupImGuiViewOnMainThread方法调用成功！");
        return true;
        
    } catch (const std::exception &e) {
        LOGE("异常: %s", e.what());
        return false;
    }
}

// ============================================================================
// Native 线程入口 - 【关键改进】
// ============================================================================

void* native_thread_func(void *arg) {
    // 【唯一的初始化检查】
    if (!check_and_create_init_flag()) {
        LOGI("检测到已初始化，跳过重复初始化");
        return nullptr;
    }
    
    LOGI("========== Native线程启动 ==========");
    LOGI("Dex大小: %u 字节", classes_dex_len);
    
    // 1. 获取JavaVM
    if (!getJavaVMViaReflection()) {
        LOGE("❌ 获取JavaVM失败");
        return nullptr;
    }
    
    sleep(2);
    
    // 2. 获取Activity实例
    jobject activityLocal = getCurrentActivityInstance();
    if (!activityLocal) {
        LOGE("❌ 获取Activity失败");
        return nullptr;
    }
    
    JNIEnv *env = getJNIEnv();
    if (!env) {
        LOGE("❌ 无法获取JNI环境来保存Activity全局引用");
        return nullptr;
    }
    
    g_ActivityInstance = env->NewGlobalRef(activityLocal);
    env->DeleteLocalRef(activityLocal);
    
    if (!g_ActivityInstance) {
        LOGE("❌ 创建Activity全局引用失败");
        return nullptr;
    }
    
    LOGI("✓ 成功获取并保存Activity全局引用: %p", g_ActivityInstance);
    sleep(1);
    
    // 3. 初始化Handler
    if (!initMainThreadHandler()) {
        LOGE("❌ 初始化Handler失败");
        return nullptr;
    }
    sleep(1);
    
    // 4. 加载ImGui类
    jclass imguiClass = loadImGuiClassFromDex();
    if (!imguiClass) {
        LOGE("❌ 加载ImGui类失败");
        return nullptr;
    }
    sleep(1);
    
    // 5. 提取自身.so文件
    if (g_ActivityInstance) {
        std::string cacheDir = get_app_cache_dir();
        if (!cacheDir.empty()) {
            std::string soPath = cacheDir + "/libimgui.so";
            LOGI("正在提取当前.so文件到: %s", soPath.c_str());
            
            if (extract_current_so(soPath)) {
                LOGI("✓ 成功提取自身动态库到: %s", soPath.c_str());
            } else {
                LOGE("❌ 提取.so文件失败");
            }
        }
    }
    sleep(1);
    
    // 6. 调用ImGui.setupImGuiViewOnMainThread()
    if (!callImGuiSetupView(imguiClass)) {
        LOGE("❌ 调用setupImGuiViewOnMainThread失败");
        return nullptr;
    }
    
    LOGI("========== ✓✓✓ ImGui视图初始化成功 ==========");
    LOGI("✓ ImGui已成功注入并初始化！");
    LOGI("✓ 所有操作完成！\n");
    
    return nullptr;
}

// ============================================================================
// SO 加载时初始化 - 【被调用两次，但只初始化一次】
// ============================================================================

__attribute__((constructor)) void _init() {
    LOGI("💉 SO已加载 - 开始注入ImGui");
    LOGI("包含Dex大小: %u 字节", classes_dex_len);
    
    // 创建线程来执行初始化
    // native_thread_func 会检查初始化标记，只初始化一次
    pthread_t t;
    pthread_create(&t, nullptr, native_thread_func, nullptr);
    pthread_detach(t);
}