#include <linker.hpp>



uintptr_t find_linker_base() {
    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps) {
        ALOGE("Failed to open /proc/self/maps");
        return 0;
    }

    char line[512];
    uintptr_t base = 0;

    while (fgets(line, sizeof(line), maps)) {
        if (strstr(line, LINKER_NAME)) {
            char* endptr;
            base = strtoul(line, &endptr, 16);
            break;
        }
    }

    fclose(maps);
    return base;
}

// Callback для dl_iterate_phdr
static int phdr_callback(struct dl_phdr_info* info, size_t size, void* data) {
    linker_info *linfo = static_cast<linker_info *>(data);

    if (strstr(info->dlpi_name, "linker") || strstr(info->dlpi_name, "ld-android")) {
        linfo->base = info->dlpi_addr;
        linfo->path = info->dlpi_name;
        linfo->found = true;
        return 1; // Останавливаем итерацию
    }

    return 0;
}

dlopen_func find_loader_dlopen() {
    // Получаем информацию о линкере
    linker_info linfo = {0};
    dl_iterate_phdr(phdr_callback, &linfo);

    if (!linfo.found) {
        ALOGE("Failed to find linker in memory maps");
        return nullptr;
    }

    ALOGD("Linker found at base: 0x%lx, path: %s", linfo.base, linfo.path);

    // Открываем файл линкера
    void* handle = dlopen(linfo.path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        ALOGE("Failed to dlopen linker: %s", dlerror());
        return nullptr;
    }

    // Получаем адрес __loader_dlopen
    dlopen_func loader_dlopen = reinterpret_cast<dlopen_func>(
            dlsym(handle, "__loader_dlopen"));

    if (!loader_dlopen) {
        ALOGE("Failed to find __loader_dlopen: %s", dlerror());
        dlclose(handle);
        return nullptr;
    }

    ALOGD("Found __loader_dlopen at %p", loader_dlopen);
    dlclose(handle);

    return loader_dlopen;
}

extern "C" void* unrestricted_dlopen(const char* libname, int flags) {
    static dlopen_func loader_dlopen = nullptr;

    if (!loader_dlopen) {
        loader_dlopen = find_loader_dlopen();
        if (!loader_dlopen) {
            return nullptr;
        }
    }

    // Получаем адрес вызывающей библиотеки
    Dl_info info;
    if (dladdr((void*)&unrestricted_dlopen, &info) == 0) {
        ALOGE("Failed to get caller info");
        return nullptr;
    }

    ALOGD("Calling __loader_dlopen for %s with caller %s", libname, info.dli_fname);
    return loader_dlopen(libname, flags, info.dli_fbase);

}
