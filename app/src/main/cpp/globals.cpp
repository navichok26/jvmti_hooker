#include <jvmti_hooker.hpp>

// Глобальные переменные
JavaVM* g_vm = nullptr;
JNIEnv* g_env = nullptr;
int api_level = 0;
