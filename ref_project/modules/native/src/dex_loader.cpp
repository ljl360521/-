#include "dex_loader.h"
#include "jvm_helper.h"
#include "classes_dex.h"
#include "logger.h"
#include <fcntl.h>
#include <unistd.h>

std::string releaseDexFile() {
    if (!g_ActivityInstance) {
        LOGE("[DEX] Activity实例未初始化");
        return "";
    }
    
    JNIEnv *env = getJNIEnv();
    if (!env) {
        LOGE("[DEX] 获取JNI环境失败");
        return "";
    }
    
    env->ExceptionClear();

    // 获取 cacheDir 路径
    jclass ctxCls = env->FindClass("android/content/Context");
    jobject cacheDir = env->CallObjectMethod(g_ActivityInstance,
        env->GetMethodID(ctxCls, "getCacheDir", "()Ljava/io/File;"));
    
    if (env->ExceptionCheck()) {
        LOGW("[DEX] 获取缓存目录异常");
        env->ExceptionClear();
    }
    
    jclass fileCls = env->FindClass("java/io/File");
    jstring pathStr = (jstring)env->CallObjectMethod(cacheDir,
        env->GetMethodID(fileCls, "getAbsolutePath", "()Ljava/lang/String;"));

    const char* cstr = env->GetStringUTFChars(pathStr, nullptr);
    std::string dexPath = std::string(cstr) + "/injected.dex";
    env->ReleaseStringUTFChars(pathStr, cstr);

    // 写入 dex
    LOGI("[DEX] 正在写入DEX文件到: %s", dexPath.c_str());
    int fd = open(dexPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { 
        LOGE("[DEX] 打开文件失败: %s", dexPath.c_str()); 
        return ""; 
    }
    
    if (classes_dex_len > 0) {
        ssize_t written = write(fd, classes_dex, classes_dex_len);
        if (written != (ssize_t)classes_dex_len) {
            LOGE("[DEX] 写入DEX文件失败，期望: %zu 字节，实际: %zd 字节", classes_dex_len, written);
            close(fd); 
            return "";
        }
        LOGI("[DEX] 成功写入DEX文件: %zu 字节", classes_dex_len);
    } else {
        LOGW("[DEX] DEX数据无效或为空");
    }
    
    close(fd);

    env->DeleteLocalRef(pathStr); 
    env->DeleteLocalRef(fileCls);
    env->DeleteLocalRef(cacheDir); 
    env->DeleteLocalRef(ctxCls);
    
    LOGI("[DEX] DEX文件已释放: %s", dexPath.c_str());
    return dexPath;
}

jclass loadImGuiClassFromDex() {
    JNIEnv *env = getJNIEnv();
    if (!env) {
        LOGE("[DEX] 获取JNI环境失败");
        return nullptr;
    }
    
    env->ExceptionClear();

    std::string dexPath = releaseDexFile();
    if (dexPath.empty()) {
        LOGE("[DEX] 释放DEX文件失败");
        return nullptr;
    }

    jclass dclCls = env->FindClass("dalvik/system/DexClassLoader");
    if (!dclCls) { 
        env->ExceptionClear(); 
        LOGW("[DEX] 未找到DexClassLoader类");
        return nullptr; 
    }

    jmethodID ctor = env->GetMethodID(dclCls, "<init>",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V");

    // 父类加载器
    jclass objCls = env->FindClass("java/lang/Object");
    jobject parentCL = env->CallObjectMethod(objCls,
        env->GetMethodID(env->FindClass("java/lang/Class"), "getClassLoader", "()Ljava/lang/ClassLoader;"));

    jstring jDex = env->NewStringUTF(dexPath.c_str());
    jstring jEmpty = env->NewStringUTF("");
    jobject dcl = env->NewObject(dclCls, ctor, jDex, jEmpty, jEmpty, parentCL);

    if (!dcl) { 
        env->ExceptionClear(); 
        LOGE("[DEX] 创建DexClassLoader失败");
        return nullptr; 
    }
    
    g_DexClassLoader = env->NewGlobalRef(dcl);
    LOGI("[DEX] DexClassLoader创建成功");

    jmethodID loadCls = env->GetMethodID(dclCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring name = env->NewStringUTF("com.example.imgui.ImGui");
    jclass result = (jclass)env->CallObjectMethod(dcl, loadCls, name);
    
    if (!result) { 
        env->ExceptionClear(); 
        LOGE("[DEX] 加载ImGui类失败"); 
    } else {
        LOGI("[DEX] 成功加载ImGui类");
    }

    env->DeleteLocalRef(name); 
    env->DeleteLocalRef(jEmpty); 
    env->DeleteLocalRef(jDex);
    env->DeleteLocalRef(dcl); 
    env->DeleteLocalRef(objCls); 
    env->DeleteLocalRef(dclCls);
    
    return result;
}

bool callImGuiSetupView(jclass imguiClass) {
    if (!imguiClass) {
        LOGE("[ImGui] ImGui类为空");
        return false;
    }
    
    if (!g_ActivityInstance) {
        LOGE("[ImGui] Activity实例为空");
        return false;
    }
    
    JNIEnv *env = getJNIEnv();
    if (!env) {
        LOGE("[ImGui] 获取JNI环境失败");
        return false;
    }
    
    env->ExceptionClear();

    jmethodID m = env->GetStaticMethodID(imguiClass,
        "setupImGuiViewOnMainThread", "(Landroid/app/Activity;)V");
    if (!m) { 
        env->ExceptionClear(); 
        LOGE("[ImGui] 未找到setupImGuiViewOnMainThread方法");
        return false; 
    }

    LOGI("[ImGui] 正在设置ImGui视图...");
    env->CallStaticVoidMethod(imguiClass, m, g_ActivityInstance);
    
    if (env->ExceptionCheck()) { 
        LOGE("[ImGui] 设置ImGui视图时发生异常");
        env->ExceptionDescribe(); 
        env->ExceptionClear(); 
        return false; 
    }

    LOGI("[ImGui] 视图设置完成");
    return true;
}
