#pragma once

#include <string.h>
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <iostream>
#include <sys/mman.h>
#include <dlfcn.h>
#include <math.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <sys/inotify.h>
#include <android/log.h>
#include "imgui_internal.h"
#include "imgui.h"
#include "imgui_impl_android.h"
#include "imgui_impl_opengl3.h"
#include "stb_image.h"
#include "font.h"
#include "dobby.h"
#include "stb_image.h"

#include <android/native_window.h>
#include <android/native_window_jni.h>

extern float 屏宽, 屏高;
extern int 屏幕宽, 屏幕高;
extern float g_MainWindowBounds[4];
extern bool g_Initialized;
JNIEnv* getJNIEnv();
extern JavaVM *g_JavaVM;
extern jobject g_ActivityInstance;
extern jobject g_DexClassLoader;
