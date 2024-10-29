#pragma once
#include "cppld_api_types.hpp"
#include "elf.h"
#include <cstdint>

#include "reference_types.hpp"
#include <memory_resource>
#include <unordered_map>
namespace cppld {

namespace parametersFor {
struct ParseInputAndCreateSymbolTable;
// Substeps
struct ClassifyInput;
struct ParseElfFiles;
struct ParseArchiveMembers;
// These steps are repeated until no more archives are extracted
struct InsertSymbolsIntoSymbolTable;
struct DetermineArchiveMembersToExtract;
struct ExtractArchiveMembers;
// End loop

}; // namespace parametersFor

/**
 * @brief The first phase of the linking step is to parsethe input, extract members from archives and build a symbol table while doing so
 * 
 * Archive members may need additional memory when extracing them, since they might not be properly aligned inside the archive file
 * Managing the memory is the callers job.
 * If a stateless allocator is used, the additional memory can be identified by virtue of the pointer not being equal to a source addresse
 * 
 * SortKeys are returned since they are potentially needed by section merging later
 */
auto parseInputAndCreateSymbolTable(parametersFor::ParseInputAndCreateSymbolTable) -> StatusCode;
struct parametersFor::ParseInputAndCreateSymbolTable {
    struct {
        readonly_span<void*> addresses;
        readonly_span<size_t> memSizes;
    } in;
    struct {
        out<std::vector<std::byte*>> elfAddresses;
        out<std::vector<SortKey>> sortKeys;
        out<std::vector<readonly_span<Elf64_Shdr>>> sectionHeaders;
        out<std::vector<const char*>> sectionStringTables;
        out<std::pmr::memory_resource> archiveExtractionMemory;
        out<SymbolTable> symbolTable;
    } out;
};

// Archive Members are either lazy or loaded
// On one pass of extraction, loading might be requested several times so a state flag is used to keep track if the states
// No boolean since std::vector<bool> can't easily be accessed concurrently
enum class ArchiveMemberState : uint8_t {
    lazy = 0,
    loaded = 1
};

// Using a pmr map to have the potential for more local memory management
using ArchiveSymbolTable = std::pmr::unordered_map<std::string_view, std::pmr::vector<size_t>>;

/**
 * @brief The initial bytes of the input are checked to differentiate archive files from object files
 * The two can then be handled seperately
 * 
 */
auto classifyInput(parametersFor::ClassifyInput) -> StatusCode;
struct parametersFor::ClassifyInput {
    struct {
        readonly_span<void*> addresses;
        readonly_span<size_t> memSizes{};
    } in;
    struct {
        out<std::vector<uint32_t>> elfFileIndices;
        out<std::vector<uint32_t>> archiveFileIndices;
    } out;
};

/**
 * @brief Elf Files are parsed based on the fileIndices returned by classifyInput()
 * Only relocatable elf files are accepted
 * 
 */
auto parseElfFiles(parametersFor::ParseElfFiles) -> StatusCode;
struct parametersFor::ParseElfFiles {
    struct {
        readonly_span<void*> addresses;
        readonly_span<size_t> memSizes;
        readonly_span<uint32_t> elfFileIndices;
    } in;
    struct {
        out<std::vector<std::byte*>> elfAddresses;
        out<std::vector<SortKey>> sortKeys;
        out<std::vector<readonly_span<Elf64_Shdr>>> sectionHeaders;
        out<std::vector<const char*>> sectionStringTables;
    } out;
};

/**
 * @brief Archive Files are parsed to based on the fileIndices returned by classifyInput()
 * 
 * A special Symbol Table for Archive files is built, too. 
 * It is later used to determine which archive file should be extracted
 * 
 */
auto parseArchiveMembers(parametersFor::ParseArchiveMembers) -> StatusCode;
struct parametersFor::ParseArchiveMembers {
    struct {
        readonly_span<void*> addresses;
        readonly_span<size_t> memSizes;
        readonly_span<uint32_t> archiveFileIndices;
    } in;
    struct {
        out<std::vector<SortKey>> archiveMemberSortKeys;
        out<std::vector<ArchiveMemberState>> archiveMemberStates;
        out<ArchiveSymbolTable> archiveSymbolTable;
    } out;
};

/**
 * @brief Function to insert the global symbols of elf files into the global symbol table
 * 
 * Since this function is called several times with partial results, an offset parameter is given to insert the correct IDs
 */
auto insertSymbolsIntoSymbolTable(parametersFor::InsertSymbolsIntoSymbolTable) -> StatusCode;
struct parametersFor::InsertSymbolsIntoSymbolTable {
    struct {
        readonly_span<std::byte*> elfAddresses;
        readonly_span<readonly_span<Elf64_Shdr>> sectionHeaders;
        out<std::vector<SortKey>> sortKeys;
        size_t elfIDOffset;
    } in;
    struct {
        inout<SymbolTable> symbolTable;
    } inout;
    struct {
        out<std::vector<std::string_view>> searchedSymbolNames;
    } out;
};

/**
 * @brief Walk the Symbol Table to find global symbols that are searched and can be found in an archive file
 * 
 * An error occurs If an archive member would provide a symbol for an already loaded global symbol
 * This only occurs if an archive member is ordered between the first search of a symbol and the first load of a symbol in which case the archive member would have been loaded first
 * 
 */

auto determineArchiveMembersToExtract(parametersFor::DetermineArchiveMembersToExtract) -> StatusCode;
struct parametersFor::DetermineArchiveMembersToExtract {
    struct {
        in<SymbolTable> symbolTable;
        in<ArchiveSymbolTable> archiveSymbolTable;
        readonly_span<SortKey> elfSortKeys;
        readonly_span<SortKey> archiveMemberSortKeys;
        readonly_span<std::string_view> searchedSymbolNames;
    } in;
    struct {
        out<std::vector<size_t>> archiveMemberIDsToExtract;
    } out;
};

/**
 * @brief Based on the results of determineArchiveMembersToExtract(), parse the archive member as an elf file and add it to the input
 * 
 * This updates the archive member states from lazy to loaded
 * 
 * A memory allocator for placing the extracted elf files in poperly aligned memory needs to be provided in case they reside unaligned in the archive file
 * 
 */
auto extractArchiveMembers(parametersFor::ExtractArchiveMembers) -> StatusCode;
struct parametersFor::ExtractArchiveMembers {
    struct {
        readonly_span<void*> addresses;
        readonly_span<size_t> memSizes;
        readonly_span<SortKey> archiveMemberSortKeys;
        readonly_span<size_t> archiveMemberIDsToExtract;
    } in;
    struct {
        inout<std::vector<ArchiveMemberState>> archiveMemberStates;
    } inout;
    struct {
        out<std::vector<std::byte*>> addresses;
        out<std::vector<SortKey>> sortKeys;
        out<std::vector<readonly_span<Elf64_Shdr>>> sectionHeaders;
        out<std::vector<const char*>> sectionStringTables;
        out<std::pmr::memory_resource> extractionMemory;
    } out;
};

} // namespace cppld