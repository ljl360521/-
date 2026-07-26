#pragma once
#include <jni.h>
bool ImeClearException(JNIEnv* env);
JNIEnv* ImeGetEnv(bool* attached);
bool ImeEnsureActivity();
bool ImeLoadDex();
bool ImeShowKeyboard(bool show);
void ImeUpdateByImGui();
bool MainDexLoad();
bool CaptureCurrentClassLoader(JNIEnv* env, jclass cls);
jclass GetClassFromCurrentLoader(JNIEnv* env, const char* className);
bool OpenUrlByActivity(const char* url);
