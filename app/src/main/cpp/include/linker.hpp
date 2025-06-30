#include <android/log.h>
#include <dlfcn.h>
#include <link.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string>
#include <cstring>
#include <errno.h>

#include <logger.hpp>

// Android-specific linker name
#ifdef __LP64__
#define LINKER_NAME "linker64"
#else
#define LINKER_NAME "linker"
#endif

// ELF structures for both 32 and 64-bit
#if defined(__LP64__)
#define Elf_(type) Elf64_##type
#else
#define Elf_(type) Elf32_##type
#endif

// Структура для хранения информации о линкере
struct linker_info {
    uintptr_t base;
    const char* path;
    bool found;
};

typedef void* (*dlopen_func)(const char*, int, const void*);

extern "C" void* unrestricted_dlopen(const char* libname, int flags);
