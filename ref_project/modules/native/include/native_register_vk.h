#pragma once
#include <jni.h>

extern int   screenWidth, screenHeight;
extern float 屏宽, 屏高;
extern int   屏幕宽, 屏幕高;
extern bool  g_Initialized;
extern float g_MainWindowBounds[4];
extern float g_DragBarBounds[4];
extern bool  g_DragBarVisible;
extern float g_PopupBounds[4];
extern bool  g_PopupVisible;
extern void setSurfaceSecurity(bool skipScreenshot, bool secure);

bool registerNativeMethods();