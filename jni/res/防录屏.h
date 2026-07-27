#pragma once
#include "imgui.h"
bool SetJavaSecureSurfaceMode(bool enable);
bool SecureLoadDex();
void SecureSetEnabledOnly(bool enable);
void DestroySecureEglSurface();
bool EnsureSecureEglSurface(int width, int height);
bool RenderDrawDataToSecureSurface(ImDrawData* drawData);
bool RebindNativeWindowForSecure(bool enable);
