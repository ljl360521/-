#include "egl_hook.h"
#include "gl_bindless_renderer.h"
#include "logger.h"
#include "gui.h"

#include <EGL/egl.h>
#include <android/native_window_jni.h>
#include <dobby.h>
#include <atomic>

static EGLBoolean (*orig_eglSwapBuffers)(EGLDisplay, EGLSurface) = nullptr;

static std::atomic<bool> s_hookReady(false);

static EGLBoolean _eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    
    
    GLBindlessRenderer::instance().init(屏幕宽, 屏幕高);
    GLBindlessRenderer::instance().flush();
    GLBindlessRenderer::instance().clear();
    
    return orig_eglSwapBuffers(dpy, surface);
}

void installEGLHooks() {
    void* eglSwap = (void*)eglSwapBuffers;
    
    if (DobbyHook(eglSwap, (void*)_eglSwapBuffers,
                  (void**)&orig_eglSwapBuffers) == 0) {
        LOGI("[EGLHook] eglSwapBuffers hooked");
    } else {
        LOGE("[EGLHook] eglSwapBuffers hook 失败");
    }


    s_hookReady.store(true);
}