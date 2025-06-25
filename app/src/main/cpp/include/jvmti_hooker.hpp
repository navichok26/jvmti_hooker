#pragma once

#include <jni.h>
#include "jvmti.h"
#include <string>

// Структуры
struct JavaVMExt {
    void* functions;
    void* runtime;
};

// Глобальные переменные
extern JavaVM* g_vm;
extern JNIEnv* g_env;
extern int api_level;

// Основные функции пейлоада
JavaVM* getJavaVM();
void initPayload();
void onLibraryLoad();
void onLibraryUnload();

// Утилиты
std::string getCurrentLibraryPath();
int getAndroidApiLevel();
bool fileExists(const char *path);
void* getSym(const char* sym);

// JVMTI функции
void attachJvmtiAgent(JavaVM* vm, JNIEnv* env, const std::string& agentPath);
void MethoddEntry(jvmtiEnv *jvmti_env, JNIEnv *jni_env, jthread thread, jmethodID method);

// APK утилиты
std::string extractLibraryFromApk(JNIEnv* env, const std::string& apkPath, const std::string& libName);
std::string getApkPath(JNIEnv* env);

// JNI экспорты
extern "C" JNIEXPORT jstring JNICALL
Java_ru_x5113nc3x_jvmti_1test_MainActivity_stringFromJNI(JNIEnv* env, jobject thiz);

extern "C" JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM *vm, char *options, void *reserved);
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved);
