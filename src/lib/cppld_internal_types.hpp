#pragma once

#include "cppld_api_types.hpp"
#include "reference_types.hpp"

#include <variant>

namespace cppld {

// Since sections are grouped by file in a 2D arrangement,
// it is often the case that a vector of vector needs to be employed to add data parallel to the sections
template <typename T>
using Vector2D = std::vector<std::vector<T>>;

// Section ID, since there are only a couple of possible output sections, a smaller integer type can be used
using OutSectionID = uint16_t;

// For Section merges different parts of a section get moved to different locations.
// This information is saved to find addresses of symbols
struct PartCopy {
    size_t size; // How much is copied.
    size_t dstOffset; // Where it is copied to relative to an output section
};

// Probably the most useful construct for std::variant.
// See https://en.cppreference.com/w/cpp/utility/variant/visit
template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};
// explicit deduction guide (still needed for clang-tidy)
template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;
// Having a variant here avoid an extra indirection for sections that only have a single part copy (which is most of them)
using SectionMemCopies = std::variant<std::monostate, PartCopy, std::vector<PartCopy>>;

// A Relocation in an elf file almost ready to be applied to an output file
// Since final addresses are only know later, the values are still relative
struct ProcessedRela {
    int64_t addend; // A
    size_t outputSectionOffset; // + address[outSectionID] = address to apply the relocation to | P
    size_t symbolValue; // + address[srcID] = address of the symbol in the output | Is either the value (S) or the size of the Symbol (Z)
    uint32_t type; // Switch for calculation
    OutSectionID symbolSectionID; // gives dstAddress base , could be the GOT
    enum class Note : uint16_t { // Would be in the padding bytes
        none = 0,
        undefinedWeak,
        absoluteValue
    } note;
};
static_assert(sizeof(ProcessedRela) == 4 * sizeof(size_t)); // Not much larger than an ELF64_Rela
static_assert(sizeof(Elf64_Rela) == 3 * sizeof(size_t));

// Emitted during the rela preprocessing
// Needed to bring the global offset table into a functional state
struct GOTEntryPatchupInfo {
    size_t elfID;
    size_t headerID;
    size_t symbolValue;
};
} // namespace cppld