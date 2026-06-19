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
extern float g_IslandBounds[4];   // 灵动岛边界（屏幕坐标，含容错）
extern bool  g_IslandVisible;     // 灵动岛是否可见（始终为 true，用于触摸命中）
extern void setSurfaceSecurity(bool skipScreenshot, bool secure);

bool registerNativeMethods();