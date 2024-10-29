#pragma once
#include "cppld_internal_types.hpp"

namespace cppld {
namespace parametersFor {
struct WriteLinkingResultsToFile;
} // namespace parametersFor

/**
 * @brief This function manifests the linking results to a file in a platform specific way
 * 
 * 
 */
auto writeLinkingResultsToFile(parametersFor::WriteLinkingResultsToFile) -> StatusCode;
struct parametersFor::WriteLinkingResultsToFile {
    struct {
        readonly_span<std::byte*> elfAddresses;
        readonly_span<readonly_span<Elf64_Shdr>> sectionHeaders;
        std::string_view outputFileName;
        in<Elf64_Ehdr> elfHeader;
        in<std::vector<Elf64_Phdr>> programHeaders;
        readonly_span<Elf64_Shdr> outputSectionHeaders;
        in<Vector2D<SectionRef>> outputToInputSections;
        readonly_span<std::byte*> materializedViews;
        readonly_span<size_t> outputSectionAddresses;
        readonly_span<size_t> outputSectionFileOffsets;
        readonly_span<size_t> outputSectionSizes;
        readonly_span<Elf64_Word> outputSectionTypes;
        in<Vector2D<SectionMemCopies>> inputSectionCopyCommands;
        size_t gotAddress;
        in<Vector2D<ProcessedRela>> processedRelas;
    } in;
};
} // namespace cppld