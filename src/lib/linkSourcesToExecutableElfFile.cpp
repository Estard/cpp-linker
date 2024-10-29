#include "cppld.hpp"
#include "mapInputSectionsToOutputSections.hpp"
#include "parseInputAndCreateSymbolTable.hpp"
#include "statusreport.hpp"
#include "writeLinkingResultsToFile.hpp"

namespace cppld {

auto linkSourcesToExecutableElfFile(parametersFor::LinkSourcesToExecutableElfFile p) -> StatusCode {
    auto& [sourceAddresses, sourceMemorySizes, options] = p.in;

    if (sourceAddresses.size() >= std::numeric_limits<uint32_t>::max())
        return report(StatusCode::not_ok, "too much input: ", sourceAddresses.size(), " files");
    if (sourceAddresses.empty())
        return report(StatusCode::not_ok, "not enough input to link something");
    if (sourceAddresses.size() != sourceMemorySizes.size())
        return report(StatusCode::not_ok, "library usage error");
    if (options.createEhFrameHeader)
        return report(StatusCode::not_ok, "creating eh_frame Headers is not supported");

    StatusCode status{StatusCode::ok};

    std::vector<std::byte*> elfAddresses;
    std::vector<SortKey> sortKeys;
    std::vector<readonly_span<Elf64_Shdr>> sectionHeaders;
    std::vector<const char*> sectionStringTables;
    std::pmr::monotonic_buffer_resource archiveExtractionMemory;

    std::pmr::monotonic_buffer_resource symbolTableMemory;
    SymbolTable symbolTable{&symbolTableMemory};

    status = parseInputAndCreateSymbolTable({.in{sourceAddresses, sourceMemorySizes},
                                             .out{elfAddresses, sortKeys, sectionHeaders, sectionStringTables,
                                                  archiveExtractionMemory, symbolTable}});

    if (status != StatusCode::ok) return status;

    auto& entrySymbolInfo = symbolTable[options.entrySymbolName];
    if (!entrySymbolInfo.firstLoad.symbol) {
        return report(StatusCode::not_ok, "entry symbol \"", options.entrySymbolName, "\" not found in global symbol table");
    }

    std::pmr::monotonic_buffer_resource sectionMaterializationMemory{};
    std::vector<Elf64_Shdr> outputSectionHeaders;
    Elf64_Ehdr elfHeader;

    Vector2D<SectionRef> outputToInputSections;
    Vector2D<OutSectionID> inputToOutputSection;
    std::vector<Elf64_Word> outputSectionTypes;
    std::vector<size_t> outputSectionSizes;
    Vector2D<SectionMemCopies> inputSectionCopyCommands;
    std::vector<std::byte*> materializedViews;
    std::vector<Elf64_Phdr> programHeaders;
    std::vector<size_t> outputSectionAddresses;
    std::vector<size_t> outputSectionFileOffsets;
    size_t gotAddress{};
    Vector2D<ProcessedRela> processedRelas;
    status = mapInputSectionsToOutputSections({.in{elfAddresses,
                                                   sortKeys,
                                                   sectionHeaders,
                                                   sectionStringTables,
                                                   symbolTable,
                                                   entrySymbolInfo},
                                               .out{sectionMaterializationMemory,
                                                    outputSectionHeaders,
                                                    elfHeader,
                                                    outputToInputSections,
                                                    inputToOutputSection,
                                                    outputSectionTypes,
                                                    outputSectionSizes,
                                                    inputSectionCopyCommands,
                                                    materializedViews,
                                                    programHeaders,
                                                    outputSectionAddresses,
                                                    outputSectionFileOffsets,
                                                    gotAddress,
                                                    processedRelas}});
    if (status != StatusCode::ok) return status;

    status = writeLinkingResultsToFile({.in{elfAddresses,
                                            sectionHeaders,
                                            options.outputFileName,
                                            elfHeader,
                                            programHeaders,
                                            outputSectionHeaders,
                                            outputToInputSections,
                                            materializedViews,
                                            outputSectionAddresses,
                                            outputSectionFileOffsets,
                                            outputSectionSizes,
                                            outputSectionTypes,
                                            inputSectionCopyCommands,
                                            gotAddress,
                                            processedRelas}});
    return status;
}

} // namespace cppld
