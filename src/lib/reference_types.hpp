#pragma once

#include <cstddef>
#include <memory_resource>
#include <string_view>
#include <unordered_map>
// Can't forward declare Elf64_Sym because it's a typedef
#include "elf.h"

namespace cppld {

using SortKey = uint64_t;

struct SectionRef {
    size_t elfIndex;
    size_t headerIndex;
};

struct SymbolRef {
    Elf64_Sym const* symbol{nullptr}; // Contains the isWeak information
    size_t elfID;
};

struct GlobalSymbolTableEntry {
    SymbolRef firstSearch;
    SymbolRef firstLoad;
};

using SymbolTable = std::pmr::unordered_map<std::string_view, GlobalSymbolTableEntry>;

} // namespace cppld