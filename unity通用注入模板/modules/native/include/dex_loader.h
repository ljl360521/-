#pragma once
#include <jni.h>
#include <string>

jobject     createDexByteBuffer();
jclass      loadImGuiClassFromDex();
bool        callImGuiSetupView(jclass imguiClass);