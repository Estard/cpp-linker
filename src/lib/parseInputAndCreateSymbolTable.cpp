#include "parseInputAndCreateSymbolTable.hpp"
#include "convenient_functions.hpp"
#include "statusreport.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <utility>
#include <ar.h>

namespace cppld {

auto parseInputAndCreateSymbolTable(parametersFor::ParseInputAndCreateSymbolTable p) -> StatusCode {
    auto& [addresses, memSizes] = p.in;

    auto& [elfAddresses,
           sortKeys,
           sectionHeaders,
           sectionStringTables,
           archiveExtractionMemory,
           symbolTable] = p.out;

    StatusCode status{StatusCode::ok};

    std::vector<uint32_t> elfFileIndices;
    std::vector<uint32_t> archiveFileIndices;

    status = classifyInput({.in{addresses, memSizes}, .out{elfFileIndices, archiveFileIndices}});
    if (status != StatusCode::ok) return status;

    auto elfParseFuture = std::async(std::launch::async, parseElfFiles,
                                     parametersFor::ParseElfFiles{.in{addresses, memSizes, elfFileIndices},
                                                                  .out{elfAddresses, sortKeys, sectionHeaders, sectionStringTables}});

    std::vector<SortKey> archiveMemberSortKeys;
    std::vector<ArchiveMemberState> archiveMembersStates;
    ArchiveSymbolTable archiveSymbolTable;
    auto archiveParseFuture = std::async(std::launch::async, parseArchiveMembers,
                                         parametersFor::ParseArchiveMembers{.in{addresses, memSizes, archiveFileIndices},
                                                                            .out{archiveMemberSortKeys, archiveMembersStates, archiveSymbolTable}});
    auto elfParseStatus = elfParseFuture.get();
    auto archiveParseStatus = archiveParseFuture.get();
    if (elfParseStatus != StatusCode::ok || archiveParseStatus != StatusCode::ok) return StatusCode::not_ok;

    size_t elfInsertStartID{0};
    std::vector<std::string_view> searchedSymbolNames{};
    status = insertSymbolsIntoSymbolTable({.in{elfAddresses,
                                               sectionHeaders,
                                               sortKeys,
                                               elfInsertStartID},
                                           .inout{symbolTable},
                                           .out{searchedSymbolNames}});
    if (status != StatusCode::ok) return status;

    for (std::vector<size_t> archiveMemberIDsToExtract;; archiveMemberIDsToExtract.clear()) {
        status = determineArchiveMembersToExtract({.in{symbolTable, archiveSymbolTable, sortKeys, archiveMemberSortKeys, searchedSymbolNames},
                                                   .out{archiveMemberIDsToExtract}});

        if (status != StatusCode::ok) return status;

        elfInsertStartID = elfAddresses.size();
        status = extractArchiveMembers({.in{addresses, memSizes, archiveMemberSortKeys, archiveMemberIDsToExtract},
                                        .inout{archiveMembersStates},
                                        .out{elfAddresses, sortKeys, sectionHeaders, sectionStringTables, archiveExtractionMemory}});
        if (status != StatusCode::ok) return status;

        if (elfInsertStartID == elfAddresses.size())
            break;

        searchedSymbolNames.clear();
        status = insertSymbolsIntoSymbolTable({.in{{elfAddresses.begin() + static_cast<std::ptrdiff_t>(elfInsertStartID), elfAddresses.end()},
                                                   {sectionHeaders.begin() + static_cast<std::ptrdiff_t>(elfInsertStartID), sectionHeaders.end()},
                                                   sortKeys,
                                                   elfInsertStartID},
                                               .inout{symbolTable},
                                               .out{searchedSymbolNames}});
        if (status != StatusCode::ok) return status;
    }

    return status;
}

// Implementation of the substeps
// They should all more or less get inlined into the primary function

namespace /*internal*/ {

constexpr std::array<uint8_t, 7> elfIdent{
    0x7f, 'E', 'L', 'F', ELFCLASS64, ELFDATA2LSB, EV_CURRENT};

} // namespace

auto classifyInput(parametersFor::ClassifyInput p) -> StatusCode {
    auto& [addresses, memSizes] = p.in;
    auto& [elfFileIndices, archiveFileIndices] = p.out;

    StatusCode status{StatusCode::ok};
    for_each_indexed(addresses, [&](void* address, size_t fileIndex) {
        // The linter thinks memSizes is an uninitialized pointer. But we all know that
        // addresses and memSizes have the same size and thus the index is valid
        // NOLINTNEXTLINE
        if (memSizes[fileIndex] < sizeof(Elf64_Ehdr)) {
            status = report(StatusCode::bad_input_file, "File #", fileIndex, " is to small");
            return;
        }
        if (std::memcmp(address, elfIdent.data(), elfIdent.size()) == 0) {
            elfFileIndices.push_back(static_cast<uint32_t>(fileIndex));
        } else if (std::memcmp(address, ARMAG, SARMAG) == 0) {
            archiveFileIndices.push_back(static_cast<uint32_t>(fileIndex));
        } else {
            status = report(StatusCode::bad_input_file, "File #", fileIndex, "is neither an archive nor an elf file");
        }
    });
    return status;
}

namespace /*internal*/ {

// Common Code for initial Parsing and Archive Member Extraction
struct InitRelaElf {
    struct {
        std::byte* address;
        size_t memSize;
    } in;
    struct {
        out<readonly_span<Elf64_Shdr>> sectionHeaders;
        out<char*> strTable;
    } out;
};
auto initRelaElf(InitRelaElf p) -> StatusCode {
    auto& [address, memSize] = p.in;
    auto& [secHeaders, strTable] = p.out;

    auto& header = *estd::start_lifetime_as<Elf64_Ehdr>(address);
    if (header.e_type != ET_REL) return report(StatusCode::not_ok, "Elf File is not of type relocatable");
    if (header.e_machine != EM_X86_64) return report(StatusCode::not_ok, "Elf File is not for x86_64");
    if (header.e_shentsize != sizeof(Elf64_Shdr)) return report(StatusCode::not_ok, "Elf File does not have 64 Bit format for section headers");
    if (memSize < (header.e_shoff + static_cast<size_t>(header.e_shnum) * header.e_shentsize)) return report(StatusCode::bad_input_file, "Elf File accesses out of bounds memory");

    if (header.e_shnum == 0 || header.e_shnum >= SHN_LORESERVE) /*Zero or too many sections*/
        return report(StatusCode::bad_input_file, " Elf File with zero or too many sections");
    if (header.e_shstrndx == SHN_XINDEX) /*Too many Sections,too*/
        return report(StatusCode::bad_input_file, " Elf File with too many sections");

    secHeaders = view_as_span<Elf64_Shdr>(address + header.e_shoff, header.e_shnum);
    // Validate that all sections are within the mapped memory region
    for (auto& secHdr : secHeaders) {
        if (secHdr.sh_type == SHT_NOBITS) continue;
        if (secHdr.sh_type == SHT_GROUP) return report(StatusCode::not_ok, " Group Sections not supported");
        if (memSize < (secHdr.sh_offset + secHdr.sh_size)) return report(StatusCode::bad_input_file, "Elf File accesses out of bounds memory");
    }

    auto& strTableHdr = secHeaders[header.e_shstrndx];
    strTable = estd::start_lifetime_as_array<char>(address + strTableHdr.sh_offset, strTableHdr.sh_size);

    return StatusCode::ok;
}

SortKey makeSortKey(uint64_t base, uint32_t sub = 0) {
    return (base << 32ull) + (sub);
}

std::pair<uint32_t, uint32_t> split(SortKey k) {
    constexpr uint64_t lower32BitsMask = (1ull << 32ull) - 1ull;
    return {static_cast<uint32_t>(k >> 32ull), static_cast<uint32_t>(k & lower32BitsMask)};
}
} // namespace

auto parseElfFiles(parametersFor::ParseElfFiles p) -> StatusCode {
    auto& [addresses, memSizes, elfFileIndices] = p.in;

    auto& [elfAddresses, precedences, sectionHeaders, sectionStringTables] = p.out;
    elfAddresses.resize(elfFileIndices.size());
    precedences.resize(elfFileIndices.size());
    sectionHeaders.resize(elfFileIndices.size());
    sectionStringTables.resize(elfFileIndices.size());

    StatusCode status{StatusCode::ok};

    // Doesn't actually make much of a difference here.
    parallel_for_each_indexed(elfFileIndices, [&](uint32_t const& fileIndex, size_t elfID) {
        auto address = static_cast<std::byte*>(addresses[fileIndex]);
        auto memSize = memSizes[fileIndex];
        readonly_span<Elf64_Shdr> secHeaders{};
        char* strTable{};
        if (auto initStatus = initRelaElf({.in{address, memSize}, .out{secHeaders, strTable}});
            initStatus != StatusCode::ok) {
            std::atomic_ref{status}.store(initStatus);
            return;
        }
        precedences[elfID] = (makeSortKey(fileIndex));
        elfAddresses[elfID] = (address);
        sectionHeaders[elfID] = (secHeaders);
        sectionStringTables[elfID] = (strTable);
    });
    return StatusCode::ok;
}

auto parseArchiveMembers(parametersFor::ParseArchiveMembers p) -> StatusCode {
    auto& [addresses, memSizes, archiveFileIndices] = p.in;
    auto& [archiveMemberSortKeys, archiveMemberStates, archiveSymbolTable] = p.out;
    archiveMemberSortKeys.reserve(archiveFileIndices.size());
    archiveMemberStates.reserve(archiveFileIndices.size());
    archiveSymbolTable.reserve(archiveFileIndices.size());

    for (auto fileIndex : archiveFileIndices) {
        auto address = static_cast<std::byte*>(addresses[fileIndex]);
        auto memSize = memSizes[fileIndex];
        auto badFileError = [&]() { return report(StatusCode::bad_input_file, " input file #", fileIndex); };

        if (memSize < (SARMAG + sizeof(ar_hdr))) return StatusCode::not_ok;
        auto& symTableHdr = *estd::start_lifetime_as<ar_hdr>(address + SARMAG);
        constexpr std::string_view expectedName{"/               "};
        constexpr auto arNameSize = sizeof(symTableHdr.ar_name);
        static_assert(expectedName.size() == arNameSize);
        if (std::memcmp(symTableHdr.ar_name, expectedName.data(), arNameSize) != 0)
            return badFileError();

        // Get the size of the entry in bytes
        size_t symTableSize{0};
        auto ar_sizeEndPtr = symTableHdr.ar_size + sizeof(symTableHdr.ar_size);
        if (auto ec = std::from_chars(symTableHdr.ar_size, ar_sizeEndPtr, symTableSize).ec;
            ec != std::errc{}) return badFileError();
        if (memSize < (SARMAG + sizeof(ar_hdr) + symTableSize)) return badFileError();

        auto symTableFileOffset = SARMAG + sizeof(ar_hdr);
        auto symTablePtr = address + symTableFileOffset;
        struct ArchiveWord {
            std::array<uint8_t, 4> mem;
            operator uint32_t() const {
                return (uint32_t{mem[0]} << 24) | (uint32_t{mem[1]} << 16) | (uint32_t{mem[2]} << 8) | (uint32_t{mem[3]} << 0);
            }
        };
        uint32_t totalNumberOfSymbols = *estd::start_lifetime_as<ArchiveWord>(symTablePtr);

        if (totalNumberOfSymbols == 0) return badFileError();
        if (symTableSize < (totalNumberOfSymbols * sizeof(ArchiveWord) + sizeof(ArchiveWord))) return badFileError();

        symTablePtr += sizeof(ArchiveWord);
        auto memberOffsets = view_as_span<ArchiveWord>(symTablePtr, totalNumberOfSymbols);

        auto symStrTabSize = symTableSize - sizeof(ArchiveWord) * (totalNumberOfSymbols + 1);
        auto symStrTabPtr = estd::start_lifetime_as_array<char>(symTablePtr + memberOffsets.size_bytes(),
                                                                symStrTabSize);
        size_t symStrTabPtrOffset{0};
        uint32_t currMemberOffset = memberOffsets[0] - 1;

        for (uint32_t memberOffset : memberOffsets) {
            if (currMemberOffset != memberOffset) {
                currMemberOffset = memberOffset;

                archiveMemberSortKeys.push_back(makeSortKey(fileIndex, memberOffset));
                archiveMemberStates.push_back(ArchiveMemberState::lazy);
            }
            std::string_view currentSymbolName{symStrTabPtr + symStrTabPtrOffset};
            archiveSymbolTable[currentSymbolName].push_back(archiveMemberStates.size() - 1);
            symStrTabPtrOffset += currentSymbolName.size() + 1;
            if (symStrTabPtrOffset > symStrTabSize) return badFileError();
        }
    }
    return StatusCode::ok;
}

auto insertSymbolsIntoSymbolTable(parametersFor::InsertSymbolsIntoSymbolTable p) -> StatusCode {
    auto& [baseAddresses, sectionHeaders, elfSortKeys, startID] = p.in;
    auto& [symbolTable] = p.inout;
    auto& [searchedSymbolNames] = p.out;

    std::vector<readonly_span<Elf64_Sym>> symbols;
    std::vector<const char*> symbolStringTables;
    std::vector<size_t> elfIDs;

    constexpr size_t averageNumberOfSymbolsInChromePerFile{350}; // See https://github.com/rui314/mold/blob/main/docs/design.md
    symbols.reserve(baseAddresses.size() * averageNumberOfSymbolsInChromePerFile);
    symbolStringTables.reserve(baseAddresses.size());
    elfIDs.reserve(baseAddresses.size());

    StatusCode status{StatusCode::ok};
    for_each_indexed(sectionHeaders, [&](readonly_span<Elf64_Shdr> secHeaders, size_t elfID) {
        for (auto& secHdr : secHeaders) {
            if (secHdr.sh_type != SHT_SYMTAB) continue;
            if (secHdr.sh_entsize != sizeof(Elf64_Sym)) {
                status = report(StatusCode::bad_input_file, " object file #", elfID);
                return;
            }
            auto baseAddress = baseAddresses[elfID];
            auto& strTabHdr = secHeaders[secHdr.sh_link];
            symbols.push_back(view_as_span<Elf64_Sym>(baseAddress + secHdr.sh_offset, secHdr.sh_size / secHdr.sh_entsize));
            symbolStringTables.push_back(estd::start_lifetime_as_array<char>(baseAddress + strTabHdr.sh_offset, strTabHdr.sh_size));
            elfIDs.push_back(elfID + startID);
        }
    });

    if (status != StatusCode::ok) return status;

    auto isLocal = [](Elf64_Sym const& sym) {
        return ELF64_ST_BIND(sym.st_info) == STB_LOCAL;
    };

    auto isWeak = [](Elf64_Sym const& sym) {
        return ELF64_ST_BIND(sym.st_info) == STB_WEAK;
    };

    auto isGlobal = [](Elf64_Sym const& sym) {
        return ELF64_ST_BIND(sym.st_info) == STB_GLOBAL;
    };

    auto replaceIfAppropriate = [&](SymbolRef& entry, Elf64_Sym const& sym, size_t elfID) {
        if (!entry.symbol) {
            entry = {&sym, elfID};
        }
        bool entryIsWeak = isWeak(*entry.symbol);
        bool symIsWeak = isWeak(sym);

        if (entryIsWeak && !symIsWeak) {
            entry = {&sym, elfID};
        }
        if (entryIsWeak == symIsWeak && elfSortKeys[elfID] < elfSortKeys[entry.elfID]) {
            entry = {&sym, elfID};
        }
    };

    for_each_indexed(symbols, [&](readonly_span<Elf64_Sym> syms, size_t index) {
        auto elfID = elfIDs[index];
        for_each_indexed(syms, [&](Elf64_Sym const& sym, size_t symIndex) {
            if (symIndex == STN_UNDEF) return; // Skips the dummy symbol
            if (isLocal(sym)) return; // Don't insert any local symbols

            std::string_view name{symbolStringTables[index] + sym.st_name};
            if (sym.st_shndx == SHN_UNDEF) {
                searchedSymbolNames.push_back(name);
                // Symbol Search
                replaceIfAppropriate(symbolTable[name].firstSearch, sym, elfID);
                return /*early success*/;
            }
            // Symbol Definition
            auto& entry = symbolTable[name].firstLoad;
            if (entry.symbol && (isGlobal(sym) && isGlobal(*entry.symbol))) {
                status = report(StatusCode::symbol_redefined, name);
                return /*failure*/;
            }
            replaceIfAppropriate(entry, sym, elfID);
        });
    });

    return status;
}

auto determineArchiveMembersToExtract(parametersFor::DetermineArchiveMembersToExtract p) -> StatusCode {
    auto& [symbolTable, archiveSymbolTable, elfSortKeys, archiveMemberSortKeys, searchedSymbolNames] = p.in;
    auto& [archiveMemberIDs] = p.out;

    // Walking the entiry symbol table is not ideal.
    // So to optimize, symbols that had new insertions after a elf file has been parsed are cached.
    // While in the worst case, this would still mean walking effectively the entire map
    // but with a more random order and more hash calculations
    // it saves processing time in the more realistic case
    for (auto symName : searchedSymbolNames) {
        auto& elfRef = symbolTable.at(symName);
        auto archiveMembersIt = archiveSymbolTable.find(symName);
        if (archiveMembersIt == archiveSymbolTable.end()) continue;
        auto& archMemIDs = archiveMembersIt->second;
        if (archMemIDs.empty()) continue;

        auto firstSearchSortKey = elfSortKeys[elfRef.firstSearch.elfID];
        auto loadFromSearchIt = std::find_if(archMemIDs.begin(), archMemIDs.end(), [&](uint32_t memID) {
            return archiveMemberSortKeys[memID] > firstSearchSortKey;
        });

        if (loadFromSearchIt == archMemIDs.end()) {
            loadFromSearchIt = archMemIDs.begin();
        }
        auto archiveSortKey = archiveMemberSortKeys[*loadFromSearchIt];

        if (!elfRef.firstLoad.symbol) {
            archiveMemberIDs.push_back(*loadFromSearchIt);
            continue;
        }
        if (firstSearchSortKey < archiveSortKey && archiveSortKey < elfSortKeys[elfRef.firstLoad.elfID])
            return report(StatusCode::symbol_redefined, symName, " (loaded from file #", split(archiveSortKey).first, ')');
    }
    return StatusCode::ok;
}

auto extractArchiveMembers(parametersFor::ExtractArchiveMembers p) -> StatusCode {
    auto& [addresses, memSizes, archiveMemberSortKeys, archiveMemberIDsToExtract] = p.in;
    auto& [memberStates] = p.inout;
    auto& [elfAddresses, elfSortKeys, sectionHeaders, sectionStringTables, extractionMemory] = p.out;

    for (auto memberID : archiveMemberIDsToExtract) {
        if (memberStates[memberID] == ArchiveMemberState::loaded) continue;
        memberStates[memberID] = ArchiveMemberState::loaded;

        auto [sourceFileIndex, offset] = split(archiveMemberSortKeys[memberID]);
        auto sourceAddress = static_cast<std::byte*>(addresses[sourceFileIndex]);
        auto memSize = memSizes[sourceFileIndex];
        if (memSize < (offset + sizeof(ar_hdr))) return report(StatusCode::bad_input_file, "Archive File to small");

        auto& arHdr = *estd::start_lifetime_as<ar_hdr>(sourceAddress + offset);
        size_t elfFileSize{0};
        if (auto ec = std::from_chars(arHdr.ar_size, arHdr.ar_size + sizeof(arHdr.ar_size), elfFileSize).ec;
            ec != std::errc{}) return StatusCode::not_ok;

        auto fileStartOffset = offset + sizeof(arHdr);
        fileStartOffset += fileStartOffset % 2;

        std::byte* elfAddress{nullptr};
        if (fileStartOffset % alignof(Elf64_Ehdr) == 0) {
            elfAddress = sourceAddress + fileStartOffset;
        } else {
            elfAddress = static_cast<std::byte*>(extractionMemory.allocate(elfFileSize));
            std::memcpy(elfAddress, sourceAddress + fileStartOffset, elfFileSize);
        }
        if (std::memcmp(elfAddress, elfIdent.data(), elfIdent.size()) != 0) return StatusCode::not_ok;

        readonly_span<Elf64_Shdr> secHeaders{};
        char* strTable{};
        if (auto status = initRelaElf({.in{elfAddress, elfFileSize}, .out{secHeaders, strTable}});
            status != StatusCode::ok) {
            return status;
        }

        elfSortKeys.push_back(archiveMemberSortKeys[memberID]);
        elfAddresses.push_back(elfAddress);
        sectionHeaders.push_back(secHeaders);
        sectionStringTables.push_back(strTable);
    }
    return StatusCode::ok;
}

} // namespace cppld