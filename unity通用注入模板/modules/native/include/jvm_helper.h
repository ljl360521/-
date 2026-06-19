#pragma once
#include <jni.h>

extern JavaVM *g_JavaVM;
extern jobject g_ActivityInstance;
extern jobject g_DexClassLoader;

JavaVM* obtainJavaVM();
bool    validateJavaVM(JavaVM* vm);
JNIEnv* getJNIEnv();
jobject getCurrentActivityInstance();