include(FindPackageHandleStandardArgs)

# Most recent clang-tidy version known to this script
set(CLANG_TIDY_MOST_RECENT_VERSION 13)

# Enumerate candidate executable names (we start with clang-tidy to prefer locally-built clang-tidy over provided clang-tidy)
list(APPEND CLANG_TIDY_EXECUTABLE_CANDIDATES clang-tidy)
foreach (CANDIDATE_VERSION RANGE ${${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION_MAJOR} ${CLANG_TIDY_MOST_RECENT_VERSION})
    list(APPEND CLANG_TIDY_EXECUTABLE_CANDIDATES clang-tidy-${CANDIDATE_VERSION})
endforeach ()

# Iterate over candidate executable names and determine whether they are present and match version requirements
foreach (CLANG_TIDY_EXECUTABLE_NAME ${CLANG_TIDY_EXECUTABLE_CANDIDATES})
    find_program(${CLANG_TIDY_EXECUTABLE_NAME}_EXECUTABLE ${CLANG_TIDY_EXECUTABLE_NAME})
    if (NOT ${CLANG_TIDY_EXECUTABLE_NAME}_EXECUTABLE STREQUAL "${CLANG_TIDY_EXECUTABLE_NAME}_EXECUTABLE-NOTFOUND")
        # Parse the executable version
        execute_process(
                COMMAND ${${CLANG_TIDY_EXECUTABLE_NAME}_EXECUTABLE} --version
                OUTPUT_VARIABLE CLANG_TIDY_RAW_VERSION
        )

        string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" CLANG_TIDY_VERSION ${CLANG_TIDY_RAW_VERSION})

        # Always extract executable information, so that CMake can provide more informative messages
        set(ClangTidy_EXECUTABLE ${${CLANG_TIDY_EXECUTABLE_NAME}_EXECUTABLE})
        set(ClangTidy_VERSION ${CLANG_TIDY_VERSION})

        string(REGEX REPLACE "([0-9]+)\\.[0-9]+\\.[0-9]+" "\\1" ClangTidy_VERSION_MAJOR ${CLANG_TIDY_VERSION})
        string(REGEX REPLACE "[0-9]+\\.([0-9]+)\\.[0-9]+" "\\1" ClangTidy_VERSION_MINOR ${CLANG_TIDY_VERSION})
        string(REGEX REPLACE "[0-9]+\\.[0-9]+\\.([0-9]+)" "\\1" ClangTidy_VERSION_PATCH ${CLANG_TIDY_VERSION})

        # Check if the candidate matches the requested version. This would be much more convenient if we were able to
        # require at least cmake version 3.19 which provides the find_package_check_version function. However, even
        # on a recent Ubuntu cmake is still rather old at 3.16.
        if (${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION_EXACT)
            # If an exact match is requested we have to check which parts of the version are actually specified
            set(ClangTidy_FOUND TRUE)
            if (${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION_MAJOR AND NOT ClangTidy_VERSION_MAJOR EQUAL ${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION_MAJOR)
                set(ClangTidy_FOUND FALSE)
            endif ()
            if (${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION_MINOR AND NOT ClangTidy_VERSION_MINOR EQUAL ${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION_MINOR)
                set(ClangTidy_FOUND FALSE)
            endif ()
            if (${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION_PATCH AND NOT ClangTidy_VERSION_PATCH EQUAL ${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION_PATCH)
                set(ClangTidy_FOUND FALSE)
            endif ()
        else ()
            # If only a lower bound is specified we can simply compare
            if (ClangTidy_VERSION VERSION_GREATER_EQUAL ${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION)
                # We have found a suitable version
                set(ClangTidy_FOUND TRUE)
            endif ()
        endif ()
    endif ()

    mark_as_advanced(${CLANG_TIDY_EXECUTABLE_NAME}_EXECUTABLE)

    if (ClangTidy_FOUND)
        break()
    endif ()
endforeach ()

find_package_handle_standard_args(ClangTidy
        FOUND_VAR ClangTidy_FOUND
        REQUIRED_VARS ClangTidy_EXECUTABLE
        VERSION_VAR ClangTidy_VERSION
        )