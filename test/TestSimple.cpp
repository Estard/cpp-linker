#include <cstdlib>
#include <gtest/gtest.h>

TEST(Simple, StartAndExit) {
    std::ignore = std::system("echo '.global _start; .section .text; _start: call exit' | as -o a.o");
    std::ignore = std::system("echo '.global exit; .section .text; exit: mov $60,%eax; syscall' | as -o b.o");
    ASSERT_EQ(std::system("./../src/ld a.o b.o && ./a.out"), 0);
}

TEST(Simple, GlobalSymbolUndefined) {
    std::ignore = std::system("echo '.global _start; .extern sym; .section .text; _start: movabsq $sym, %rax; setz %dil; mov $60,%eax; syscall' | as -o a.o");
    ASSERT_EQ(std::system("! ./../src/ld a.o"), 0);
}

TEST(Simple, WeakSymbolUndefined) {
    std::ignore = std::system("echo '.global _start; .weak weaksym; .section .text; _start: movabsq $weaksym, %rax; setz %dil; mov $60,%eax; syscall' | as -o a.o");
    ASSERT_EQ(std::system("./../src/ld a.o && ./a.out"), 0);
}

TEST(Simple, WeakSymbolSingleWeakDefinition) {
    std::ignore = std::system("echo '.global _start; .weak weaksym; .section .text; _start: movq (weaksym), %rax; cmp $1, %rax; setne %dil; mov $60,%eax; syscall' | as -o a.o");
    std::ignore = std::system("echo '.weak weaksym; .section .rodata; weaksym: .8byte 1' | as -o b.o");
    ASSERT_EQ(std::system("ld a.o b.o && ./a.out"), 0);
}

TEST(Simple, WeakSymbolOverridingGlobalDefinition) {
    std::ignore = std::system("echo '.global _start; .weak weaksym; .section .text; _start: movq (weaksym), %rax; cmp $2, %rax; setne %dil; mov $60,%eax; syscall' | as -o a.o");
    std::ignore = std::system("echo '.weak weaksym; .section .rodata; weaksym: .8byte 1' | as -o b.o");
    std::ignore = std::system("echo '.global weaksym; .section .rodata; weaksym: .8byte 2' | as -o c.o");
    ASSERT_EQ(std::system("./../src/ld a.o b.o c.o && ./a.out"), 0);
    ASSERT_EQ(std::system("./../src/ld a.o c.o b.o && ./a.out"), 0);
    ASSERT_EQ(std::system("./../src/ld a.o c.o && ./a.out"), 0);
    ASSERT_EQ(std::system("./../src/ld a.o b.o && ! ./a.out"), 0);
}

TEST(Simple, MultipleWeakSymbolDefinitions) {
    std::ignore = std::system("echo '.global _start; .section .text; _start: movq (weaksym), %rax; cmp $2, %rax; setne %dil; mov $60,%eax; syscall' | as -o a.o");
    std::ignore = std::system("echo '.weak weaksym; .section .rodata; weaksym: .8byte 1' | as -o b.o");
    std::ignore = std::system("echo '.weak weaksym; .section .rodata; weaksym: .8byte 2' | as -o c.o");
    ASSERT_EQ(std::system("./../src/ld a.o b.o c.o && ! ./a.out # first weak symbol wins, value 1 != 2"), 0);
    ASSERT_EQ(std::system("./../src/ld a.o c.o b.o && ./a.out # first weak symbol wins, value 2 == 2"), 0);
    ASSERT_EQ(std::system("./../src/ld a.o b.o && ! ./a.out"), 0);
    ASSERT_EQ(std::system("./../src/ld a.o c.o && ./a.out"), 0);
    ASSERT_EQ(std::system("! ./../src/ld a.o # weaksym undefined"), 0);
}

TEST(Simple, SectionRenamingMerging) {
    std::ignore = std::system("echo '.global _start; .section .text.xxx; _start: mov $60,%eax; syscall' | as -o a.o");
    ASSERT_EQ(std::system("./../src/ld a.o"), 0);
    ASSERT_EQ(std::system("! readelf -SW a.out | grep -E ' \\.text[^ ]'"), 0); // Should not find a .text segment with something extra
    ASSERT_EQ(std::system("[ $(readelf -SW a.o | grep ' \\.text' | wc -l) = 2 ]"), 0); // Should find two .text segments
    ASSERT_EQ(std::system("[ $(readelf -SW a.out | grep ' \\.text' | wc -l) = 1 ]"), 0); // Should find only one .text segment
}

TEST(Simple, ExtractionOrderOfArchives) {
    std::ignore = std::system("echo '.global _start; .section .text; _start: call exit' | as -o a.o");
    std::ignore = std::system("echo '.global exit; .section .text; exit: mov $60,%eax; syscall' | as -o b.o");
    std::ignore = std::system("echo '.global exit; .section .text; exit: mov $1,%edi; mov $60,%eax; syscall' | as -o c.o");
    std::ignore = std::system("ar rc b.a b.o");
    ASSERT_EQ(std::system("./../src/ld a.o b.a && ./a.out"), 0);
    ASSERT_EQ(std::system("! ./../src/ld a.o b.a c.o # duplicate definition of exit; b.o is extracted from b.a first"), 0);
    ASSERT_EQ(std::system("./../src/ld a.o c.o b.a && ! ./a.out # b.o is not extracted from b.a, exit already defined"), 0);
}
