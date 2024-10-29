#include "cppld.hpp"
#include <iostream>

int main(int argc, char** argv) {
    cppld::StatusCode status{cppld::StatusCode::ok};

    cppld::LinkerOptions linkerOptions;
    cppld::MemoryMappings filemappings;
    {
        std::vector<std::string_view> inputFilePaths;
        std::pmr::monotonic_buffer_resource libraryFilePathStringMemory{std::pmr::get_default_resource()};
        status = cppld::argumentsToLinkerParameters({.in{{argv + 1, argv + argc}},
                                                     .out{linkerOptions, inputFilePaths, libraryFilePathStringMemory}});
        if (status != cppld::StatusCode::ok) {
            std::cerr << "Argument Parsing failed\n";
            return -1;
        }

        status = cppld::filePathsToMemoryMappings({.in{inputFilePaths}, .out{filemappings}});
        if (status != cppld::StatusCode::ok) {
            std::cerr << "Loading Input Files Failed\n";
            return -1;
        }
    }

    status = cppld::linkSourcesToExecutableElfFile({.in{filemappings.addresses, filemappings.memSizes, linkerOptions}});
    if (status != cppld::StatusCode::ok) {
        std::cerr << "Linking Failed\n";
        return -1;
    }

    return 0;
}