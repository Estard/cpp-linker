#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <gtest/gtest.h>


TEST(Simple, Reject_EH_Frame_Hdr) {
    std::ignore = std::system("echo '.global _start; .section .text; _start: call exit' | as -o a.o");
    std::ignore = std::system("echo '.global exit; .section .text; exit: mov $60,%eax; syscall' | as -o b.o");
    ASSERT_EQ(std::system("! ./../src/ld a.o b.o --eh-frame-hdr"), 0);
}

constexpr size_t MAX_NUM_SECTIONS{65280};
constexpr size_t NUM_ALWAYS_GENERATED_SECTIONS{8};
constexpr size_t NUM_ADDABLE_SECTIONS = MAX_NUM_SECTIONS - NUM_ALWAYS_GENERATED_SECTIONS;

TEST(Unit, ObjectFilesManySections) {
    auto tmpFileName = std::tmpnam(nullptr);
    std::ofstream asmFile(tmpFileName, std::ios::trunc | std::ios::binary);
    asmFile << ".global _start; .section .text; _start: mov $60,%eax; syscall;\n";
    for (size_t i = 0; i < NUM_ADDABLE_SECTIONS; ++i) {
        asmFile << ".section sec" << (i + 4) << "; _" << i << ": mov $60,%eax; syscall;\n";
    }
    asmFile.flush();
    std::stringstream ss;
    ss << "as -o ObjectFilesManySections.o " << tmpFileName;
    auto cmd = ss.str();
    auto res = std::system(cmd.c_str());
    if (res != 0) {
        std::cerr << "Test invalid: Object File Creation failed\n";
        return;
    }
    ASSERT_EQ(std::system(" ./../src/ld ObjectFilesManySections.o"), 0);
}

TEST(Unit, ObjectFilesWithTooManySections) {
    auto tmpFileName = std::tmpnam(nullptr);
    std::ofstream asmFile(tmpFileName, std::ios::trunc | std::ios::binary);
    asmFile << ".global _start; .section .text; _start: mov $60,%eax; syscall;\n";
    for (size_t i = 0; i < NUM_ADDABLE_SECTIONS + 1; ++i) {
        asmFile << ".section sec" << (i + 4) << "; _" << i << ": mov $60,%eax; syscall;\n";
    }
    asmFile.flush();
    std::stringstream ss;
    ss << "as -o ObjectFilesWithTooManySections.o " << tmpFileName;
    auto cmd = ss.str();
    auto res = std::system(cmd.c_str());
    if (res != 0) {
        std::cerr << "Test invalid: Object File Creation failed\n";
        return;
    }
    ASSERT_EQ(std::system("! ./../src/ld ObjectFilesWithTooManySections.o"), 0);
}

TEST(Unit, ExecutableWithTooManySections) {
    for (size_t n = 0; n < 2; n++) {
        auto tmpFileName = std::tmpnam(nullptr);
        std::ofstream asmFile(tmpFileName, std::ios::trunc | std::ios::binary);
        if (n == 0)
            asmFile << ".global _start;";
        asmFile << " .section .text; _start: mov $60,%eax; syscall;\n";
        for (size_t i = 0; i < NUM_ADDABLE_SECTIONS / 2 + NUM_ALWAYS_GENERATED_SECTIONS; ++i) {
            asmFile << ".section sec" << (i + n * (NUM_ADDABLE_SECTIONS / 2)) << "; _" << i << ": mov $60,%eax; syscall;\n";
        }
        asmFile.flush();
        std::stringstream ss;
        ss << "as -o ManyButNotThatManySections_" << n << ".o " << tmpFileName;
        auto cmd = ss.str();
        auto res = std::system(cmd.c_str());
        if (res != 0) {
            std::cerr << "Test invalid: Object File Creation failed\n";
            return;
        }
    }
    ASSERT_EQ(std::system("! ./../src/ld ManyButNotThatManySections_0.o ManyButNotThatManySections_1.o"), 0);
}

TEST(Unit, TLS_SegmentGetsGenerated) {
    std::ignore = std::system("echo '.global _start; .global _1; .section .text; _start: mov $60,%eax; syscall;"
                              ".section .tdata,\"awT\"; _1: mov $60,%eax; syscall; ' | as -o tls.o");
    ASSERT_EQ(std::system("./../src/ld tls.o"), 0);
    ASSERT_EQ(std::system("[ $(readelf -lW a.out | grep ' \\TLS' | wc -l) = 1 ]"), 0);
}

TEST(Unit, String_Merge) {
    std::ignore = std::system("echo '.global _start; .weak _1; .section .text; _start: mov $60,%eax; syscall; "
                              " .section sdata,\"awSM\",1; _1: .string \"I am the one and only\";"
                              " _2: .string \"I am the other one\" ;' | as -o strings_1.o");
    std::ignore = std::system("echo '.weak _1; .section .text; _start: mov $60,%eax; syscall; "
                              " .section sdata,\"awSM\",1; _1: .string \"I am the one and only\";"
                              " _2: .string \"I am the other yet another one\" ;' | as -o strings_2.o");
    ASSERT_EQ(std::system("./../src/ld strings_1.o strings_2.o"), 0);
    ASSERT_EQ(std::system("[ $(strings a.out | grep 'I am the one and only' | wc -l) = 1 ]"), 0);
    ASSERT_EQ(std::system("[ $(strings a.out | grep 'I am' | wc -l) = 3 ]"), 0);
}

TEST(Unit, FixedSize_Merge) {
    std::ignore = std::system(" echo '.global _start; .global _1; .section .text; _start: mov $60,%eax; syscall;"
                              "  .section sdata,\"awM\",4; _1: .4byte 0x41414141, 0x42424242,0x43434343,0x00444444;"
                              " _2: .4byte 0x42424242,0x45454545,0x43434343,0x00464646 ;' | as -o fixed_size.o");
    ASSERT_EQ(std::system("./../src/ld fixed_size.o"), 0);

     ASSERT_EQ(std::system("[ $(strings fixed_size.o | grep 'BBBB' | wc -l) = 2 ]"), 0);
    ASSERT_EQ(std::system("[ $(strings fixed_size.o | grep 'CCCC' | wc -l) = 2 ]"), 0);

    ASSERT_EQ(std::system("[ $(strings a.out | grep 'BBBB' | wc -l) = 1 ]"), 0);
    ASSERT_EQ(std::system("[ $(strings a.out | grep 'CCCC' | wc -l) = 1 ]"), 0);

    ASSERT_EQ(std::system("[ $(strings a.out | grep 'AAAA' | wc -l) = 1 ]"), 0);
    ASSERT_EQ(std::system("[ $(strings a.out | grep 'DDD' | wc -l) = 1 ]"), 0);
    ASSERT_EQ(std::system("[ $(strings a.out | grep 'EEEE' | wc -l) = 1 ]"), 0);
    ASSERT_EQ(std::system("[ $(strings a.out | grep 'FFF' | wc -l) = 1 ]"), 0);
}