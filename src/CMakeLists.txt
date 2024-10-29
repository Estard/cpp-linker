
add_subdirectory(lib)

add_executable(ld main.cpp)
target_link_libraries(ld cppld)
