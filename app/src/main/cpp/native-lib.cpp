#include <jni.h>
#include <jvmti.h>
#include <string>
#include <cstdlib>
#include <dlfcn.h>
#include <unistd.h>
#include <android/dlext.h>
#include <sys/system_properties.h>
#include <pthread.h>

#include <logger.hpp>
#include <jvmti_hooker.hpp>
#include <linker.hpp>
#include <xdl.h>

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
JavaVM* findJavaVMViaSymbols() {
    
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

JavaVM* getJavaVM() {
    JavaVM* vm = findJavaVMViaSymbols();
    return vm;
}

void initPayload() {
    ALOGI("=== PAYLOAD INJECTION STARTED ===");
    
    g_vm = getJavaVM();
    if (!g_vm) {
        ALOGE("Failed to get JavaVM, payload initialization failed");
        return;
    }
    
    jint getEnvResult = g_vm->GetEnv((void**)&g_env, JNI_VERSION_1_6);
    if (getEnvResult == JNI_EDETACHED) {
        if (g_vm->AttachCurrentThread(&g_env, nullptr) != JNI_OK) {
            ALOGE("Failed to attach current thread to VM");
            return;
        }
        ALOGI("Thread attached to JavaVM");
    } else if (getEnvResult != JNI_OK) {
        ALOGE("Failed to get JNIEnv: %d", getEnvResult);
        return;
    }


    auto SetJdwpAllowed = reinterpret_cast<void (*)(bool)>(
            getSym("_ZN3art3Dbg14SetJdwpAllowedEb"));
    if (SetJdwpAllowed != nullptr) {
        SetJdwpAllowed(true);
        ALOGI("SetJdwpAllowed(true) called");
    } else {
        ALOGW("SetJdwpAllowed function not found");
    }

    struct JavaVMExt {
        void* functions;
        void* runtime;
    };

    JavaVMExt* javaVMExt = (JavaVMExt*)g_vm;
    void* runtime = javaVMExt->runtime;

    if (runtime != nullptr) {
        auto setJavaDebuggable = reinterpret_cast<void (*)(void*, unsigned char)>(
                getSym("_ZN3art7Runtime20SetRuntimeDebugStateENS0_17RuntimeDebugStateE"));
        if (setJavaDebuggable != nullptr) {
            setJavaDebuggable(runtime, 2);
            ALOGI("_ZN3art7Runtime20SetRuntimeDebugStateENS0_17RuntimeDebugStateE(true) called");
        } else {
            ALOGW("setJavaDebuggable function not found");
        }
    } else {
        ALOGW("Runtime pointer is null");
    }
    
    api_level = getAndroidApiLevel();
    ALOGI("Android API Level: %d", api_level);
    
    std::string currentLibPath = getCurrentLibraryPath();
    if (!currentLibPath.empty()) {
        ALOGI("Current library path: %s", currentLibPath.c_str());
        
        attachJvmtiAgent(g_vm, g_env, "/data/data/ru.x5113nc3x.jvmti_test/cache/libjvmti_test.so");
    } else {
        ALOGE("Failed to get current library path");
    }
    
    ALOGI("=== PAYLOAD INJECTION COMPLETED ===");
}

// Constructor - вызывается при загрузке библиотеки
__attribute__((constructor))
void onLibraryLoad() {
    ALOGI("Library loaded as payload!");
    pthread_t thread;
    pthread_create(&thread, nullptr, [](void*) -> void* {
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

void* getSym(const char* name) {
    static void* handle = nullptr;
    if (!handle) {
        handle = xdl_open("libart.so", RTLD_NOW);
        if (!handle) {
            handle = xdl_open("/apex/com.android.art/lib64/libart.so", RTLD_NOW);
        }
        if (!handle) {
            ALOGE("dlopen libart.so failed: %s", dlerror());
            return nullptr;
        }
    }

    dlerror();

    size_t symbol_size = 0;
    void* sym = xdl_sym(handle, name, &symbol_size);
    const char* err = dlerror();
    if (err) {
        ALOGE("dlsym(%s) failed: %s", name, err);
        return nullptr;
    }
    return sym;
}

std::string extractLibraryFromApk(JNIEnv* env, const std::string& apkPath, const std::string& libName) {
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
    
    if (access(extractedLibPath.c_str(), F_OK) == 0) {
        ALOGI("Library already extracted: %s", extractedLibPath.c_str());
        return extractedLibPath;
    }
    
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
    
    jclass fileOutputStreamClass = env->FindClass("java/io/FileOutputStream");
    if (!fileOutputStreamClass) return "";
    
    jmethodID fileOutputStreamConstructor = env->GetMethodID(fileOutputStreamClass, 
        "<init>", "(Ljava/lang/String;)V");
    if (!fileOutputStreamConstructor) return "";
    
    jstring extractedPathStr = env->NewStringUTF(extractedLibPath.c_str());
    jobject outputStream = env->NewObject(fileOutputStreamClass, 
        fileOutputStreamConstructor, extractedPathStr);
    if (!outputStream) return "";
    
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

void attachJvmtiAgent(JavaVM* vm, JNIEnv* env, const std::string& agentPath) {
    jclass debugClass = nullptr;
    jmethodID attachMethod = nullptr;
    
    int apiLevel = getAndroidApiLevel();
    
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
        debugClass = env->FindClass("android/os/Debug");
        if (debugClass != nullptr) {
            attachMethod = env->GetStaticMethodID(debugClass, "attachJvmtiAgent", 
                "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V");
            if (attachMethod != nullptr) {
                jstring jAgentPath = env->NewStringUTF(finalAgentPath.c_str());
                jobject classLoader = nullptr;
                
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