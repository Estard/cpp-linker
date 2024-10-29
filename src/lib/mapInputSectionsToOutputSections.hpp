#pragma once
#include "cppld_internal_types.hpp"
namespace cppld {

namespace parametersFor {
struct MapInputSectionsToOutputSections;
// Substeps
struct InitOutputSections;
struct MergeAndSortInputSections;
struct SortOutputSections;
struct PreProcessesRelocations;
struct ConstructLoadedSectionLayout;
struct SynthesizeSyntheticSections;
struct BuildElfAndSectionHeaders;

} // namespace parametersFor

/**
 * @brief This function calculates everything you would want to know for creating an elf file from relocatable elf files
 * 
 * This function is 
 */
auto mapInputSectionsToOutputSections(parametersFor::MapInputSectionsToOutputSections) -> StatusCode;
struct parametersFor::MapInputSectionsToOutputSections {
    struct {
        readonly_span<std::byte*> elfAddresses{};
        readonly_span<SortKey> sortKeys;
        readonly_span<readonly_span<Elf64_Shdr>> sectionHeaders;
        readonly_span<const char*> sectionStringTables;
        in<SymbolTable> symbolTable;
        in<GlobalSymbolTableEntry> entrySymbolInfo;
    } in;
    struct {
        out<std::pmr::memory_resource> materializedSectionMemory;
        out<std::vector<Elf64_Shdr>> outputSectionHeaders;
        out<Elf64_Ehdr> elfHeader;
        out<Vector2D<SectionRef>> outputToInputSections;
        out<Vector2D<OutSectionID>> inputToOutputSection;
        out<std::vector<Elf64_Word>> outputSectionTypes;
        out<std::vector<size_t>> outputSectionSizes;
        out<Vector2D<SectionMemCopies>> inputSectionCopyCommands;
        out<std::vector<std::byte*>> materializedViews;
        out<std::vector<Elf64_Phdr>> programHeaders;
        out<std::vector<size_t>> outputSectionAddresses;
        out<std::vector<size_t>> outputSectionFileOffsets;
        out<size_t> gotAddress;
        out<Vector2D<ProcessedRela>> processedRelas;
    } out;
};

/**
 * @brief Some internal constants that are used for generating program sections
 * 
 */
namespace meta {
// Reserve a value to indicate that a non-existant mapping from input to output
static constexpr OutSectionID notAnOutputSection = std::numeric_limits<OutSectionID>::max();

// 6 Program segments cover all types of input sections
static constexpr size_t numProgramSegments{6};
struct SegmentLocation {
    static constexpr size_t readOnly = 0;
    static constexpr size_t readWrite = 1;
    static constexpr size_t readExecute = 2;
    static constexpr size_t readWriteExecute = 3;
    static constexpr size_t tlsTemplate = 4;
    static constexpr size_t notLoaded = 5;
};
// The Flags associated with the sections
static constexpr std::array<Elf64_Word, numProgramSegments> segmentFlags{
    PF_R, // readOnly
    PF_R | PF_W, // readWrite
    PF_R | PF_X, // readExecute
    PF_R | PF_W | PF_X, // readWriteExecute
    PF_R, // tlsTemplate
    0 // notLoaded
};

// The TLS segment needs special handling
static constexpr std::array<Elf64_Word, numProgramSegments> segmentTypes{
    PT_LOAD, // readOnly
    PT_LOAD, // readWrite
    PT_LOAD, // readExecute
    PT_LOAD, // readWriteExecute
    PT_TLS, // tlsTemplate
    PT_NULL // notLoaded
};

// The tables first entry (number zero) is reserved [...]
// On the AMD64 architecture, entries one and two in the global offset table also are reserved.
constexpr size_t numReservedGotEntries{3};

// Matches gnu ld
constexpr size_t virtualAddressStart{0x400000};
// Default page size on Linux
constexpr size_t pageSize{0x1000};

} // namespace meta

/**
 * @brief Sort everything and determine, names, types and alignments (e.g. Chaotic Evil, Lawful Good, etc)
 * Also saves the input to output mapping and vice versa
 */
auto initOutputSections(parametersFor::InitOutputSections) -> StatusCode;
struct parametersFor::InitOutputSections {
    struct {
        readonly_span<readonly_span<Elf64_Shdr>> sectionHeaders;
        readonly_span<char const*> sectionStringTables;
    } in;
    struct {
        out<std::vector<std::string_view>> names;
        out<Vector2D<SectionRef>> outputToInputSections;
        out<std::vector<Elf64_Xword>> alignments;
        out<std::vector<Elf64_Word>> types;
        out<std::vector<Elf64_Xword>> flags;
        out<Vector2D<OutSectionID>> inputToOutputSection;
        out<size_t> totalNumberOfLocalSymbols;
        out<size_t> totalStringTableMemorySize;
    } out;
};

/**
 * @brief Determines how to input section will appear inside an output section
 * This modifies the order of the output to input section mapping
 * Deduplicates elements if SHF_MERGE is set
 * After merging the final size is known (since it includes padding)
 */
auto mergeAndSortInputSections(parametersFor::MergeAndSortInputSections) -> StatusCode;
struct parametersFor::MergeAndSortInputSections {
    struct {
        readonly_span<std::byte*> elfAddresses;
        readonly_span<SortKey> sortKeys;
        readonly_span<readonly_span<Elf64_Shdr>> sectionHeaders;
        readonly_span<Elf64_Xword> outSectionFlags;
    } in;
    struct {
        inout<Vector2D<SectionRef>> outputToInputSections;
    } inout;
    struct {
        out<std::vector<size_t>> outputSectionSizes;
        out<Vector2D<SectionMemCopies>> inputSectionCopyCommands;
        out<std::vector<std::byte*>> materializedViews;
        out<std::pmr::memory_resource> materializedSectionMemory;
    } out;
};

/**
 * @brief Sorts the output sections into segments by types and flags
 * They are already sorted by name
 * 
 */
auto sortOutputSections(parametersFor::SortOutputSections) -> StatusCode;
struct parametersFor::SortOutputSections {
    struct {
        readonly_span<Elf64_Word> types;
        readonly_span<Elf64_Xword> flags;
    } in;

    struct {
        out<std::array<std::vector<OutSectionID>, meta::numProgramSegments>> segmentedSections;
    } out;
};

/**
 * @brief Do a pass over the relocations to determine where they should go relative to the output section
 * Also determines the entries needed for the Global Offset Table and outputs information to fill it later
 * 
 */
auto preProcessesRelocations(parametersFor::PreProcessesRelocations) -> StatusCode;
struct parametersFor::PreProcessesRelocations {
    struct {
        readonly_span<std::byte*> elfAddresses;
        readonly_span<readonly_span<Elf64_Shdr>> sectionHeaders;
        in<SymbolTable> symbolTable;
        in<Vector2D<SectionMemCopies>> inputSectionCopyCommands;
        in<Vector2D<OutSectionID>> inputToOutputSection;
        size_t numberOfOutputSections;
        OutSectionID gotSectionIndex;
    } in;
    struct {
        out<Vector2D<ProcessedRela>> processedRelas;
        out<std::vector<GOTEntryPatchupInfo>> gotEntryPatches;
    } out;
};

/**
 * @brief Assigns addresses to sections and generates the program headers accordingly
 * 
 * Note that unloaded sections are not covered here since they require the addresses of the loaded sections to be generated in the first place
 */
auto constructLoadedSectionLayout(parametersFor::ConstructLoadedSectionLayout) -> StatusCode;
struct parametersFor::ConstructLoadedSectionLayout {
    struct {
        in<std::array<std::vector<OutSectionID>, meta::numProgramSegments>> segmentedSections;
        readonly_span<size_t> const& outputSectionSizes;
        readonly_span<Elf64_Xword> const& outputSectionAlignments;
        readonly_span<Elf64_Word> const& outputSectionTypes;
    } in;
    struct {
        out<std::vector<Elf64_Phdr>> programHeaders;
        out<std::vector<size_t>> outputSectionAddresses;
        out<std::vector<size_t>> outputSectionFileOffsets;
    } out;
};

/**
 * @brief With the exception of the .got, mostly uncessary for program execution,
 * but hey, you asked for it 
 * 
 */
auto synthesizeSyntheticSections(parametersFor::SynthesizeSyntheticSections) -> StatusCode;
struct parametersFor::SynthesizeSyntheticSections {
    struct {
        OutSectionID gotID, symTabID, strTabID, shstrtabID;
        in<std::vector<GOTEntryPatchupInfo>> gotEntryPatches;
        readonly_span<size_t> outputSectionAddresses;
        in<Vector2D<OutSectionID>> inputToOutputSection;
        in<Vector2D<SectionMemCopies>> inputSectionCopyCommands;
        readonly_span<Elf64_Xword> flags;
        readonly_span<std::string_view> names;
        in<SymbolTable> symbolTable;
        readonly_span<std::byte*> elfAddresses;
        readonly_span<readonly_span<Elf64_Shdr>> sectionHeaders;

        std::byte* enoughStringTableMemory;
        std::byte* enoughSymbolTableMemory;
    } in;
    struct {
        inout<std::vector<std::byte*>> materializedViews;
        inout<std::vector<size_t>> outputSectionSizes;
    } inout;
    struct {
        out<Elf64_Word> numLocalSymbols;
        out<std::vector<Elf64_Word>> sh_names;
    } out;
};

/**
 * @brief Take a look at the function name and then take a wild guess on what it does
 * 
 */
auto buildElfAndSectionHeaders(parametersFor::BuildElfAndSectionHeaders) -> StatusCode;
struct parametersFor::BuildElfAndSectionHeaders {
    struct {
        readonly_span<std::string_view> names;
        readonly_span<Elf64_Word> sh_names;
        readonly_span<Elf64_Word> types;
        readonly_span<Elf64_Xword> flags;
        readonly_span<Elf64_Xword> alignments;
        readonly_span<size_t> outputSectionAddresses;
        readonly_span<size_t> outputSectionFileOffsets;
        readonly_span<size_t> outputSectionSizes;
        size_t sectionDataEnd;
        OutSectionID symTabID;
        OutSectionID strTabID;
        OutSectionID shstrTabID;
        Elf64_Word numLocalSymbols;
        in<Vector2D<SectionMemCopies>> inputSectionCopyCommands;
        in<Vector2D<OutSectionID>> inputToOutputSection;
        in<GlobalSymbolTableEntry> entrySymbolInfo;
        Elf64_Half numProgramHeaders;
    } in;

    struct {
        out<std::vector<Elf64_Shdr>> outputSectionHeaders;
        out<Elf64_Ehdr> elfHeader;
    } out;
};

} // namespace cppld