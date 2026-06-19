#include "dex_loader.h"
#include "jvm_helper.h"
#include "classes_dex.h"
#include "logger.h"
#include <fcntl.h>
#include <unistd.h>

// 直接从内存创建 ByteBuffer，不写文件，避免 APKS 分包找不到依赖的问题
jobject createDexByteBuffer() {
    JNIEnv *env = getJNIEnv();
    if (!env || classes_dex_len <= 0) return nullptr;
    
    // ByteBuffer.allocateDirect(capacity) 或 wrap 已存在的 buffer
    // 使用 allocateDirect 创建 native-order 的 DirectByteBuffer
    jobject byteBuffer = env->NewDirectByteBuffer((void*)classes_dex, (jlong)classes_dex_len);
    if (!byteBuffer) {
        LOGE("[DEX] 创建DirectByteBuffer失败");
        return nullptr;
    }
    
    LOGI("[DEX] 已从内存创建ByteBuffer: %zu 字节", classes_dex_len);
    return byteBuffer;
}

jclass loadImGuiClassFromDex() {
    JNIEnv *env = getJNIEnv();
    if (!env) {
        LOGE("[DEX] 获取JNI环境失败");
        return nullptr;
    }
    
    env->ExceptionClear();

    // 从内存创建 ByteBuffer
    jobject dexBuffer = createDexByteBuffer();
    if (!dexBuffer) {
        LOGE("[DEX] 创建Dex ByteBuffer失败");
        return nullptr;
    }

    // 使用 InMemoryDexClassLoader（API 26+），直接从内存加载，不依赖文件路径
    // 构造函数: InMemoryDexClassLoader(ByteBuffer dexBuffer, ClassLoader parent)
    jclass imdclCls = env->FindClass("dalvik/system/InMemoryDexClassLoader");
    if (!imdclCls) {
        env->ExceptionClear();
        // 回退到 DexClassLoader（写入文件方式）
        LOGW("[DEX] InMemoryDexClassLoader不可用(需API 26+)，回退到文件方式");
        
        // 写入文件备用
        if (!g_ActivityInstance) {
            LOGE("[DEX] Activity实例未初始化，无法回退");
            return nullptr;
        }
        
        jclass ctxCls = env->FindClass("android/content/Context");
        jobject cacheDir = env->CallObjectMethod(g_ActivityInstance,
            env->GetMethodID(ctxCls, "getCacheDir", "()Ljava/io/File;"));
        env->ExceptionClear();
        
        jclass fileCls = env->FindClass("java/io/File");
        jstring pathStr = (jstring)env->CallObjectMethod(cacheDir,
            env->GetMethodID(fileCls, "getAbsolutePath", "()Ljava/lang/String;"));
        const char* cstr = env->GetStringUTFChars(pathStr, nullptr);
        std::string dexPathStr = std::string(cstr) + "/injected.dex";
        env->ReleaseStringUTFChars(pathStr, cstr);
        
        // 写入文件
        int fd = open(dexPathStr.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            write(fd, classes_dex, classes_dex_len);
            close(fd);
            LOGI("[DEX] 回退模式：写入DEX到 %s", dexPathStr.c_str());
        }
        
        jclass dclCls = env->FindClass("dalvik/system/DexClassLoader");
        jmethodID ctor = env->GetMethodID(dclCls, "<init>",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V");
        
        jclass objCls = env->FindClass("java/lang/Object");
        jobject parentCL = env->CallObjectMethod(objCls,
            env->GetMethodID(env->FindClass("java/lang/Class"), "getClassLoader", "()Ljava/lang/ClassLoader;"));
        
        jstring jDex = env->NewStringUTF(dexPathStr.c_str());
        jstring jEmpty = env->NewStringUTF("");
        jobject dcl = env->NewObject(dclCls, ctor, jDex, jEmpty, jEmpty, parentCL);
        
        if (!dcl) {
            env->ExceptionClear();
            LOGE("[DEX] 回退DexClassLoader也创建失败");
            return nullptr;
        }
        
        g_DexClassLoader = env->NewGlobalRef(dcl);
        LOGI("[DEX] 回退DexClassLoader创建成功");
        
        jmethodID loadCls = env->GetMethodID(dclCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
        jstring name = env->NewStringUTF("com.example.imgui.ImGui");
        jclass result = (jclass)env->CallObjectMethod(dcl, loadCls, name);
        
        if (!result) { env->ExceptionClear(); LOGE("[DEX] 加载ImGui类失败"); }
        else { LOGI("[DEX] 成功加载ImGui类"); }
        
        env->DeleteLocalRef(name); env->DeleteLocalRef(jEmpty); env->DeleteLocalRef(jDex);
        env->DeleteLocalRef(dcl); env->DeleteLocalRef(objCls); env->DeleteLocalRef(dclCls);
        env->DeleteLocalRef(pathStr); env->DeleteLocalRef(fileCls); 
        env->DeleteLocalRef(cacheDir); env->DeleteLocalRef(ctxCls);
        return result;
    }

    // === InMemoryDexClassLoader 路径（正常情况）===
    // 构造函数签名: (ByteBuffer, ClassLoader)  或  (ByteBuffer[], ClassLoader)
    // 先构造 ByteBuffer 数组
    jclass bbCls = env->FindClass("java/nio/ByteBuffer");
    jobjectArray dexBuffers = env->NewObjectArray(1, bbCls, dexBuffer);
    env->DeleteLocalRef(bbCls);

    // 获取父类加载器
    jclass objCls = env->FindClass("java/lang/Object");
    jobject parentCL = env->CallObjectMethod(objCls,
        env->GetMethodID(env->FindClass("java/lang/Class"), "getClassLoader", "()Ljava/lang/ClassLoader;"));

    // InMemoryDexClassLoader(ByteBuffer[] dexBuffers, ClassLoader parent)
    jmethodID ctor = env->GetMethodID(imdclCls, "<init>",
        "([Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");
    
    jobject imdcl = env->NewObject(imdclCls, ctor, dexBuffers, parentCL);
    
    if (!imdcl || env->ExceptionCheck()) {
        env->ExceptionClear();
        LOGE("[DEX] 创建InMemoryDexClassLoader失败");
        env->DeleteLocalRef(dexBuffers); env->DeleteLocalRef(objCls); env->DeleteLocalRef(imdclCls);
        return nullptr;
    }

    g_DexClassLoader = env->NewGlobalRef(imdcl);
    LOGI("[DEX] InMemoryDexClassLoader创建成功（内存加载）");

    // loadClass
    jmethodID loadCls = env->GetMethodID(imdclCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring name = env->NewStringUTF("com.example.imgui.ImGui");
    jclass result = (jclass)env->CallObjectMethod(imdcl, loadCls, name);

    if (!result || env->ExceptionCheck()) {
        env->ExceptionClear();
        LOGE("[DEX] 加载ImGui类失败");
    } else {
        LOGI("[DEX] 成功加载ImGui类（内存模式）");
    }

    env->DeleteLocalRef(name);
    env->DeleteLocalRef(dexBuffers);
    env->DeleteLocalRef(objCls);
    env->DeleteLocalRef(imdcl);
    env->DeleteLocalRef(imdclCls);

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