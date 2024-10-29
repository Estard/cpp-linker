#include "mapInputSectionsToOutputSections.hpp"
#include "convenient_functions.hpp"
#include "statusreport.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>

namespace cppld {

auto mapInputSectionsToOutputSections(parametersFor::MapInputSectionsToOutputSections p) -> StatusCode {
    auto& [elfAddresses, sortKeys, sectionHeaders, sectionStringTables, symbolTable, entrySymbolInfo] = p.in;
    auto& [sectionMaterializationMemory, outputSectionHeaders, elfHeader, outputToInputSections,
           inputToOutputSection, outputSectionTypes, outputSectionSizes, inputSectionCopyCommands,
           materializedViews, programHeaders, outputSectionAddresses, outputSectionFileOffsets,
           gotAddress, processedRelas] = p.out;

    StatusCode status{StatusCode::ok};
    std::vector<std::string_view> names;
    std::vector<Elf64_Xword> alignments;
    std::vector<Elf64_Xword> flags;

    size_t totalNumberOfLocalSymbols;
    size_t totalStringTableMemorySize;

    status = initOutputSections({.in{sectionHeaders, sectionStringTables},
                                 .out{names, outputToInputSections, alignments,
                                      outputSectionTypes, flags, inputToOutputSection,
                                      totalNumberOfLocalSymbols,
                                      totalStringTableMemorySize}});
    if (status != StatusCode::ok) return status; // NOLINT

    status = mergeAndSortInputSections({.in{elfAddresses,
                                            sortKeys,
                                            sectionHeaders,
                                            flags},
                                        .inout{outputToInputSections},
                                        .out{outputSectionSizes,
                                             inputSectionCopyCommands,
                                             materializedViews,
                                             sectionMaterializationMemory}});
    if (status != StatusCode::ok) return status;

    OutSectionID gotID, symTabID, strTabID, shstrTabID;
    {
        // This is an internal function that gets used exactly 4 times
        // NOLINTNEXTLINE
        auto allocateSyntheticSection = [&](std::string_view name, Elf64_Word type, Elf64_Xword flag, Elf64_Xword alignment) -> OutSectionID {
            auto id = outputToInputSections.size();
            names.push_back(name);
            outputToInputSections.emplace_back(/*no source sections*/);
            outputSectionTypes.push_back(type);
            flags.push_back(flag);
            alignments.push_back(alignment);
            outputSectionSizes.push_back(0);
            materializedViews.push_back(nullptr);
            return static_cast<OutSectionID>(id);
        };

        // global offset table, gets sorted into the data segment
        gotID = allocateSyntheticSection(".got", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, alignof(Elf64_Addr));
        // symTab, strTab and shstrTab get sorted in the unloaded segment
        constexpr Elf64_Xword noFlags{0};
        symTabID = allocateSyntheticSection(".symtab", SHT_SYMTAB, noFlags, alignof(Elf64_Sym));
        strTabID = allocateSyntheticSection(".strTab", SHT_STRTAB, noFlags, alignof(char));
        shstrTabID = allocateSyntheticSection(".shstrtab", SHT_STRTAB, noFlags, alignof(char));
    }
    std::array<std::vector<OutSectionID>, meta::numProgramSegments> segmentedSections;
    status = sortOutputSections({.in{outputSectionTypes, flags}, .out{segmentedSections}});
    if (status != StatusCode::ok) return status;

    std::vector<GOTEntryPatchupInfo> gotEntryPatches;
    status = preProcessesRelocations({.in{elfAddresses, sectionHeaders, symbolTable,
                                          inputSectionCopyCommands, inputToOutputSection, outputToInputSections.size(), gotID},
                                      .out{processedRelas, gotEntryPatches}});
    if (status != StatusCode::ok) return status;

    // Reserve GOT space
    outputSectionSizes[gotID] = (meta::numReservedGotEntries + gotEntryPatches.size()) * sizeof(Elf64_Addr);
    materializedViews[gotID] = static_cast<std::byte*>(sectionMaterializationMemory.allocate(outputSectionSizes[gotID]));

    status = constructLoadedSectionLayout({.in{segmentedSections, outputSectionSizes, alignments, outputSectionTypes},
                                           .out{programHeaders, outputSectionAddresses, outputSectionFileOffsets}});
    if (status != StatusCode::ok) return status;

    // Return address of global offset Table
    gotAddress = outputSectionAddresses[gotID];

    auto syntheticSectionStringMemorySize = size_t{4};
    syntheticSectionStringMemorySize += names[gotID].size() + names[strTabID].size();
    syntheticSectionStringMemorySize += names[shstrTabID].size() + names[symTabID].size();

    auto enoughStringTableMemory = static_cast<std::byte*>(
        sectionMaterializationMemory.allocate(totalStringTableMemorySize + syntheticSectionStringMemorySize));

    auto enoughSymbolTableMemory = static_cast<std::byte*>(sectionMaterializationMemory.allocate(
        (totalNumberOfLocalSymbols + symbolTable.size()) * sizeof(Elf64_Sym), alignof(Elf64_Sym)));
    Elf64_Word numLocalSymbols;
    std::vector<Elf64_Word> sh_names;
    status = synthesizeSyntheticSections({.in{gotID, symTabID, strTabID, shstrTabID,
                                              gotEntryPatches, outputSectionAddresses,
                                              inputToOutputSection, inputSectionCopyCommands,
                                              flags, names, symbolTable, elfAddresses,
                                              sectionHeaders, enoughStringTableMemory, enoughSymbolTableMemory},
                                          .inout{materializedViews, outputSectionSizes},
                                          .out{numLocalSymbols, sh_names}});
    if (status != StatusCode::ok) return status;

    // Give the unloaded sections a place in the file, too
    // This can only happen after they are synthesized which in turn depends on the loaded sections to have a fix layout
    auto& lastLoaded = programHeaders[programHeaders.size() - 2];
    auto offsetOfUnloaded = lastLoaded.p_offset + lastLoaded.p_filesz;

    for (auto& sectionID : segmentedSections[meta::SegmentLocation::notLoaded]) {
        offsetOfUnloaded = alignup(offsetOfUnloaded, alignments[sectionID]);
        outputSectionAddresses[sectionID] = 0;
        outputSectionFileOffsets[sectionID] = offsetOfUnloaded;
        offsetOfUnloaded += outputSectionSizes[sectionID];
    }

    status = buildElfAndSectionHeaders({.in{names, sh_names, outputSectionTypes, flags, alignments, outputSectionAddresses,
                                            outputSectionFileOffsets, outputSectionSizes, offsetOfUnloaded,
                                            symTabID, strTabID, shstrTabID, numLocalSymbols, inputSectionCopyCommands,
                                            inputToOutputSection, entrySymbolInfo,
                                            static_cast<Elf64_Half>(programHeaders.size())},
                                        .out{outputSectionHeaders, elfHeader}});

    return status;
}

// Implementation of the substeps
// They should all more or less get inlined into the primary function

auto initOutputSections(parametersFor::InitOutputSections p) -> StatusCode {
    // Certain Section Types never reach the output
    auto sectionTypeReachesOutput = [](Elf64_Word type) -> bool {
        constexpr std::array<Elf64_Word, 6> nonOutputSectionTypes{
            SHT_NULL,
            SHT_STRTAB,
            SHT_SYMTAB,
            SHT_GROUP,
            SHT_REL,
            SHT_RELA};
        for (auto notThisType : nonOutputSectionTypes)
            if (type == notThisType) return false;
        return true;
    };

    // Certain Section Names get truncated
    auto toOutputSectionName = [](std::string_view fullName) -> std::string_view {
        constexpr std::array<std::string_view, 13> namesToTruncate{
            ".text",
            ".data.rel.ro",
            ".data",
            ".ldata",
            ".rodata",
            ".lrodata",
            ".bss.rel.ro",
            ".bss",
            ".lbss",
            ".init_array",
            ".fini_array",
            ".tbss",
            ".tdata"};
        for (auto truncate : namesToTruncate) {
            if (fullName.starts_with(truncate)) return fullName.substr(0, truncate.size());
        }
        return fullName;
    };

    auto& [sectionHeaders, sectionStringTables] = p.in;
    auto& [names, outputToInputSections, alignments, types, flags, inputToOutputSection,
           totalNumberOfLocalSymbols, totalStringTableMemorySize] = p.out;

    // sections to output sections. 2D array since input sections are also in a 2D array
    inputToOutputSection.resize(sectionHeaders.size());
    totalNumberOfLocalSymbols = 0;
    totalStringTableMemorySize = 0;

    // sections names to inputSections
    std::unordered_map<std::string_view, std::vector<SectionRef>> sectionMap;
    // Map all input sections to their output section
    // The final ids for the output sections are only known after flattening the hash map, so the id is assigned later
    for_each_indexed(sectionHeaders, [&](readonly_span<Elf64_Shdr> headers, size_t inputIndex) {
        // Reserve enough space for later. For debugging set an out of bounds value
        inputToOutputSection[inputIndex].resize(headers.size(), meta::notAnOutputSection);

        for_each_indexed(headers, [&](Elf64_Shdr const& header, size_t headerIndex) {
            if (header.sh_type == SHT_STRTAB)
                totalStringTableMemorySize += header.sh_size;
            if (header.sh_type == SHT_SYMTAB)
                totalNumberOfLocalSymbols += header.sh_info;
            if (!sectionTypeReachesOutput(header.sh_type))
                return;
            std::string_view sectionName{sectionStringTables[inputIndex] + header.sh_name};
            auto outputSectionName = toOutputSectionName(sectionName);
            sectionMap[outputSectionName].push_back({.elfIndex = inputIndex, .headerIndex = headerIndex});
        });
    });

    // 4 synthetic sections will be added later (got, symtab, strtab and shstrtab)
    if (sectionMap.size() >= (SHN_LORESERVE - 4))
        return report(StatusCode::not_ok, "too many output sections: ", sectionMap.size());

    // Flatten map to array. This is necessary to obtain a reasonable index and to make iterations faster
    names.reserve(sectionMap.size());
    outputToInputSections.reserve(sectionMap.size());
    for (auto& [name, inputSections] : sectionMap) {
        names.push_back(name);
        outputToInputSections.push_back(std::move(inputSections));
    }

    // SHF_MERGE sometimes appears in .rodata sections but sometimes it doesn't.
    // If that happens, the flags are not directly compatible, so the no merge happens.
    // Though luck, now your output is maybe slightly larger than before... should have put it in a separate section
    // Of course, partial merging would in principal be possible, but it was not demanded
    // and is thus not in the scope of this project
    auto makeFlagsCompatible = [](Elf64_Xword& sourceFlag, Elf64_Xword other) {
        if (sourceFlag == other) return true;

        constexpr auto clearMergeFlagsMask = ~static_cast<Elf64_Xword>(SHF_MERGE | SHF_STRINGS);
        auto cleanSource = sourceFlag & clearMergeFlagsMask;
        auto cleanOther = other & clearMergeFlagsMask;
        if (cleanOther == cleanSource) {
            sourceFlag = cleanSource;
            return true;
        }
        return false;
    };

    // Resize to make sure
    alignments.resize(names.size());
    types.resize(names.size());
    flags.resize(names.size());

    StatusCode status{StatusCode::ok};
    // set alignment, types and flags for output section
    // This is necessary to sort them to the correct place later
    // Each output section is independent from others, so they could be done in parallel (though there isn't much to be gained here since the number of output section is fairly small and the input unevenly distributed)
    for_each_indexed(outputToInputSections, [&](std::vector<SectionRef>& inputSections, size_t outSecID) {
        for_each_indexed(inputSections, [&](SectionRef secRef, size_t sectionIndex) {
            // The linter once again sees an uninitialized pointer, which seems to be confusing some things
            // NOLINTNEXTLINE
            auto& inputSection = sectionHeaders[secRef.elfIndex][secRef.headerIndex];

            // Create a link back. Since one input can only ever belong to one output, there is no race condition here
            inputToOutputSection[secRef.elfIndex][secRef.headerIndex] = static_cast<OutSectionID>(outSecID);
            // The first section determines the attributes
            if (sectionIndex == 0) {
                flags[outSecID] = inputSection.sh_flags;
                types[outSecID] = inputSection.sh_type;
                alignments[outSecID] = inputSection.sh_addralign;
            }
            // Error on incompatible type + flags
            if ((!makeFlagsCompatible(flags[outSecID], inputSection.sh_flags)) || (types[outSecID] != inputSection.sh_type)) {
                status = report(StatusCode::not_ok, "Sections with the same name have different flags/flags than expected: ",
                                flags[outSecID], " vs ", inputSection.sh_flags, " and ", types[outSecID], " vs. ", inputSection.sh_type,
                                ". Offending section: ", names[outSecID]);
            }
            // Alignment is the maximum of all input sections
            alignments[outSecID] = std::max(alignments[outSecID], inputSection.sh_addralign);
        });
    });
    return status;
}

namespace /*internal*/ {

struct ConcatenateSections {
    struct {
        readonly_span<readonly_span<Elf64_Shdr>> sectionHeaders;
        readonly_span<SectionRef> sectionRefs;
        OutSectionID outSectionID;
    } in;
    struct {
        out<std::vector<size_t>> outputSectionSizes;
        out<Vector2D<SectionMemCopies>> inputSectionCopyCommands;
    } out;
};

auto concatenateSections(ConcatenateSections p) -> StatusCode {
    auto& [sectionHeaders, sectionRefs, outSectionID] = p.in;
    auto& [outputSectionSizes, inputSectionCopyCommands] = p.out;
    size_t outSectionSize{0};
    for (auto& secRef : sectionRefs) {
        auto elfID = secRef.elfIndex;
        auto& inSecHdr = sectionHeaders[elfID][secRef.headerIndex];

        outSectionSize = alignup(outSectionSize, inSecHdr.sh_addralign);

        auto& copyCmds = inputSectionCopyCommands[elfID];
        copyCmds.resize(sectionHeaders[elfID].size());
        copyCmds[secRef.headerIndex] = PartCopy{.size = inSecHdr.sh_size,
                                                .dstOffset = outSectionSize};
        //copyCmds[secRef.headerIndex].push_back({.size = inSecHdr.sh_size,
        //                                        .dstOffset = outSectionSize});
        outSectionSize += inSecHdr.sh_size;
    }
    outputSectionSizes[outSectionID] = outSectionSize;
    return StatusCode::ok;
};

enum class MergeType {
    fixedLength,
    variableLength
};

struct MergeSections {
    struct {
        readonly_span<std::byte*> elfAddresses;
        readonly_span<readonly_span<Elf64_Shdr>> sectionHeaders;
        readonly_span<SectionRef> sectionRefs;
        OutSectionID outSectionID;
        MergeType mergeType;
    } in;
    struct {
        out<std::vector<size_t>> outputSectionSizes;
        out<Vector2D<SectionMemCopies>> inputSectionCopyCommands;
        out<std::byte*> materializedView;
        out<std::pmr::memory_resource> materializedSectionMemory;
    } out;
};

auto mergeSections(MergeSections p) -> StatusCode {
    auto& [elfAddresses,
           sectionHeaders,
           sectionRefs,
           outSectionID,
           mergeType] = p.in;

    auto& [outputSectionSizes,
           inputSectionCopyCommands,
           materializedView,
           materializedSectionMemory] = p.out;

    size_t outSectionSize{0};
    std::unordered_map<std::string_view, size_t> elementToOutSectionOffset;

    Vector2D<std::string_view> sectionElements;
    sectionElements.reserve(sectionRefs.size());

    for (auto& secRef : sectionRefs) {
        auto baseAddress = elfAddresses[secRef.elfIndex];
        auto secHdr = sectionHeaders[secRef.elfIndex][secRef.headerIndex];
        // While we are here, prepare enough space for the copy commands later
        inputSectionCopyCommands[secRef.elfIndex].resize(sectionHeaders[secRef.elfIndex].size());

        const bool useFixedLength{mergeType == MergeType::fixedLength};
        auto& secElements = sectionElements.emplace_back();

        auto stringsStart = estd::start_lifetime_as_array<char>(baseAddress + secHdr.sh_offset, secHdr.sh_size);
        auto stringsEnd = stringsStart + secHdr.sh_size;
        for (char* nextEnd{}; stringsStart < stringsEnd; stringsStart = nextEnd) {
            //
            nextEnd = useFixedLength ? stringsStart + secHdr.sh_entsize :
                                       std::find(stringsStart, stringsEnd, '\0') + 1;
            if (nextEnd > stringsEnd) {
                return report(StatusCode::not_ok, "section merger encountered out of bounds element");
            }

            // Using a string_view because it comes with a hash function built in
            std::string_view element{stringsStart, nextEnd};
            secElements.push_back(element);

            auto [_, newInsert] = elementToOutSectionOffset.try_emplace(element, outSectionSize);
            if (!newInsert) continue;

            outSectionSize += element.size();
        }
    }
    // After merging the size of the section is known
    outputSectionSizes[outSectionID] = outSectionSize;

    for_each_indexed(sectionRefs, [&](SectionRef const& secRef, size_t secRefIndex) {
        auto& sectionMemCopies = inputSectionCopyCommands[secRef.elfIndex][secRef.headerIndex];
        auto& elements = sectionElements[secRefIndex];

        sectionMemCopies = std::vector<PartCopy>{};
        auto& partCopies = std::get<std::vector<PartCopy>>(sectionMemCopies);
        partCopies.reserve(elements.size());
        for (auto e : elements) {
            partCopies.push_back({.size = e.size(),
                                  .dstOffset = elementToOutSectionOffset.at(e)});
        }
    });

    //
    materializedView = static_cast<std::byte*>(materializedSectionMemory.allocate(outSectionSize));
    for (auto& [string, offset] : elementToOutSectionOffset) {
        std::memcpy(materializedView + offset, string.data(), string.size());
    }

    return StatusCode::ok;
};

} // namespace

auto mergeAndSortInputSections(parametersFor::MergeAndSortInputSections p) -> StatusCode {
    auto& [elfAddresses, sortKeys, sectionHeaders, outSectionFlags] = p.in;
    auto& [outputToInputSections] = p.inout;
    auto& [outputSectionSizes, inputSectionCopyCommands, materializedViews, materializedSectionMemory] = p.out;

    outputSectionSizes.resize(outSectionFlags.size());
    inputSectionCopyCommands.resize(elfAddresses.size());
    materializedViews.resize(outSectionFlags.size(), nullptr);

    StatusCode status{StatusCode::ok};

    for_each_indexed(outputToInputSections, [&](std::vector<SectionRef>& sectionRefs, size_t outSectionID) {
        std::sort(sectionRefs.begin(), sectionRefs.end(), [&](SectionRef const& a, SectionRef const& b) {
            auto precA = sortKeys[a.elfIndex];
            auto precB = sortKeys[b.elfIndex];
            // Same file? sort by file precendece else sort by section location
            return precA != precB ? precA < precB : a.headerIndex < b.headerIndex;
        });
        // all outsectionIDs have flags, but the linter seem to realize this
        // NOLINTNEXTLINE
        auto sectionFlags = outSectionFlags[outSectionID];
        StatusCode mergeResult{StatusCode::ok};
        if (!(sectionFlags & SHF_MERGE)) {
            concatenateSections({.in{sectionHeaders, sectionRefs, static_cast<OutSectionID>(outSectionID)},
                                 .out{outputSectionSizes, inputSectionCopyCommands}});
            return;
        }

        MergeType mergeType = (sectionFlags & SHF_STRINGS) ? MergeType::variableLength : MergeType::fixedLength;
        mergeResult = mergeSections({.in{elfAddresses, sectionHeaders, sectionRefs, static_cast<OutSectionID>(outSectionID), mergeType},
                                     .out{outputSectionSizes, inputSectionCopyCommands, materializedViews[outSectionID], materializedSectionMemory}});
        if (mergeResult != StatusCode::ok) {
            status = mergeResult;
        }
    });

    return status;
}
auto sortOutputSections(parametersFor::SortOutputSections p) -> StatusCode {
    auto& [types, flags] = p.in;
    auto& [segmentedSections] = p.out;

    auto flagsToSegmentIndex = [](Elf64_Xword flag) -> size_t {
        if (!(flag & SHF_ALLOC)) return (meta::SegmentLocation::notLoaded);
        if (flag & SHF_TLS) return (meta::SegmentLocation::tlsTemplate);
        if ((flag & SHF_WRITE) && (flag & SHF_EXECINSTR)) return (meta::SegmentLocation::readWriteExecute);
        if (flag & SHF_WRITE) return (meta::SegmentLocation::readWrite);
        if (flag & SHF_EXECINSTR) return (meta::SegmentLocation::readExecute);
        // else
        return (meta::SegmentLocation::readOnly);
    };

    for_each_indexed(flags, [&](Elf64_Xword flag, size_t outsecID) {
        segmentedSections[flagsToSegmentIndex(flag)].push_back(static_cast<OutSectionID>(outsecID));
    });

    parallel_for_each_indexed(segmentedSections, [&](std::vector<OutSectionID>& sections, size_t segmentLocation) {
        if (segmentLocation == meta::SegmentLocation::notLoaded)
            return;
        std::sort(sections.begin(), sections.end(), [&](OutSectionID a, OutSectionID b) {
            return (types[a] == SHT_NOBITS) < (types[b] == SHT_NOBITS);
        });
    });

    return StatusCode::ok;
}

namespace /*internal*/
{
struct InputToOutputSectionOffset {
    struct {
        in<SectionRef> secRef;
        in<size_t> offsetInInput;
        in<Vector2D<SectionMemCopies>> inputSectionCopyCommands;
    } in;
    struct {
        out<size_t> offsetInOutput;
    } out;
};

auto inputToOutputSectionOffset(InputToOutputSectionOffset p) -> StatusCode {
    auto& [secRef, offsetInInput, inputSectionCopyCommands] = p.in;
    auto& [offsetInOutput] = p.out;
    offsetInOutput = offsetInInput;

    auto& copyCmds = inputSectionCopyCommands[secRef.elfIndex][secRef.headerIndex];

    auto visitor = overloaded{
        [&](std::vector<PartCopy> const& copyCmdsVec) -> StatusCode {
            for (size_t start{0}, end{0}; auto& copyCmd : copyCmdsVec) {
                end += copyCmd.size;
                
                if (start <= offsetInInput && offsetInInput < end) { // NOLINT
                    offsetInOutput = offsetInInput + copyCmd.dstOffset; // NOLINT
                    return StatusCode::ok;
                }
                start = end;
            }
            return report(StatusCode::bad_input_file, "offset in source section is not in a copied region of output section. Offset is: ", offsetInInput);
        },
        [&](PartCopy const& cmd) -> StatusCode {
            offsetInOutput = offsetInInput + cmd.dstOffset; // NOLINT
            return StatusCode::ok;
        },
        [&](auto&) -> StatusCode {
            return report(StatusCode::not_ok, "tried finding a way from file ", secRef.elfIndex, ", section: ", secRef.headerIndex, " to the output, but there was none");
        }};
    return std::visit(visitor, copyCmds);
};

struct ProcessRelas {
    struct {
        size_t elfID;
        size_t headerID;
        readonly_span<Elf64_Rela> relas;
        const char* symStrings;
        SymbolTable const& symbolTable;
        in<Vector2D<OutSectionID>> inputToOutputSection;
        in<Vector2D<SectionMemCopies>> inputSectionCopyCommands;
        readonly_span<Elf64_Sym> linkedSymbols;

        OutSectionID gotSectionIndex;

    } in;

    struct {
        inout<std::pmr::unordered_map<std::string_view, size_t>> symbolNamesThatNeedGOTEntries;
        inout<std::vector<GOTEntryPatchupInfo>> gotEntryPatches;
    } inout;

    struct {
        out<std::vector<ProcessedRela>> processResults;
    } out;
};

auto processRelas(ProcessRelas p) -> StatusCode {
    auto& [elfID, headerID, relas, symStrings, symbolTable, inputToOutputSection, inputSectionCopyCommands, linkedSymbols, gotSectionIndex] = p.in;
    auto& [symbolNamesThatNeedGOTEntries, gotEntryPatches] = p.inout;
    auto& [processResults] = p.out;
    StatusCode status{StatusCode::ok};

    auto needsGOTEntry = [](uint32_t type) {
        switch (type) {
            case R_X86_64_GOT32:
            case R_X86_64_GOT64:
            case R_X86_64_GOTPCREL:
            case R_X86_64_GOTPCREL64:
            case R_X86_64_GOTPCRELX:
            case R_X86_64_REX_GOTPCRELX:
                return true;
        }
        return false;
    };

    auto addGOTEntry = [&](ProcessedRela& resRela, std::string_view symName, GOTEntryPatchupInfo patchInfo) {
        auto nextIndex = symbolNamesThatNeedGOTEntries.size() + meta::numReservedGotEntries;
        auto [entry, wasInserted] = symbolNamesThatNeedGOTEntries.try_emplace(symName, nextIndex);
        if (wasInserted) {
            gotEntryPatches.push_back(patchInfo);
        }

        resRela.symbolSectionID = gotSectionIndex;
        resRela.symbolValue = entry->second * sizeof(Elf64_Addr);
    };

    for (auto& rela : relas) {
        auto& sym = linkedSymbols[ELF64_R_SYM(rela.r_info)];

        if (sym.st_shndx == SHN_XINDEX) {
            status = report(StatusCode::bad_input_file, " symbol points to a section with a too high index");
            continue;
        }

        if (sym.st_shndx == SHN_ABS) {
            processResults.push_back(
                {.addend = 0,
                 .outputSectionOffset = 0,
                 .symbolValue = sym.st_value,
                 .type = static_cast<uint32_t>(ELF64_R_TYPE(rela.r_info)),
                 .symbolSectionID = 0,
                 .note = ProcessedRela::Note::absoluteValue});
            continue;
        }

        if (ELF32_ST_BIND(sym.st_info) == STB_LOCAL) {
            if (sym.st_shndx == SHN_UNDEF) {
                status = report(StatusCode::not_ok, " local symbol undefined");
                continue;
            }

            size_t outputSectionOffset{};
            auto outSectionStatus = inputToOutputSectionOffset({.in{{elfID, headerID}, rela.r_offset, inputSectionCopyCommands}, .out{outputSectionOffset}});
            if (outSectionStatus != StatusCode::ok) return outSectionStatus;
            size_t symbolValue{};
            auto symbolValueStatus = inputToOutputSectionOffset({.in{{elfID, sym.st_shndx}, sym.st_value, inputSectionCopyCommands}, .out{symbolValue}});
            if (symbolValueStatus != StatusCode::ok) return symbolValueStatus;

            processResults.push_back(
                {.addend = rela.r_addend,
                 .outputSectionOffset = outputSectionOffset,
                 .symbolValue = symbolValue,
                 .type = static_cast<uint32_t>(ELF64_R_TYPE(rela.r_info)),
                 .symbolSectionID = inputToOutputSection[elfID][sym.st_shndx],
                 .note = ProcessedRela::Note::none});
            continue;
        }

        auto& resRela = processResults.emplace_back();
        resRela.addend = rela.r_addend;
        auto outputSectionStatus = inputToOutputSectionOffset({.in{{elfID, headerID}, rela.r_offset, inputSectionCopyCommands}, .out{resRela.outputSectionOffset}});
        if (outputSectionStatus != StatusCode::ok) return outputSectionStatus;
        resRela.type = static_cast<uint32_t>(ELF64_R_TYPE(rela.r_info));

        // We have a global or weak symbol. This requires a lookup in the symbolTable to find the definition

        std::string_view symName{sym.st_name + symStrings};
        /*Is a global symbol, find in table*/;
        auto it = symbolTable.find(symName);
        if (it == symbolTable.end()) {
            return report(StatusCode::symbol_undefined, symName, " (not even present in symbol table, something went horribly wrong)");
        }
        auto& [_, firstLoad] = it->second;
        bool isWeakSym{ELF64_ST_BIND(sym.st_info) == STB_WEAK};
        if (!firstLoad.symbol && !isWeakSym) {
            status = report(StatusCode::symbol_undefined, symName, " ");
            continue;
        }

        if (!firstLoad.symbol && isWeakSym) {
            resRela.symbolValue = 0;
            resRela.symbolSectionID = 0;
            resRela.note = ProcessedRela::Note::undefinedWeak;
            if (needsGOTEntry(resRela.type)) {
                addGOTEntry(resRela, symName, {0, SHN_UNDEF, 0});
            }
            continue;
        }
        auto& symbol = *firstLoad.symbol;

        if (symbol.st_shndx == SHN_XINDEX) {
            status = report(StatusCode::bad_input_file, " symbol points to a section with a too high index");
            continue;
        }

        resRela.symbolSectionID = inputToOutputSection[firstLoad.elfID][firstLoad.symbol->st_shndx];
        if (symbol.st_shndx == SHN_ABS) {
            resRela.symbolValue = firstLoad.symbol->st_value;
            resRela.note = ProcessedRela::Note::absoluteValue;
        } else {
            auto symbolValueState = inputToOutputSectionOffset({.in{{firstLoad.elfID, symbol.st_shndx}, symbol.st_value, inputSectionCopyCommands}, .out{resRela.symbolValue}});
            if (symbolValueState != StatusCode::ok)
                return symbolValueState;
        }

        if (needsGOTEntry(resRela.type)) {
            addGOTEntry(resRela, symName, {firstLoad.elfID, symbol.st_shndx, symbol.st_value});
        } else if (resRela.type == R_X86_64_SIZE32 || resRela.type == R_X86_64_SIZE64) {
            resRela.symbolValue = sym.st_size;
        }
    }

    return status;
}
} // namespace
auto preProcessesRelocations(parametersFor::PreProcessesRelocations p) -> StatusCode {
    auto& [elfAddresses, sectionHeaders, symbolTable, inputSectionCopyCommands, inputToOutputSection, numberOfOutputSections, gotSectionIndex] = p.in;
    auto& [processedRelas, gotEntryPatches] = p.out;
    processedRelas.resize(numberOfOutputSections);

    StatusCode status{StatusCode::ok};

    std::pmr::monotonic_buffer_resource mem;
    std::pmr::unordered_map<std::string_view, size_t> symbolNamesThatNeedGOTEntries{&mem};
    symbolNamesThatNeedGOTEntries.reserve(symbolTable.size());

    for_each_indexed(sectionHeaders, [&](readonly_span<Elf64_Shdr> headers, size_t elfID) {
        // every elfID accesses a valid elfAddress... those are the rules
        // NOLINTNEXTLINE
        auto address = elfAddresses[elfID];
        for_each_indexed(headers, [&](Elf64_Shdr const& header, size_t) {
            if (header.sh_type != SHT_RELA) return;
            if (header.sh_entsize != sizeof(Elf64_Rela)) {
                status = report(StatusCode::not_ok, "relocation not of the right size");
                return;
            }

            auto& symTabHdr = headers[header.sh_link];
            auto linkedSymbols = view_as_span<Elf64_Sym>(address + symTabHdr.sh_offset, symTabHdr.sh_size / sizeof(Elf64_Sym));
            auto& symStrTabHdr = headers[symTabHdr.sh_link];
            auto symStrings = estd::start_lifetime_as_array<char>(address + symStrTabHdr.sh_offset, symStrTabHdr.sh_size);
            auto outSectionID = inputToOutputSection[elfID][header.sh_info];

            if (outSectionID == meta::notAnOutputSection) {
                //relocations in a section that is not part of the output; Can be skipped
                return;
            }

            auto relas = view_as_span<Elf64_Rela>(address + header.sh_offset, header.sh_size / header.sh_entsize);

            auto processStatus = processRelas({.in{elfID, header.sh_info, relas, symStrings, symbolTable, inputToOutputSection, inputSectionCopyCommands, linkedSymbols, gotSectionIndex},
                                               .inout{symbolNamesThatNeedGOTEntries, gotEntryPatches},
                                               .out{processedRelas[outSectionID]}});
            if (processStatus != StatusCode::ok) {
                status = processStatus;
                return;
            }
        });
    });
    return status;
}
auto constructLoadedSectionLayout(parametersFor::ConstructLoadedSectionLayout p) -> StatusCode {
    auto& [segmentedSections, outputSectionSizes, outputSectionAlignments, outputSectionTypes] = p.in;
    auto& [programHeaders, outputSectionAddresses, outputSectionFileOffsets] = p.out;

    outputSectionAddresses.resize(outputSectionSizes.size(), meta::notAnOutputSection);
    outputSectionFileOffsets.resize(outputSectionSizes.size(), meta::notAnOutputSection);
    programHeaders.reserve(meta::numProgramSegments);

    // Create a program header for each loaded segment with some content
    for_each_indexed(segmentedSections, [&](std::vector<OutSectionID> const& sections, size_t segmentIndex) {
        if (sections.empty() || segmentIndex == meta::SegmentLocation::notLoaded)
            return;
        programHeaders.emplace_back(meta::segmentTypes[segmentIndex], meta::segmentFlags[segmentIndex]);
    });

    auto& gnuStack = programHeaders.emplace_back(PT_GNU_STACK, meta::segmentFlags[meta::SegmentLocation::readWrite]);
    gnuStack.p_align = 0x10;
    static_assert((sizeof(Elf64_Ehdr) % alignof(Elf64_Phdr)) == 0);
    const size_t fileHeadersSize{sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr) * programHeaders.size()};

    size_t fileStartPos{fileHeadersSize};
    size_t segmentStartAddress{fileHeadersSize + meta::virtualAddressStart};
    size_t ptHdrIndex{0};
    for_each_indexed(segmentedSections, [&](std::vector<OutSectionID> const& sections, size_t segmentIndex) {
        if (sections.empty() || segmentIndex == meta::SegmentLocation::notLoaded)
            return;

        auto filePos = fileStartPos;
        auto addressPos = segmentStartAddress;

        for (auto outsecID : sections) {
            auto sectionAlignment = outputSectionAlignments[outsecID];
            auto sectionSize = outputSectionSizes[outsecID];
            filePos = alignup(filePos, sectionAlignment);
            addressPos = alignup(addressPos, sectionAlignment);

            outputSectionAddresses[outsecID] = addressPos;
            outputSectionFileOffsets[outsecID] = filePos;

            if (outputSectionTypes[outsecID] == SHT_NOBITS) continue;

            filePos += sectionSize;
            addressPos += sectionSize;
        }

        auto segmentFileSize = filePos - fileStartPos;
        auto memSize = addressPos - segmentStartAddress;

        auto& ptHdr = programHeaders[ptHdrIndex];
        ptHdr.p_align = meta::pageSize;
        ptHdr.p_offset = fileStartPos;
        ptHdr.p_memsz = memSize;
        ptHdr.p_filesz = segmentFileSize;
        ptHdr.p_vaddr = ptHdr.p_paddr = segmentStartAddress;
        if (ptHdrIndex == 0) {
            // The first segment covers the headers as well
            ptHdr.p_vaddr -= fileHeadersSize;
            ptHdr.p_paddr -= fileHeadersSize;
            ptHdr.p_offset -= fileHeadersSize;
            // This icnreases the sizes
            ptHdr.p_filesz += fileHeadersSize;
            ptHdr.p_memsz += fileHeadersSize;
        }
        ++ptHdrIndex;

        fileStartPos = alignup(fileStartPos + segmentFileSize, meta::pageSize);
        segmentStartAddress = alignup(segmentStartAddress + memSize, meta::pageSize);
    });

    return StatusCode::ok;
}

auto synthesizeSyntheticSections(parametersFor::SynthesizeSyntheticSections p) -> StatusCode {
    auto& [gotID, symTabID, strTabID, shstrTabID, gotEntryPatches, outputSectionAddresses, inputToOutputSection, inputSectionCopyCommands, flags, names, symbolTable, elfAddresses, sectionHeaders, enoughStringTableMemory, enoughSymbolTableMemory] = p.in;
    auto& [materializedViews, outputSectionSizes] = p.inout;

    auto& [numLocalSymbols, sh_names] = p.out;

    // Global Offset Table

    for_each_indexed(gotEntryPatches, [&](GOTEntryPatchupInfo patch, size_t patchID) {
        if (patch.headerID == SHN_UNDEF) return;
        Elf64_Addr gotEntry{};
        std::ignore = inputToOutputSectionOffset({.in{{patch.elfID, patch.headerID}, patch.symbolValue, inputSectionCopyCommands},
                                                  .out{gotEntry}});
        gotEntry += outputSectionAddresses[inputToOutputSection[patch.elfID][patch.headerID]];
        auto gotByteOffset = sizeof(Elf64_Addr) * (meta::numReservedGotEntries + patchID);
        std::memcpy(materializedViews[gotID] + gotByteOffset, &gotEntry, sizeof(gotEntry));
    });

    // Symbol String Table

    //std::vector<char> symbolStringTable{'\0'};
    //std::vector<Elf64_Sym> symbolHeaders{Elf64_Sym{}};

    auto stringTableMem = enoughStringTableMemory;
    materializedViews[strTabID] = stringTableMem;

    std::memset(stringTableMem, 0, sizeof(char));
    size_t stringTableSize{sizeof(char)};

    auto insertSymbolString = [&](std::string_view symName) {
        std::memcpy(stringTableMem + stringTableSize, symName.data(), symName.size());
        stringTableSize += symName.size();
        std::memset(stringTableMem + (stringTableSize++), 0, 1);
    };

    auto symbolTableMem = enoughSymbolTableMemory;
    materializedViews[symTabID] = symbolTableMem;

    std::memset(symbolTableMem, 0, sizeof(Elf64_Sym));
    size_t symbolTableSize{sizeof(Elf64_Sym)};

    auto insertSymbol = [&](Elf64_Sym& sym) {
        std::memcpy(symbolTableMem + symbolTableSize, &sym, sizeof(Elf64_Sym));
        symbolTableSize += sizeof(Elf64_Sym);
    };

    auto pushSymbol = [&](Elf64_Sym sym, std::string_view symName, size_t elfID) {
        sym.st_name = static_cast<Elf64_Word>(stringTableSize);
        insertSymbolString(symName);

        if (sym.st_shndx == SHN_ABS) {
            insertSymbol(sym);
            return;
        }

        auto outSectionID = inputToOutputSection[elfID][sym.st_shndx];

        if (!(flags[outSectionID] & SHF_ALLOC))
            return;

        auto inputAddress = outputSectionAddresses[outSectionID];

        std::ignore = inputToOutputSectionOffset({.in{{elfID, sym.st_shndx}, sym.st_value, inputSectionCopyCommands},
                                                  .out{sym.st_value}});
        sym.st_value += inputAddress;
        sym.st_shndx = static_cast<Elf64_Section>(outSectionID) + 1;
        insertSymbol(sym);
    };

    for_each_indexed(sectionHeaders, [&](readonly_span<Elf64_Shdr> headers, size_t inputIndex) {
        for_each_indexed(headers, [&](Elf64_Shdr const& header, size_t) {
            if (header.sh_type != SHT_SYMTAB) return;
            auto address = elfAddresses[inputIndex];

            auto symbols = view_as_span<Elf64_Sym>(address + header.sh_offset, header.sh_size / sizeof(Elf64_Sym));
            auto& symStrTabHdr = headers[header.sh_link];
            auto symStrings = estd::start_lifetime_as_array<char>(address + symStrTabHdr.sh_offset, symStrTabHdr.sh_size);

            for (size_t i = 1; i < header.sh_info; ++i) {
                std::string_view symName{symbols[i].st_name + symStrings};
                pushSymbol(symbols[i], symName, inputIndex);
            }
        });
    });

    numLocalSymbols = static_cast<Elf64_Word>(symbolTableSize / sizeof(Elf64_Sym));

    for (auto& [symName, entry] : symbolTable) {
        if (!entry.firstLoad.symbol) continue;
        pushSymbol(*entry.firstLoad.symbol, symName, entry.firstLoad.elfID);
    }

    outputSectionSizes[symTabID] = symbolTableSize;
    outputSectionSizes[strTabID] = stringTableSize;

    materializedViews[shstrTabID] = stringTableMem + stringTableSize;
    auto shstrTabOffset = stringTableSize;
    std::memset(stringTableMem + (stringTableSize++), 0, 1);

    sh_names.reserve(names.size());

    for_each_indexed(names, [&](std::string_view name, size_t) {
        sh_names.push_back(static_cast<Elf64_Word>(stringTableSize - shstrTabOffset));
        insertSymbolString(name);
    });
    outputSectionSizes[shstrTabID] = stringTableSize - shstrTabOffset;

    return StatusCode::ok;
}

auto buildElfAndSectionHeaders(parametersFor::BuildElfAndSectionHeaders p) -> StatusCode {
    auto& [names, sh_names, types, flags, alignments, outputSectionAddresses, outputSectionFileOffsets, outputSectionSizes, sectionDataEnd, symTabID, strTabID, shstrTabID, numLocalSymbols, inputSectionCopyCommands, inputToOutputSection, entrySymbolInfo, numProgramHeaders] = p.in;

    auto& [outputSectionHeaders, elfHeader] = p.out;
    outputSectionHeaders.clear();
    outputSectionHeaders.reserve(names.size());
    outputSectionHeaders.emplace_back();
    for_each_indexed(names, [&](std::string_view, size_t id) {
        outputSectionHeaders.push_back({
            .sh_name = sh_names[id],
            .sh_type = types[id],
            .sh_flags = flags[id],
            .sh_addr = outputSectionAddresses[id],
            .sh_offset = outputSectionFileOffsets[id],
            .sh_size = outputSectionSizes[id],
            .sh_link = 0,
            .sh_info = 0,
            .sh_addralign = std::max(alignments[id], Elf64_Xword{1}),
            .sh_entsize = 0,
        });
    });
    outputSectionHeaders[symTabID + 1].sh_link = static_cast<Elf64_Word>(strTabID + 1);
    outputSectionHeaders[symTabID + 1].sh_info = static_cast<Elf64_Word>(numLocalSymbols);
    outputSectionHeaders[symTabID + 1].sh_entsize = sizeof(Elf64_Sym);

    auto sectionHeaderFileOffset = alignup(sectionDataEnd, alignof(Elf64_Shdr));

    auto& entrySym = *entrySymbolInfo.firstLoad.symbol;
    SectionRef entrySecRef{entrySymbolInfo.firstLoad.elfID, entrySym.st_shndx};
    auto entrySectionID = inputToOutputSection[entrySecRef.elfIndex][entrySecRef.headerIndex];

    size_t entrySymbolOutputOffset;
    std::ignore = inputToOutputSectionOffset({.in{entrySecRef, entrySym.st_value, inputSectionCopyCommands},
                                              .out{entrySymbolOutputOffset}});
    elfHeader = Elf64_Ehdr{
        .e_ident = {0x7f, 'E', 'L', 'F', ELFCLASS64, ELFDATA2LSB, EV_CURRENT, ELFOSABI_GNU},
        .e_type = ET_EXEC,
        .e_machine = EM_X86_64,
        .e_version = EV_CURRENT,
        .e_entry = outputSectionAddresses[entrySectionID] + entrySymbolOutputOffset,
        .e_phoff = sizeof(Elf64_Ehdr),
        .e_shoff = sectionHeaderFileOffset,
        .e_flags = 0,
        .e_ehsize = sizeof(Elf64_Ehdr),
        .e_phentsize = sizeof(Elf64_Phdr),
        .e_phnum = static_cast<Elf64_Half>(numProgramHeaders),
        .e_shentsize = sizeof(Elf64_Shdr),
        .e_shnum = static_cast<Elf64_Half>(outputSectionHeaders.size()),
        .e_shstrndx = static_cast<Elf64_Half>(shstrTabID + 1),
    };

    return StatusCode::ok;
}
} // namespace cppld