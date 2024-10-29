# We need compile commands for clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "" FORCE)

# We need clang-tidy itself
find_package(ClangTidy 14 REQUIRED)

# Add target to lint the given source files
function(add_clang_tidy_target TARGET)
    set(FILES ${ARGN})

    if (NOT FILES)
        add_custom_target(${TARGET})
        return()
    endif ()

    # Remove duplicates & sort
    list(REMOVE_DUPLICATES FILES)
    list(SORT FILES)
    list(JOIN FILES " " FILES_PRETTY)

    message(STATUS "LINT ${TARGET}: ${FILES_PRETTY}")

    # Add target
    add_custom_target(${TARGET}
            COMMAND ${CMAKE_COMMAND} -E chdir
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${ClangTidy_EXECUTABLE}
            "-quiet"
            "-p=${CMAKE_CURRENT_BINARY_DIR}"
            ${FILES}
            COMMENT "Running ${TARGET}"
            VERBATIM)
endfunction()
