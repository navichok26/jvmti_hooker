#include <linker.hpp>

#include <elf.h>
#include <jni.h>
#include <string>
#include <dlfcn.h>
#include <link.h>

module_t get_module(const std::string& name) {
    module_t module;
    module.name = name;
    dl_iterate_phdr([] (dl_phdr_info* info, size_t size, void* data) {
        module_t* m = reinterpret_cast<module_t*>(data);
        if (info->dlpi_name == nullptr) {
            return 0;
        }
        if (std::string(info->dlpi_name).find(m->name) != std::string::npos) {
            m->base = info->dlpi_addr;
            m->bin  = Parser::parse(info->dlpi_name);
            return 1;
        }
        return 0;
    }, reinterpret_cast<void*>(&module));
    return std::move(module);
}