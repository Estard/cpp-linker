set(CPPLD_SOURCES
    argumentsToLinkerParameters.cpp
    filePathsToMemoyMappings.cpp
    linkSourcesToExecutableElfFile.cpp
    parseInputAndCreateSymbolTable.cpp
    mapInputSectionsToOutputSections.cpp
    writeLinkingResultsToFile.cpp
)

add_library(cppld ${CPPLD_SOURCES})
target_include_directories(cppld PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_clang_tidy_target(lint_ld ${CPPLD_SOURCES})
add_dependencies(lint lint_ld)