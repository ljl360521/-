#include "native_register_vk.h"
#include "vulkan_helper.h"
#include "jvm_helper.h"
#include "ImGuiDraw.h"
#include "ImGuiInputIME.h"
#include "ImGuiMultiTouch.h"
#include "ImGuiKeyboard.h"
#include "CircularButton.h"
#include "gui.h"
#include "aura_ui.hpp"
#include "zt_ttf.h"
#include "Iconcpp.h"
#include "logger.h"

#include "imgui.h"
#include "imgui_impl_vulkan.h"

#include <android/native_window_jni.h>
#include <vector>

int   screenWidth = 0, screenHeight = 0;
float 屏宽 = 0, 屏高 = 0;
int   屏幕宽 = 0, 屏幕高 = 0;
bool  g_Initialized = false;
float g_MainWindowBounds[4] = {};
float g_DragBarBounds[4] = {};
bool  g_DragBarVisible = false;
float g_PopupBounds[4] = {};
bool  g_PopupVisible = false;
float g_IslandBounds[4] = {};   // 灵动岛边界
bool  g_IslandVisible = true;   // 灵动岛始终参与触摸命中

static void JNICALL nativeOnTouchEvent(
    JNIEnv* env, jclass, jint action, jint pointerIndex,
    jintArray pointerIds, jfloatArray pointerXs,
    jfloatArray pointerYs, jfloatArray pointerPressures, jint pointerCount)
{
    jfloat* xs  = env->GetFloatArrayElements(pointerXs, nullptr);
    jfloat* ys  = env->GetFloatArrayElements(pointerYs, nullptr);
    jint*   ids = env->GetIntArrayElements(pointerIds, nullptr);
    jfloat* pr  = env->GetFloatArrayElements(pointerPressures, nullptr);

    if (!g_Initialized) goto cleanup;

    {
        ImGuiIO& io = ImGui::GetIO();
        enum { DOWN=0, UP=1, MOVE=2, CANCEL=3, PTR_DOWN=5, PTR_UP=6 };

        switch (action) {
            case DOWN:
                if (pointerCount > 0) { io.MousePos = {xs[0], ys[0]}; io.MouseDown[0] = true; }
                break;
            case PTR_DOWN:
                if (pointerIndex < pointerCount) {
                    io.MousePos = {xs[pointerIndex], ys[pointerIndex]}; io.MouseDown[0] = true;
                }
                break;
            case MOVE:
                if (pointerCount > 0) io.MousePos = {xs[0], ys[0]};
                break;
            case UP:
                if (pointerCount > 0) io.MousePos = {xs[0], ys[0]};
                io.MouseDown[0] = false;
                break;
            case CANCEL:
                io.MouseDown[0] = false;
                break;
        }

        auto& mtm = ImGuiMultiTouch::MultiTouchManager::GetInstance();
        switch (action) {
            case DOWN:
                if (pointerCount > 0) mtm.EnqueueDown(ids[0], xs[0], ys[0], pr[0]);
                break;
            case PTR_DOWN:
                if (pointerIndex < pointerCount)
                    mtm.EnqueueDown(ids[pointerIndex], xs[pointerIndex], ys[pointerIndex], pr[pointerIndex]);
                break;
            case MOVE:
                for (int i = 0; i < pointerCount; i++) mtm.EnqueueMove(ids[i], xs[i], ys[i], pr[i]);
                break;
            case UP:
                if (pointerCount > 0) mtm.EnqueueUp(ids[0]);
                break;
            case PTR_UP:
                if (pointerIndex < pointerCount) mtm.EnqueueUp(ids[pointerIndex]);
                break;
            case CANCEL:
                mtm.EnqueueCancel();
                break;
        }
    }

cleanup:
    env->ReleaseFloatArrayElements(pointerXs, xs, JNI_ABORT);
    env->ReleaseFloatArrayElements(pointerYs, ys, JNI_ABORT);
    env->ReleaseIntArrayElements(pointerIds, ids, JNI_ABORT);
    env->ReleaseFloatArrayElements(pointerPressures, pr, JNI_ABORT);
}

static void JNICALL nativeOnKeyEvent(
    JNIEnv*, jclass, jint action, jint keyCode, jint scanCode,
    jint metaState, jint deviceId, jint source)
{
    auto& input = ImGuiKeyboard::InputManager::GetInstance();
    if (action == 0)      input.EnqueueKeyDown(keyCode, scanCode, metaState, deviceId, source);
    else if (action == 1) input.EnqueueKeyUp(keyCode, scanCode, metaState, deviceId, source);
}

static void JNICALL nativeOnCharInput(JNIEnv*, jclass, jint unicodeChar) {
    if (unicodeChar > 0 && g_Initialized)
        ImGui::GetIO().AddInputCharacter((unsigned int)unicodeChar);
}

static void JNICALL nativeOnMouseEvent(
    JNIEnv*, jclass, jint action, jfloat x, jfloat y,
    jint button, jfloat scrollX, jfloat scrollY,
    jint deviceId, jint source)
{
    auto& input = ImGuiKeyboard::InputManager::GetInstance();
    switch (action) {
        case 0: input.EnqueueMouseMove(x, y, deviceId, source);                    break;
        case 1: input.EnqueueMouseButtonDown(button, x, y, deviceId, source);      break;
        case 2: input.EnqueueMouseButtonUp(button, x, y, deviceId, source);        break;
        case 3: input.EnqueueMouseScroll(x, y, scrollX, scrollY, deviceId, source); break;
    }
}

static jfloatArray JNICALL nativeGetCircularButtonBounds(JNIEnv* env, jclass) {
    auto& mgr = TouchUI::ButtonManager::GetInstance();
    if (!mgr.IsRenderEnabled()) {
        jfloatArray empty = env->NewFloatArray(1);
        float zero = 0.0f;
        env->SetFloatArrayRegion(empty, 0, 1, &zero);
        return empty;
    }

    const auto& buttons = mgr.GetAllButtons();
    std::vector<TouchUI::CircularButton*> visible;
    for (auto& btn : buttons) {
        if (btn && btn->IsEnabled() && btn->ShouldRender())
            visible.push_back(btn.get());
    }

    int count = (int)visible.size();
    jfloatArray result = env->NewFloatArray(1 + count * 3);
    float fcount = (float)count;
    env->SetFloatArrayRegion(result, 0, 1, &fcount);

    for (int i = 0; i < count; i++) {
        float data[3] = { visible[i]->GetPosition().x, visible[i]->GetPosition().y,
                          visible[i]->GetRadius() };
        env->SetFloatArrayRegion(result, 1 + i * 3, 3, data);
    }
    return result;
}

static jboolean JNICALL isImGuiComponentTouched(JNIEnv*, jclass, jfloat x, jfloat y) {
    if (!g_Initialized) return JNI_FALSE;
    auto hit = [&](float* b) { return x >= b[0] && x <= b[2] && y >= b[1] && y <= b[3]; };
    // 灵动岛优先（始终命中，确保点击事件能传到 native 层触发切换）
    if (g_IslandVisible && hit(g_IslandBounds)) return JNI_TRUE;
    if (hit(g_MainWindowBounds)) return JNI_TRUE;
    if (g_DragBarVisible && hit(g_DragBarBounds)) return JNI_TRUE;
    if (g_PopupVisible  && hit(g_PopupBounds))   return JNI_TRUE;
    return JNI_FALSE;
}

static jfloatArray JNICALL nativeGetImGuiWindowBounds(JNIEnv* env, jclass) {
    if (!g_Initialized) return env->NewFloatArray(0);
    // 查询灵动岛当前边界（由 aura_ui 模块维护）
    float islandBounds[4] = {};
    bool hasIsland = aura_ui::get_dynamic_island_bounds(islandBounds);
    if (hasIsland) {
        g_IslandBounds[0] = islandBounds[0];
        g_IslandBounds[1] = islandBounds[1];
        g_IslandBounds[2] = islandBounds[2];
        g_IslandBounds[3] = islandBounds[3];
    }
    int count = 1 + (hasIsland?1:0) + (g_DragBarVisible?1:0) + (g_PopupVisible?1:0);
    jfloatArray result = env->NewFloatArray(count * 4);
    int off = 0;
    env->SetFloatArrayRegion(result, off, 4, g_MainWindowBounds); off += 4;
    if (hasIsland)        { env->SetFloatArrayRegion(result, off, 4, g_IslandBounds);  off += 4; }
    if (g_DragBarVisible) { env->SetFloatArrayRegion(result, off, 4, g_DragBarBounds); off += 4; }
    if (g_PopupVisible)   { env->SetFloatArrayRegion(result, off, 4, g_PopupBounds);   off += 4; }
    return result;
}


static void JNICALL nativeResize(JNIEnv*, jclass, jint w, jint h) {
    screenWidth = w; screenHeight = h;
    屏宽 = (float)w; 屏高 = (float)h;
    屏幕宽 = w; 屏幕高 = h;

    VulkanResize(w, h);

    if (g_Initialized) {
        ImGui::GetIO().DisplaySize = {(float)g_Vk.width, (float)g_Vk.height};
    }
}

static void JNICALL nativeInit(JNIEnv* env, jclass, jobject surface) {
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) {
        LOGE("[VK] ANativeWindow_fromSurface failed");
        return;
    }
    if (g_Vk.suspended && g_Vk.coreInitialized) {
        LOGI("[VK] nativeInit: resuming from suspend");

        if (!VulkanResume(window)) {
            LOGE("[VK] VulkanResume failed");
            ANativeWindow_release(window);
            return;
        }

        屏宽 = (float)g_Vk.width; 屏高 = (float)g_Vk.height;
        screenWidth = g_Vk.width; screenHeight = g_Vk.height;
        屏幕宽 = screenWidth; 屏幕高 = screenHeight;
        ImGui::GetIO().DisplaySize = {屏宽, 屏高};

        g_Initialized = true;
        LOGI("[VK] nativeInit resume complete (%dx%d)", screenWidth, screenHeight);
        return;
    }

    if (g_Initialized) {
        ANativeWindow_release(window);
        return;
    }

    LOGI("[VK] nativeInit: first-time initialization");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;

    // 字体加载（严格按旧项目 imguijni.cpp 移植）
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    config.OversampleH = 1;
    static const ImWchar icons_ranges[] = { 0xf000, 0xf3ff, 0 };
    ImFontConfig icons_config;
    icons_config.MergeMode = true;
    icons_config.PixelSnapH = true;
    icons_config.OversampleH = 2.5;
    icons_config.OversampleV = 2.5;
    icons_config.FontDataOwnedByAtlas = false;
    // 主字体 28px + FontAwesome 28px 合并
    io.Fonts->AddFontFromMemoryTTF((void*)zt_ttf, (int)zt_ttf_len, 28.0f, &config, io.Fonts->GetGlyphRangesChineseFull());
    io.Fonts->AddFontFromMemoryCompressedTTF((const void*)font_awesome_data, (int)font_awesome_size, 28.0f, &icons_config, icons_ranges);
    // Large 30px / Superlarge 40px
    io.Fonts->AddFontFromMemoryTTF((void*)zt_ttf, (int)zt_ttf_len, 30.0f, &config, io.Fonts->GetGlyphRangesChineseFull());
    io.Fonts->AddFontFromMemoryTTF((void*)zt_ttf, (int)zt_ttf_len, 40.0f, &config, io.Fonts->GetGlyphRangesChineseFull());
    io.Fonts->AddFontDefault(&config);

    ImGui::GetStyle().ScaleAllSizes(1.0f);
    auto& s = ImGui::GetStyle();
    s.WindowRounding = 5.3f; s.FrameRounding = 2.3f; s.ScrollbarRounding = 0;
    // 使用 ImGui 默认 Dark 主题（与旧项目一致，不调用任何主题函数）
    ImGui::StyleColorsDark();

    if (!VulkanInit(window)) {
        LOGE("[VK] VulkanInit failed");
        ANativeWindow_release(window);
        ImGui::DestroyContext();
        return;
    }

    屏宽 = (float)g_Vk.width; 屏高 = (float)g_Vk.height;
    screenWidth = g_Vk.width; screenHeight = g_Vk.height;
    屏幕宽 = screenWidth; 屏幕高 = screenHeight;
    io.DisplaySize = {屏宽, 屏高};

    ImGuiIME::Init(g_JavaVM);

    ImGuiMultiTouch::MultiTouchManager::GetInstance().Init();
    ImGuiKeyboard::InputManager::GetInstance().Init();

    ImGuiMultiTouch::MultiTouchManager::GetInstance().AddListener(
        &TouchUI::ButtonManager::GetInstance());
    ImGuiKeyboard::InputManager::GetInstance().AddKeyListener(
        &TouchUI::ButtonManager::GetInstance());

    g_Initialized = true;
    LOGI("[VK] nativeInit complete (%dx%d)", screenWidth, screenHeight);
}

static void JNICALL nativeStep(JNIEnv*, jclass) {
    if (!g_Initialized || g_Vk.suspended || !g_Vk.initialized) return;

    ImGuiMultiTouch::MultiTouchManager::GetInstance().NewFrame();
    ImGuiKeyboard::InputManager::GetInstance().NewFrame();

    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();

    绘制控件();

    ImGui::Render();

    VulkanRenderFrame();
}

static void JNICALL nativeShutdown(JNIEnv*, jclass) {
    if (!g_Initialized && !g_Vk.coreInitialized) return;

    LOGI("[VK] nativeShutdown (suspend mode)");

    g_Initialized = false;

    VulkanSuspend();

    LOGI("[VK] nativeShutdown complete (suspended, ready for resume)");
}

static void JNICALL motionEventClick(JNIEnv*, jobject, jboolean down, jfloat x, jfloat y) {
    if (!g_Initialized) return;
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDown[0] = down;
    io.MousePos = {x, y};
}

bool registerNativeMethods() {
    if (!g_JavaVM || !g_DexClassLoader) return false;
    JNIEnv *env = getJNIEnv();
    if (!env) return false;
    env->ExceptionClear();

    jclass dclCls = env->FindClass("dalvik/system/DexClassLoader");
    if (!dclCls) {
        LOGW("[JNI] 未找到 DexClassLoader 类");
        env->ExceptionClear();
        return false;
    }

    jmethodID loadCls = env->GetMethodID(dclCls, "loadClass",
        "(Ljava/lang/String;)Ljava/lang/Class;");

    jstring vn = env->NewStringUTF("com.example.imgui.VulkanView");
    jclass vCls = (jclass)env->CallObjectMethod(g_DexClassLoader, loadCls, vn);
    env->DeleteLocalRef(vn);
    if (!vCls) {
        LOGE("[JNI] 加载 VulkanView 类失败");
        env->ExceptionClear();
        env->DeleteLocalRef(dclCls);
        return false;
    }

    static JNINativeMethod vm[] = {
        {"init",                    "(Ljava/lang/Object;)V",   (void*)nativeInit},
        {"step",                    "()V",                     (void*)nativeStep},
        {"resize",                  "(II)V",                   (void*)nativeResize},
        {"imgui_Shutdown",          "()V",                     (void*)nativeShutdown},
        {"MotionEventClick",        "(ZFF)V",                  (void*)motionEventClick},
        {"isImGuiComponentTouched", "(FF)Z",                   (void*)isImGuiComponentTouched},
        {"nativeOnTouchEvent",      "(II[I[F[F[FI)V",         (void*)nativeOnTouchEvent},
        {"nativeOnKeyEvent",        "(IIIIII)V",               (void*)nativeOnKeyEvent},
        {"nativeOnCharInput",       "(I)V",                    (void*)nativeOnCharInput},
        {"nativeOnMouseEvent",      "(IFFIFFII)V",             (void*)nativeOnMouseEvent},
    };
    if (env->RegisterNatives(vCls, vm, sizeof(vm) / sizeof(vm[0])) != JNI_OK) {
        LOGE("[JNI] 注册 VulkanView 本地方法失败");
        env->ExceptionClear();
        env->DeleteLocalRef(vCls);
        env->DeleteLocalRef(dclCls);
        return false;
    }
    env->DeleteLocalRef(vCls);

    jstring in = env->NewStringUTF("com.example.imgui.ImGui");
    jclass iCls = (jclass)env->CallObjectMethod(g_DexClassLoader, loadCls, in);
    env->DeleteLocalRef(in);
    if (iCls) {
        static JNINativeMethod im[] = {
            {"nativeGetImGuiWindowBounds",    "()[F", (void*)nativeGetImGuiWindowBounds},
            {"nativeGetCircularButtonBounds", "()[F", (void*)nativeGetCircularButtonBounds},
        };
        if (env->RegisterNatives(iCls, im, sizeof(im) / sizeof(im[0])) != JNI_OK) {
            LOGW("[JNI] 注册 ImGui 本地方法失败");
            env->ExceptionClear();
        }
        env->DeleteLocalRef(iCls);
    } else {
        LOGW("[JNI] 加载 ImGui 类失败");
        env->ExceptionClear();
    }

    env->DeleteLocalRef(dclCls);
    LOGI("[JNI] 本地方法注册完成 (Vulkan)");
    return true;
}

void setSurfaceSecurity(bool skipScreenshot, bool secure) {
    JNIEnv* env = getJNIEnv();
    if (!env || !g_DexClassLoader) return;

    jclass dclCls = env->FindClass("dalvik/system/DexClassLoader");
    if (!dclCls) { env->ExceptionClear(); return; }

    jmethodID loadCls = env->GetMethodID(dclCls, "loadClass",
        "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring vn = env->NewStringUTF("com.example.imgui.VulkanView");
    jclass vCls = (jclass)env->CallObjectMethod(g_DexClassLoader, loadCls, vn);
    env->DeleteLocalRef(vn);
    env->DeleteLocalRef(dclCls);
    if (!vCls) { env->ExceptionClear(); return; }

    jmethodID mid = env->GetStaticMethodID(vCls, "setSurfaceSecurity", "(ZZ)V");
    if (mid) {
        env->CallStaticVoidMethod(vCls, mid, (jboolean)skipScreenshot, (jboolean)secure);
    }
    env->DeleteLocalRef(vCls);
}