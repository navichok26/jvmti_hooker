#include <jni.h>
#include <jvmti.h>
#include <string>
#include <cstdlib>
#include <dlfcn.h>
#include <unistd.h>
#include <android/dlext.h>
#include <sys/system_properties.h>
#include <pthread.h>
#include <android/dlext_namespaces.h>

#include <logger.hpp>
#include <jvmti_hooker.hpp>

#define ANDROID_R_API 30

extern "C" {
    jint JNI_GetCreatedJavaVMs(JavaVM **vmBuf, jsize bufLen, jsize *nVMs);
}

extern "C" JNIEXPORT jstring JNICALL
Java_ru_x5113nc3x_jvmti_1test_MainActivity_stringFromJNI(
        JNIEnv* env,
        jobject /* this */) {
    std::string hello = "Hello from C++";
    ALOGI("call string from jni");
    JavaVM* vm_ptr = getJavaVM();
    if (!vm_ptr) {
        ALOGE("Failed to get JavaVM");
        return env->NewStringUTF("Error: JavaVM not found");
    }

    return env->NewStringUTF(hello.c_str());
}

typedef jint (*JNI_GetCreatedJavaVMs_t)(JavaVM **vmBuf, jsize bufLen, jsize *nVMs);
// Функция для получения JavaVM через поиск символов в загруженных библиотеках
JavaVM* findJavaVMViaSymbols() {
    
    // Пробуем другие возможные символы
    JNI_GetCreatedJavaVMs_t getCreatedVMs = 
            (JNI_GetCreatedJavaVMs_t)getSym("JNI_GetCreatedJavaVMs");
    if (getCreatedVMs) {
        JavaVM* vms[10];
        jsize vm_count = 0;
        jint result = getCreatedVMs(vms, 10, &vm_count);
        if (result == JNI_OK && vm_count > 0) {
            ALOGI("Successfully found %d JavaVMs", vm_count);
            return vms[0];
        } else {
            ALOGE("JNI_GetCreatedJavaVMs returned %d, vm_count=%d", result, vm_count);
        }
    }
    
    return nullptr;
}

// Функция для получения JavaVM из любого загруженного процесса
JavaVM* getJavaVM() {
    // Пытаемся найти JavaVM через dlsym из libart.so
    JavaVM* vm = findJavaVMViaSymbols();
    return vm;
}

// Функция для инициализации пейлоада
void initPayload() {
    ALOGI("=== PAYLOAD INJECTION STARTED ===");
    
    // Получаем JavaVM
    g_vm = getJavaVM();
    if (!g_vm) {
        ALOGE("Failed to get JavaVM, payload initialization failed");
        return;
    }
    
    // Получаем JNIEnv
    jint getEnvResult = g_vm->GetEnv((void**)&g_env, JNI_VERSION_1_6);
    if (getEnvResult == JNI_EDETACHED) {
        // Поток не привязан к VM, привязываем
        if (g_vm->AttachCurrentThread(&g_env, nullptr) != JNI_OK) {
            ALOGE("Failed to attach current thread to VM");
            return;
        }
        ALOGI("Thread attached to JavaVM");
    } else if (getEnvResult != JNI_OK) {
        ALOGE("Failed to get JNIEnv: %d", getEnvResult);
        return;
    }
    
    // Получаем API level
    api_level = getAndroidApiLevel();
    ALOGI("Android API Level: %d", api_level);
    
    // Получаем путь к текущей библиотеке
    std::string currentLibPath = getCurrentLibraryPath();
    if (!currentLibPath.empty()) {
        ALOGI("Current library path: %s", currentLibPath.c_str());
        
        // Подключаем себя как JVMTI агент
        attachJvmtiAgent(g_vm, g_env, currentLibPath);
    } else {
        ALOGE("Failed to get current library path");
    }
    
    // Инициализируем ART хуки
    const char* ArtLibPath = nullptr;
    if (fileExists("/apex/com.android.art/lib64/libart.so")) {
        ArtLibPath = "/apex/com.android.art/lib64/libart.so";
    } else if (fileExists("/system/lib64/libart.so")) {
        ArtLibPath = "/system/lib64/libart.so";
    } else {
        ALOGE("ART library not found");
        return;
    }
    
    ALOGI("Using ART library: %s", ArtLibPath);
    
    // Устанавливаем флаги отладки
    auto SetJdwpAllowed = reinterpret_cast<void (*)(bool)>(
        getSym("_ZN3art3Dbg14SetJdwpAllowedEb"));
    if (SetJdwpAllowed != nullptr) {
        SetJdwpAllowed(true);
        ALOGI("SetJdwpAllowed(true) called");
    } else {
        ALOGW("SetJdwpAllowed function not found");
    }
    
    // Получаем runtime из JavaVM
    struct JavaVMExt {
        void* functions;
        void* runtime;
    };
    
    JavaVMExt* javaVMExt = (JavaVMExt*)g_vm;
    void* runtime = javaVMExt->runtime;
    
    if (runtime != nullptr) {
        auto setJavaDebuggable = reinterpret_cast<void (*)(void*, bool)>(
            getSym("_ZN3art7Runtime17SetJavaDebuggableEb"));
        if (setJavaDebuggable != nullptr) {
            setJavaDebuggable(runtime, true);
            ALOGI("setJavaDebuggable(true) called");
        } else {
            ALOGW("setJavaDebuggable function not found");
        }
    } else {
        ALOGW("Runtime pointer is null");
    }
    
    ALOGI("=== PAYLOAD INJECTION COMPLETED ===");
}

// Constructor - вызывается при загрузке библиотеки
__attribute__((constructor))
void onLibraryLoad() {
    ALOGI("Library loaded as payload!");
    
    // Запускаем инициализацию в отдельном потоке для избежания блокировок
    pthread_t thread;
    pthread_create(&thread, nullptr, [](void*) -> void* {
        // Небольшая задержка для стабилизации процесса
        usleep(100000); // 100ms
        initPayload();
        return nullptr;
    }, nullptr);
    
    pthread_detach(thread);
}

// Destructor - вызывается при выгрузке библиотеки
__attribute__((destructor))
void onLibraryUnload() {
    ALOGI("Library unloaded");
    
    if (g_env && g_vm) {
        // Отвязываем поток если он был привязан
        g_vm->DetachCurrentThread();
    }
}

std::string getCurrentLibraryPath() {
    Dl_info dl_info;
    if (dladdr((void*)getCurrentLibraryPath, &dl_info) != 0) {
        return std::string(dl_info.dli_fname);
    }
    return "";
}

int getAndroidApiLevel() {
    char sdk_version[PROP_VALUE_MAX];
    if (__system_property_get("ro.build.version.sdk", sdk_version) > 0) {
        return atoi(sdk_version);
    }
    return 0;
}

void MethoddEntry(jvmtiEnv *jvmti_env, JNIEnv *jni_env, jthread thread, jmethodID method) {
    char *name = nullptr;
    char *signature = nullptr;
    char *generic = nullptr;
    char *class_signature = nullptr;
    jclass declaring_class = nullptr;
    jvmtiError result;

    jvmtiThreadInfo tinfo;
    jvmti_env->GetThreadInfo(thread, &tinfo);
    if (jvmti_env->GetMethodDeclaringClass(method, &declaring_class) != JVMTI_ERROR_NONE) {
        return;
    }
    if (jvmti_env->GetClassSignature(declaring_class, &class_signature, nullptr) != JVMTI_ERROR_NONE) {
        return;
    }
    if (jvmti_env->GetMethodName(method, &name, &signature, &generic) != JVMTI_ERROR_NONE) {
        goto deallocate;
    }

    if (name && !strcmp(name, "stringFromJava")) {
        ALOGI("MethoddEntry: %s %s in class %s", name, signature, class_signature);
    }

    deallocate:
    if (name) jvmti_env->Deallocate((unsigned char*)name);
    if (signature) jvmti_env->Deallocate((unsigned char*)signature);
    if (generic) jvmti_env->Deallocate((unsigned char*)generic);
    if (class_signature) jvmti_env->Deallocate((unsigned char*)class_signature);
}

extern "C" JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM *vm, char *options, void *reserved) {
    ALOGI("Agent_OnAttach");
    jvmtiEnv* jvmti = nullptr;
    jint result = vm->GetEnv((void**) &jvmti, JVMTI_VERSION_1_2);
    if (result != JNI_OK || jvmti == nullptr) {
        ALOGE("Unable to access JVMTI!");
        return JNI_ERR;
    }

    jvmtiCapabilities capabilities = {0};
    capabilities.can_generate_method_entry_events = 1;
    if (jvmti->AddCapabilities(&capabilities) != JVMTI_ERROR_NONE) {
        ALOGE("Failed to add capabilities!");
        return JNI_ERR;
    }

    jvmtiEventCallbacks callbacks = {0};
    callbacks.MethodEntry = &MethoddEntry;
    if (jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks)) != JVMTI_ERROR_NONE) {
        ALOGE("Failed to set event callbacks!");
        return JNI_ERR;
    }

    if (jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_METHOD_ENTRY, nullptr) != JVMTI_ERROR_NONE) {
        ALOGE("Failed to set event notification mode!");
        return JNI_ERR;
    }

    return JNI_OK;
}

bool fileExists(const char *path) {
    return (access(path, F_OK) == 0);
}

// Получение символа из указанной библиотеки
void* getSym(const char* sym) {
    const android_dlextinfo dlextinfo = {
        .flags = ANDROID_DLEXT_USE_NAMESPACE,
        .library_namespace = android_get_exported_namespace("art")
    };

    void* handle = android_dlopen_ext("/apex/com.android.art/lib64/libart.so", RTLD_NOW, &dlextinfo);
    if (!handle) {
        ALOGE("dlopen error: %s", dlerror());
        return nullptr;
    }
    void* symbol = dlsym(handle, sym);
    if (!symbol) {
        ALOGE("dlsym error: %s", dlerror());
    }
    // Не закрываем handle, чтобы символ остался валидным
    return symbol;
}

// Функция для извлечения библиотеки из APK
std::string extractLibraryFromApk(JNIEnv* env, const std::string& apkPath, const std::string& libName) {
    // Получаем директорию кэша приложения
    jclass contextClass = env->FindClass("android/app/ActivityThread");
    if (!contextClass) return "";
    
    jmethodID currentActivityThread = env->GetStaticMethodID(contextClass, 
        "currentActivityThread", "()Landroid/app/ActivityThread;");
    if (!currentActivityThread) return "";
    
    jobject activityThread = env->CallStaticObjectMethod(contextClass, currentActivityThread);
    if (!activityThread) return "";
    
    jmethodID getApplication = env->GetMethodID(contextClass, 
        "getApplication", "()Landroid/app/Application;");
    if (!getApplication) return "";
    
    jobject application = env->CallObjectMethod(activityThread, getApplication);
    if (!application) return "";
    
    jclass appClass = env->GetObjectClass(application);
    jmethodID getCacheDir = env->GetMethodID(appClass, "getCacheDir", "()Ljava/io/File;");
    if (!getCacheDir) return "";
    
    jobject cacheDir = env->CallObjectMethod(application, getCacheDir);
    if (!cacheDir) return "";
    
    jclass fileClass = env->GetObjectClass(cacheDir);
    jmethodID getAbsolutePath = env->GetMethodID(fileClass, "getAbsolutePath", "()Ljava/lang/String;");
    if (!getAbsolutePath) return "";
    
    jstring cacheDirPath = (jstring)env->CallObjectMethod(cacheDir, getAbsolutePath);
    if (!cacheDirPath) return "";
    
    const char* cacheDirStr = env->GetStringUTFChars(cacheDirPath, nullptr);
    std::string extractedLibPath = std::string(cacheDirStr) + "/" + libName;
    env->ReleaseStringUTFChars(cacheDirPath, cacheDirStr);
    
    // Проверяем, существует ли уже извлечённая библиотека
    if (access(extractedLibPath.c_str(), F_OK) == 0) {
        ALOGI("Library already extracted: %s", extractedLibPath.c_str());
        return extractedLibPath;
    }
    
    // Извлекаем библиотеку из APK
    jclass zipFileClass = env->FindClass("java/util/zip/ZipFile");
    if (!zipFileClass) return "";
    
    jmethodID zipFileConstructor = env->GetMethodID(zipFileClass, "<init>", "(Ljava/lang/String;)V");
    if (!zipFileConstructor) return "";
    
    jstring apkPathStr = env->NewStringUTF(apkPath.c_str());
    jobject zipFile = env->NewObject(zipFileClass, zipFileConstructor, apkPathStr);
    if (!zipFile) return "";
    
    jmethodID getEntry = env->GetMethodID(zipFileClass, "getEntry", 
        "(Ljava/lang/String;)Ljava/util/zip/ZipEntry;");
    if (!getEntry) return "";
    
    std::string entryName = "lib/arm64-v8a/" + libName;
    jstring entryNameStr = env->NewStringUTF(entryName.c_str());
    jobject entry = env->CallObjectMethod(zipFile, getEntry, entryNameStr);
    if (!entry) return "";
    
    jmethodID getInputStream = env->GetMethodID(zipFileClass, "getInputStream", 
        "(Ljava/util/zip/ZipEntry;)Ljava/io/InputStream;");
    if (!getInputStream) return "";
    
    jobject inputStream = env->CallObjectMethod(zipFile, getInputStream, entry);
    if (!inputStream) return "";
    
    // Создаём файл для извлечённой библиотеки
    jclass fileOutputStreamClass = env->FindClass("java/io/FileOutputStream");
    if (!fileOutputStreamClass) return "";
    
    jmethodID fileOutputStreamConstructor = env->GetMethodID(fileOutputStreamClass, 
        "<init>", "(Ljava/lang/String;)V");
    if (!fileOutputStreamConstructor) return "";
    
    jstring extractedPathStr = env->NewStringUTF(extractedLibPath.c_str());
    jobject outputStream = env->NewObject(fileOutputStreamClass, 
        fileOutputStreamConstructor, extractedPathStr);
    if (!outputStream) return "";
    
    // Копируем данные
    jclass inputStreamClass = env->GetObjectClass(inputStream);
    jmethodID read = env->GetMethodID(inputStreamClass, "read", "([B)I");
    jclass outputStreamClass = env->GetObjectClass(outputStream);
    jmethodID write = env->GetMethodID(outputStreamClass, "write", "([BII)V");
    jmethodID close = env->GetMethodID(outputStreamClass, "close", "()V");
    jmethodID closeInput = env->GetMethodID(inputStreamClass, "close", "()V");
    
    if (read && write && close && closeInput) {
        jbyteArray buffer = env->NewByteArray(8192);
        jint bytesRead;
        
        while ((bytesRead = env->CallIntMethod(inputStream, read, buffer)) > 0) {
            env->CallVoidMethod(outputStream, write, buffer, 0, bytesRead);
        }
        
        env->CallVoidMethod(outputStream, close);
        env->CallVoidMethod(inputStream, closeInput);
        env->DeleteLocalRef(buffer);
    }
    
    // Очищаем ресурсы
    env->DeleteLocalRef(apkPathStr);
    env->DeleteLocalRef(entryNameStr);
    env->DeleteLocalRef(extractedPathStr);
    env->DeleteLocalRef(zipFile);
    env->DeleteLocalRef(entry);
    env->DeleteLocalRef(inputStream);
    env->DeleteLocalRef(outputStream);
    
    ALOGI("Library extracted to: %s", extractedLibPath.c_str());
    return extractedLibPath;
}

// Функция для получения пути к APK
std::string getApkPath(JNIEnv* env) {
    jclass contextClass = env->FindClass("android/app/ActivityThread");
    if (!contextClass) return "";
    
    jmethodID currentActivityThread = env->GetStaticMethodID(contextClass, 
        "currentActivityThread", "()Landroid/app/ActivityThread;");
    if (!currentActivityThread) return "";
    
    jobject activityThread = env->CallStaticObjectMethod(contextClass, currentActivityThread);
    if (!activityThread) return "";
    
    jmethodID getApplication = env->GetMethodID(contextClass, 
        "getApplication", "()Landroid/app/Application;");
    if (!getApplication) return "";
    
    jobject application = env->CallObjectMethod(activityThread, getApplication);
    if (!application) return "";
    
    jclass appClass = env->GetObjectClass(application);
    jmethodID getPackageCodePath = env->GetMethodID(appClass, 
        "getPackageCodePath", "()Ljava/lang/String;");
    if (!getPackageCodePath) return "";
    
    jstring apkPath = (jstring)env->CallObjectMethod(application, getPackageCodePath);
    if (!apkPath) return "";
    
    const char* apkPathStr = env->GetStringUTFChars(apkPath, nullptr);
    std::string result(apkPathStr);
    env->ReleaseStringUTFChars(apkPath, apkPathStr);
    
    return result;
}

// Функция для подключения агента
void attachJvmtiAgent(JavaVM* vm, JNIEnv* env, const std::string& agentPath) {
    jclass debugClass = nullptr;
    jmethodID attachMethod = nullptr;
    
    // Получаем версию Android API
    int apiLevel = getAndroidApiLevel();
    
    // Если библиотека внутри APK, извлекаем её
    std::string finalAgentPath = agentPath;
    if (agentPath.find(".apk!") != std::string::npos) {
        std::string apkPath = getApkPath(env);
        if (!apkPath.empty()) {
            finalAgentPath = extractLibraryFromApk(env, apkPath, "libjvmti_test.so");
            if (finalAgentPath.empty()) {
                ALOGE("Failed to extract library from APK");
                return;
            }
        } else {
            ALOGE("Failed to get APK path");
            return;
        }
    }
    
    if (apiLevel >= 28) { // Android P и выше
        // Используем Debug.attachJvmtiAgent
        debugClass = env->FindClass("android/os/Debug");
        if (debugClass != nullptr) {
            attachMethod = env->GetStaticMethodID(debugClass, "attachJvmtiAgent", 
                "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V");
            if (attachMethod != nullptr) {
                jstring jAgentPath = env->NewStringUTF(finalAgentPath.c_str());
                jobject classLoader = nullptr;
                
                // Получаем ClassLoader
                jclass contextClass = env->FindClass("android/app/ActivityThread");
                if (contextClass) {
                    jmethodID currentActivityThread = env->GetStaticMethodID(contextClass, 
                        "currentActivityThread", "()Landroid/app/ActivityThread;");
                    if (currentActivityThread) {
                        jobject activityThread = env->CallStaticObjectMethod(contextClass, currentActivityThread);
                        if (activityThread) {
                            jmethodID getApplication = env->GetMethodID(contextClass, 
                                "getApplication", "()Landroid/app/Application;");
                            if (getApplication) {
                                jobject application = env->CallObjectMethod(activityThread, getApplication);
                                if (application) {
                                    jclass appClass = env->GetObjectClass(application);
                                    jmethodID getClassLoader = env->GetMethodID(appClass, 
                                        "getClassLoader", "()Ljava/lang/ClassLoader;");
                                    if (getClassLoader) {
                                        classLoader = env->CallObjectMethod(application, getClassLoader);
                                    }
                                }
                            }
                        }
                    }
                }
                
                env->CallStaticVoidMethod(debugClass, attachMethod, jAgentPath, nullptr, classLoader);
                env->DeleteLocalRef(jAgentPath);
                ALOGI("JVMTI agent attached via Debug.attachJvmtiAgent with path: %s", finalAgentPath.c_str());
            }
        }
    } else {
        // Используем VMDebug.attachAgent для более старых версий
        debugClass = env->FindClass("dalvik/system/VMDebug");
        if (debugClass != nullptr) {
            attachMethod = env->GetStaticMethodID(debugClass, "attachAgent", "(Ljava/lang/String;)V");
            if (attachMethod != nullptr) {
                jstring jAgentPath = env->NewStringUTF(finalAgentPath.c_str());
                env->CallStaticVoidMethod(debugClass, attachMethod, jAgentPath);
                env->DeleteLocalRef(jAgentPath);
                ALOGI("JVMTI agent attached via VMDebug.attachAgent with path: %s", finalAgentPath.c_str());
            }
        }
    }
    
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        ALOGE("Failed to attach JVMTI agent");
    }
}

// JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
//     ALOGI("library load");
//     char api_level_str[5];
//     __system_property_get("ro.build.version.sdk", api_level_str);
//     api_level = atoi(api_level_str);
//     JNIEnv *env = NULL;

//     if (vm->GetEnv((void **) &env, JNI_VERSION_1_6) != JNI_OK) {
//         return -1;
//     }

//     // Получаем путь к текущей библиотеке
//     std::string currentLibPath = getCurrentLibraryPath();
//     if (!currentLibPath.empty()) {
//         ALOGI("Current library path: %s", currentLibPath.c_str());
        
//         // Подключаем себя как JVMTI агент
//         attachJvmtiAgent(vm, env, currentLibPath);
//     } else {
//         ALOGE("Failed to get current library path");
//     }

//     JavaVMExt* javaVMExt = (JavaVMExt*)vm;

//     void* runtime = javaVMExt->runtime;
//     if (runtime == nullptr) {
//         return JNI_ERR;
//     }

//     const char* ArtLibPath = nullptr;
//     if (fileExists("/apex/com.android.art/lib64/libart.so")) {
//         ArtLibPath = "/apex/com.android.art/lib64/libart.so";
//     } else {
//         ArtLibPath = "/system/lib64/libart.so";
//     }
//     ALOGI("artlib: %s", ArtLibPath);

//     // Получение адреса функции SetJdwpAllowed и вызов её
//     auto SetJdwpAllowed = reinterpret_cast<void (*)(bool)>(SandGetSym(ArtLibPath,
//                                                                       "_ZN3art3Dbg14SetJdwpAllowedEb"));
//     if (SetJdwpAllowed != nullptr) {
//         SetJdwpAllowed(true);
//     }

//     // Получение адреса функции setJavaDebuggable и установка флага
//     auto setJavaDebuggable = reinterpret_cast<void (*)(void*, bool)>(SandGetSym(ArtLibPath,
//                                                                                 "_ZN3art7Runtime17SetJavaDebuggableEb"));
//     if (setJavaDebuggable != nullptr) {
//         setJavaDebuggable(runtime, true);
//         ALOGE("zxw %s", "setJavaDebuggable true");
//     }

//     return JNI_VERSION_1_6;
// }