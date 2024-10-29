#pragma once

#include <memory_resource>
#include <string_view>
#include <vector>

#include "cppld_api_types.hpp"

namespace cppld {

/**
 * @brief User specific options for the linker
 * 
 */
struct LinkerOptions {
    std::string_view outputFileName = "a.out";
    std::string_view entrySymbolName = "_start";
    bool createEhFrameHeader = false;
};

/**
 * @brief Provide raw memory access for files in a platform specific way
 * Automatically releases the memory the correct way once out of scope 
 */
struct MemoryMappings {
    std::vector<void*> addresses;
    std::vector<size_t> memSizes;
    ~MemoryMappings();
};

//
namespace parametersFor {
struct ArgumentsToLinkerParameters;
struct FilePathsToMemoryMappings;
struct LinkSourcesToExecutableElfFile;
} // namespace parametersFor

/**
 * @brief Parses program arguments
 * 
 * @return Options for the linker, the paths to the input file
 * May allocate memory for paths strings for libraries that are not specified explicitly but
 * Freeing this memory is 
 */
auto argumentsToLinkerParameters(parametersFor::ArgumentsToLinkerParameters) -> StatusCode;
struct parametersFor::ArgumentsToLinkerParameters {
    struct {
        readonly_span<char*> relevantArguments;
    } in;
    struct {
        out<LinkerOptions> linkerOptions;
        out<std::vector<std::string_view>> inputFilePaths;
        out<std::pmr::memory_resource> libraryPathMemory;
    } out;
};

/**
 * @brief Maps Files into memory in a platform specific way
 * 
 * @return Memory mappings 
 */
auto filePathsToMemoryMappings(parametersFor::FilePathsToMemoryMappings) -> StatusCode;
struct parametersFor::FilePathsToMemoryMappings {
    struct {
        readonly_span<std::string_view> filePaths;
    } in;
    struct {
        out<MemoryMappings> memoryMappings;
    } out;
};

auto linkSourcesToExecutableElfFile(parametersFor::LinkSourcesToExecutableElfFile) -> StatusCode;
struct parametersFor::LinkSourcesToExecutableElfFile {
    struct {
        readonly_span<void*> sourceAddresses;
        readonly_span<size_t> sourceMemorySizes;
        in<LinkerOptions> options;
    } in;
};

} // namespace cppld