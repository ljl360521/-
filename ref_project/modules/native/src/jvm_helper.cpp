#include "jvm_helper.h"
#include "logger.h"
#include <xdl.h>
#include <sys/system_properties.h>

JavaVM *g_JavaVM = nullptr;
jobject g_ActivityInstance = nullptr;
jobject g_DexClassLoader = nullptr;

// ---- 内部函数 ----

static JavaVM* getJavaVM_JNI_GetCreatedJavaVMs() {
    const char* libs[] = {"libnativehelper.so","libart.so","libdvm.so","libjavacore.so",nullptr};
    typedef jint (*Fn)(JavaVM**, jsize, jsize*);
    Fn func = nullptr;

    for (int i = 0; libs[i]; i++) {
        void* h = xdl_open(libs[i], XDL_DEFAULT);
        if (!h) continue;
        func = (Fn)xdl_sym(h, "JNI_GetCreatedJavaVMs", nullptr);
        if (func) { xdl_close(h); break; }
        xdl_close(h);
    }
    if (!func) {
        void* h = xdl_open(nullptr, XDL_DEFAULT);
        if (h) { func = (Fn)xdl_sym(h, "JNI_GetCreatedJavaVMs", nullptr); xdl_close(h); }
    }
    if (!func) {
        LOGW("[JavaVM] 通过JNI_GetCreatedJavaVMs获取虚拟机失败");
        return nullptr;
    }

    JavaVM* vms[1] = {nullptr}; jsize count = 0;
    if (func(vms, 1, &count) != JNI_OK || count == 0) {
        LOGW("[JavaVM] 调用JNI_GetCreatedJavaVMs返回失败或无虚拟机");
        return nullptr;
    }
    LOGI("[JavaVM] 通过JNI_GetCreatedJavaVMs获取虚拟机: %p", vms[0]);
    return vms[0];
}

static JavaVM* getJavaVM_AndroidRuntime() {
    void* h = xdl_open("libandroid_runtime.so", XDL_DEFAULT);
    if (!h) {
        LOGW("[JavaVM] 打开libandroid_runtime.so失败");
        return nullptr;
    }

    JavaVM** pp = (JavaVM**)xdl_sym(h, "_ZN7android14AndroidRuntime7mJavaVME", nullptr);
    if (pp && *pp) { 
        xdl_close(h); 
        LOGI("[JavaVM] 通过AndroidRuntime静态变量获取: %p", *pp);
        return *pp; 
    }

    typedef JavaVM* (*Fn)();
    Fn f = (Fn)xdl_sym(h, "_ZN7android14AndroidRuntime9getJavaVMEv", nullptr);
    if (!f) {
        LOGW("[JavaVM] AndroidRuntime.getJavaVM符号未找到");
        xdl_close(h);
        return nullptr;
    }
    
    JavaVM* vm = f();
    if (vm) {
        LOGI("[JavaVM] 通过AndroidRuntime.getJavaVM获取: %p", vm);
    } else {
        LOGW("[JavaVM] AndroidRuntime.getJavaVM返回空");
    }
    
    xdl_close(h);
    return vm;
}

// ---- 公开接口 ----

JavaVM* obtainJavaVM() {
    LOGI("[JavaVM] 开始获取Java虚拟机...");
    JavaVM* vm = getJavaVM_JNI_GetCreatedJavaVMs();
    if (vm) {
        LOGI("[JavaVM] 通过JNI_GetCreatedJavaVMs成功获取");
        return vm;
    }
    
    LOGI("[JavaVM] 尝试通过AndroidRuntime获取");
    vm = getJavaVM_AndroidRuntime();
    if (vm) {
        LOGI("[JavaVM] 通过AndroidRuntime成功获取");
    } else {
        LOGE("[JavaVM] 所有方法都无法获取Java虚拟机");
    }
    return vm;
}

bool validateJavaVM(JavaVM* vm) {
    if (!vm) {
        LOGW("[JavaVM] 验证虚拟机: 传入虚拟机为空");
        return false;
    }
    
    JNIEnv* env = nullptr;
    jint ret = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (ret == JNI_EDETACHED) {
        LOGI("[JavaVM] 当前线程未附加到虚拟机，尝试附加...");
        ret = vm->AttachCurrentThread(&env, nullptr);
    }
    
    bool valid = (ret == JNI_OK && env);
    if (valid) {
        LOGI("[JavaVM] 虚拟机验证成功");
    } else {
        LOGE("[JavaVM] 虚拟机验证失败，返回值: %d", ret);
    }
    return valid;
}

JNIEnv* getJNIEnv() {
    if (!g_JavaVM) {
        LOGW("[JNI] 未找到Java虚拟机实例");
        return nullptr;
    }
    
    JNIEnv *env = nullptr;
    jint ret = g_JavaVM->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (ret == JNI_EDETACHED) {
        LOGI("[JNI] 当前线程未附加，正在附加到虚拟机...");
        ret = g_JavaVM->AttachCurrentThread(&env, nullptr);
    }
    
    if (ret == JNI_OK && env) {
        return env;
    } else {
        LOGE("[JNI] 获取JNI环境失败，错误码: %d", ret);
        return nullptr;
    }
}

jobject getCurrentActivityInstance() {
    LOGI("[Activity] 开始获取当前Activity实例...");
    JNIEnv *env = getJNIEnv();
    if (!env) {
        LOGE("[Activity] 获取JNI环境失败");
        return nullptr;
    }
    
    env->ExceptionClear();

    jclass atClass = env->FindClass("android/app/ActivityThread");
    if (!atClass) { 
        LOGW("[Activity] 未找到ActivityThread类");
        env->ExceptionClear(); 
        return nullptr; 
    }

    jmethodID curAT = env->GetStaticMethodID(atClass, "currentActivityThread", "()Landroid/app/ActivityThread;");
    if (!curAT) { 
        LOGW("[Activity] 未找到currentActivityThread方法");
        env->ExceptionClear(); 
        env->DeleteLocalRef(atClass); 
        return nullptr; 
    }
    
    jobject at = env->CallStaticObjectMethod(atClass, curAT);
    if (!at) {
        LOGW("[Activity] currentActivityThread返回空");
        env->DeleteLocalRef(atClass);
        return nullptr;
    }

    jfieldID fld = env->GetFieldID(atClass, "mActivities", "Landroid/util/ArrayMap;");
    if (!fld) { 
        LOGI("[Activity] 尝试ArrayMap字段失败，尝试HashMap字段");
        env->ExceptionClear(); 
        fld = env->GetFieldID(atClass, "mActivities", "Ljava/util/HashMap;"); 
        env->ExceptionClear(); 
    }
    
    if (!fld) { 
        LOGE("[Activity] 未找到mActivities字段");
        env->DeleteLocalRef(at); 
        env->DeleteLocalRef(atClass); 
        return nullptr; 
    }

    jobject map = env->GetObjectField(at, fld);
    if (!map) {
        LOGW("[Activity] mActivities字段为空");
        env->DeleteLocalRef(at);
        env->DeleteLocalRef(atClass);
        return nullptr;
    }
    
    jclass mapCls = env->GetObjectClass(map);
    jobject vals = env->CallObjectMethod(map, env->GetMethodID(mapCls, "values", "()Ljava/util/Collection;"));
    jclass colCls = env->GetObjectClass(vals);
    jobjectArray arr = (jobjectArray)env->CallObjectMethod(vals, env->GetMethodID(colCls, "toArray", "()[Ljava/lang/Object;"));

    jobject result = nullptr;
    for (jsize i = 0, n = env->GetArrayLength(arr); i < n && !result; i++) {
        jobject rec = env->GetObjectArrayElement(arr, i);
        jclass rCls = env->GetObjectClass(rec);
        jfieldID af = env->GetFieldID(rCls, "activity", "Landroid/app/Activity;");
        if (af) result = env->GetObjectField(rec, af);
        env->DeleteLocalRef(rCls);
        env->DeleteLocalRef(rec);
    }

    env->DeleteLocalRef(arr); 
    env->DeleteLocalRef(colCls); 
    env->DeleteLocalRef(vals);
    env->DeleteLocalRef(mapCls); 
    env->DeleteLocalRef(map);
    env->DeleteLocalRef(at); 
    env->DeleteLocalRef(atClass);
    
    if (result) {
        LOGI("[Activity] 成功获取Activity实例");
    } else {
        LOGW("[Activity] 未找到Activity实例");
    }
    
    return result;
}
