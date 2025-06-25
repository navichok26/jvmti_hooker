#pragma once

#include <linker/linker_namespaces.h>
#include <elf.h>

typedef Elf32_Half Elf32_Versym;
typedef Elf64_Half Elf64_Versym;
typedef Elf32_Word Elf32_Relr;
typedef Elf64_Xword Elf64_Relr;

#include <linker/linker_soinfo.h>

using get_soname_t            = const char* (*)(soinfo*);
using get_primary_namespace_t = android_namespace_t* (*)(soinfo*);

struct module_t {
public:
    std::string name;
    std::unique_ptr<Binary> bin;
    uintptr_t base;

public:
    uintptr_t get_address(const std::string& symname) {
        if (not this->bin->has_symbol(symname)) {
            return 0;
        }
        Symbol& sym = reinterpret_cast<Symbol&>(this->bin->get_symbol(symname));
        return this->base + sym.value();
    }

    operator bool() {
        return this->bin != nullptr;
    }
};