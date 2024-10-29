if (NOT UNIX)
   message(FATAL_ERROR "Unsupported OS")
endif ()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=native")

include(EnableAddressSanitizer)
include(EnableUndefinedSanitizer)
include(clang-tidy)
include(BundledGTest)

add_custom_target(lint)
