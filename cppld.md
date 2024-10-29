# cppld

Create a fork of the Git repository at https://gitlab.db.in.tum.de/cpplab23/final. The final project is due on **2023-09-17 at 23:59 CEST**. Do not forget to `git push` your solution before the deadline expires.

Once the deadline for this sheet expires, a signed tag will be created in your fork automatically. Your solutions will be graded based on the state of your repository at this tag. **Do not attempt to modify or remove this tag, as we cannot grade your solution otherwise.**

## Task

Implement a linker that links reloctable object files( `*.o`) and non-thin static archives of relocatable object files (`*.a`) into a single, statically linked, position-dependent ELF executable for x86-64 Linux; reasonably optimize for performance including multi-threading.

The linked executable should run on modern Linux systems and be comparable with standard linkers (e.g., not just using RWX pages just because it is easier). Compare your output and linking performance with other linkers like GNU ld and lld and document structural differences and performance results in the README file of your repository.

In particular, you need to implement the following features: section merging (`SHF_MERGE`, also with `SHF_STRINGS`), weak symbols, lazy extraction of objects from archives. Your output file should appropriately include program headers, section headers, and a symbol table. For thread-local storage, you might need a global offset table.

You *do not* need to implement any of the following features (error out when attempted): comdat groups (`SHF_GROUP` with `GRP_COMDAT`), `.eh_frame_hdr` generation, common symbols, `STT_GNU_IFUNC` symbols, linking against shared libraries, generating position-independent executables, generating dynamic relocations, ELF interpreters, object or executable files with more than 65280 sections, build IDs other than `none`, stack splitting, identical code folding, section-based garbage collection, linker plugins (ignore), compressed sections, linker relaxations (Appendix B of x86-64 psABI), and linker scripts.

You might find the following specifications useful:

- [System V Application Binary Interface – Edition 4.1 – 18 March 1997](http://www.sco.com/developers/devspecs/gabi41.pdf), Chapter 7.2 (specifies the archive file format).
- [System V Application Binary Interface - DRAFT - 10 June 2013](https://www.sco.com/developers/gabi/latest/contents.html), Chapters 4, 5.1, and 5.2 (specifies ELF object files and program headers of executables).
- [System V Application Binary Interface – Linux Extensions](https://gitlab.com/x86-psABIs/Linux-ABI/-/wikis/uploads/9eca2f2defe62b0c5015bf2e3e8a9f05/Linux-gABI-1_needed-2021-06-18.pdf), Chapters 2 and 3 (specifies EH Frame sections, GNU properties, and related program headers).
- [System V Application Binary Interface – AMD64 Architecture Processor Supplement](https://gitlab.com/x86-psABIs/x86-64-ABI/-/wikis/uploads/24dcc4171240a412b10de4f5d1996e10/x86-64-psABI-2023-05-30.pdf), Chapters 4.1, 4.4, and 5.1 (specifies ELF header information, relocations, and binary layout specifics for x86-64).

The book "Linkers & Loaders" by John Levine (1999) ([online PDF](https://wh0rd.org/books/linkers-and-loaders/linkers_and_loaders.pdf), our university library also has a copy in Garching) gives a more gentle introduction, but also includes a lot of material not relevant for this task and is partly outdated.

## Non-functional Requirements
In addition to the linker, write tests to ensure that your linker behaves as expected, even in corner cases (compare with the behavior of GNU ld or ask on Mattermost if unsure). You can use the tests below as inspiration. Be reminded that input files can be corrupt or even adversarial. Make use of CI to run these tests.

Document your code concisely and structure your code appropriately. Use appropriate C++ constructs as discussed in the lectures/exercises. Do not use any other libraries except for the C/C++ standard library (including headers like `elf.h`); for tests you may (but don't have to) use gtest.

## Command Line Interface

Your executable shall be named `ld` and operate in a way compatible with GNU ld, see [`man ld`](https://linux.die.net/man/1/ld). Invocations of your linker from GCC should work, so you need to implement the following options:

- `-o <output>`/`--output=<output>` – set the output file name, default to `a.out`
- `-e <symbol>`/`--entry=<symbol>` – set entry symbol, default to `_start`.
- `<file>` – link against `<file>`.
- `-l <namespec>`/`--library=<namespec>` – if shared linking is enabled, try linking against `lib<namespec>.so`, if this fails or otherwise, link against `lib<namespec>.a`; search the library path in the order specified on the command line.
- `-L <path>`/`--library-path=<path>` – add `<path>` to the library search path. All `-L` options apply to all `-l` options.
- `-Bstatic`/`-dn`/`-non_shared`/`-static` – disable linking against shared libraries *for subsequently following `-l` options*.
- `-Bdynamic`/`-dy`/`-call_shared` – enable linking against shared libraries *for subsequently following `-l` options*.
- `--push-state`/`--pop-state` – store/restore the state of `-Bstatic`/`-Bdynamic` to/from an internal stack.
- `--eh-frame-hdr`/`--no-eh-frame-hdr` – enable/disable generation of `.eh_frame_hdr` section (default to no).
- `--build-id`/`--build-id=none` – check that only the supported build id type (`none`) is used, ignore otherwise.
- `-z <keyword>`
    - `now` – ignore.
    - `noexecstack` – ignore. (You should never generate executables with executable stack.)
    - You do not need to support other keywords (error when unsupported).
- `--start-group`/`--end-group` – ignore. (We relax the GNU semantics and treat all specified options as if they were in a group. The order of arguments does not matter with regard to back references now.)
- `--plugin`/`--plugin-opt` – ignore.
- `--add-needed`/`--no-add-needed` – ignore.
- `--as-needed`/`--no-as-needed` – ignore.
- `--dynamic-linker=<file>`/`--no-dynamic-linker` – ignore.
- `-nostdlib` – ignore. Do not add default library paths in any case.
- `-m` – ignore.
- `--hash-style` – ignore.

## Linking Steps

A linker roughly follows the following steps. You are, of course, allowed to interleave or change the order in your implementation, provided that correctness is maintained.

1. Parse program arguments.
1. Resolve and process input files in order.
    - Objects from an archive file are only extracted when they are required, typically, such objects and their symbols are marked as "lazy" until they are needed.
    - Due to weak symbols and lazy archive extraction, linking of object files and/or static libraries is not commutative. Thus, all input files must be processed in their specified order.
1. Resolve symbols by building a symbol table.
    - There may be at most one global definition of a symbol.
    - Weak symbols may have multiple definition or none at all; if there is a global definition, it takes priority, otherwise the first definition is used.
    - We treat all files as a single group, so there might be undefined forward references, that must be satisfied at the end.
1. Merge sections into sections for the output file.
    - Input sections of type `SHT_NULL`, `SHT_STRTAB`, `SHT_SYMTAB`, `SHT_GROUP`, `SHT_REL`, and `SHT_RELA` never reach the output. The new symbol table and string table are synthesized for the resulting binary when writing.
    - Sections of the same name are merged, keep merged sections in the order of the input files (except for `SHF_MERGE` sections).
    - If a section starts with one of the following names, the remainder of the name is discarded when determining the output section name: `.text`, `.data.rel.ro`, `.data`, `.ldata`, `.rodata`, `.lrodata`, `.bss.rel.ro`, `.bss`, `.lbss`, `.init_array`, `.fini_array`, `.tbss`, `.tdata`
    - The specification indicates that only sections of the same name/type/flags should be merged, but this is problematic in practice. Only merge by name and error out on incompatible flags.
1. Allocate synthetic sections and linker-defined symbols.
    - Allocate final string and symbol table (`.symtab` and `.strtab`); note that symbol values are not yet finalized.
    - Section header string table (`.shstrtab`).
    - Allocate a global offset table (`.got`).
1. Sort sections by type and flags, so that, e.g., `SHF_ALLOC` sections or sections with the same flags (e.g., read-execute or read-write) are grouped together.
1. Scan relocations to find references to undefined symbols and determine which GOT entries are needed.
1. Generate program headers, but still without final offsets (these require the size of the program headers).
    - Generate a `PT_PHDR` entry, this must come first.
    - For `PT_LOAD` segments, filesz can be smaller than memsz to cover `SHF_NOBITS` sections.
    - Although this is not explicitly specified, the ELF header and the program headers *must* be covered by a `PT_LOAD` entry.
    - If there are sections with `SHF_TLS`, generate a `PT_TLS` entry covering them all.
    - Generate a `PT_GNU_STACK` with appropriate permissions.
1. Assign virtual addresses for all sections/program headers/symbols, respecting alignment requirements of the sections.
1. Assign file offsets for all sections/program headers based on the virtual addresses.
    - For `PT_LOAD` segments, the file offset and the virtual address must be equal modulo the page size (`0x1000`).
    - The ELF header and program headers come first, followed by loaded sections/segments, followed by non-loaded sections, followed by the symbol/string table.
    - Adjust the addresses and file offsets in section/program headers, symbol information, and the GOT accordingly.
1. Open output file and write individual components. You can apply relocations during writing, avoiding an unnecessary copy of the file contents.

## Simple Test Programs
All commands should pass. This is by no means a complete test suite and we strongly recommend that you write further and more thorough tests. For development purposes, you might find to make these tests pass in order useful.

### Simple

    echo ".global _start; .section .text; _start: call exit" | as -o a.o
    echo '.global exit; .section .text; exit: mov $60,%eax; syscall' | as -o b.o
    ld a.o b.o && ./a.out

### Global Symbol Undefined

    echo '.global _start; .extern sym; .section .text; _start: movabsq $sym, %rax; setz %dil; mov $60,%eax; syscall' | as -o a.o
    ! ld a.o

### Weak Symbol Undefined

    echo '.global _start; .weak weaksym; .section .text; _start: movabsq $weaksym, %rax; setz %dil; mov $60,%eax; syscall' | as -o a.o
    ld a.o && ./a.out

### Weak Symbol Single Weak Definition

    echo '.global _start; .weak weaksym; .section .text; _start: movq (weaksym), %rax; cmp $1, %rax; setne %dil; mov $60,%eax; syscall' | as -o a.o
    echo '.weak weaksym; .section .rodata; weaksym: .8byte 1' | as -o b.o
    ld a.o b.o && ./a.out

### Weak Symbol Overriding Global Definition

    echo '.global _start; .weak weaksym; .section .text; _start: movq (weaksym), %rax; cmp $2, %rax; setne %dil; mov $60,%eax; syscall' | as -o a.o
    echo '.weak weaksym; .section .rodata; weaksym: .8byte 1' | as -o b.o
    echo '.global weaksym; .section .rodata; weaksym: .8byte 2' | as -o c.o
    ld a.o b.o c.o && ./a.out
    ld a.o c.o b.o && ./a.out
    ld a.o c.o && ./a.out
    ld a.o b.o && ! ./a.out

### Multiple Weak Symbol Definitions

    echo '.global _start; .section .text; _start: movq (weaksym), %rax; cmp $2, %rax; setne %dil; mov $60,%eax; syscall' | as -o a.o
    echo '.weak weaksym; .section .rodata; weaksym: .8byte 1' | as -o b.o
    echo '.weak weaksym; .section .rodata; weaksym: .8byte 2' | as -o c.o
    ld a.o b.o c.o && ! ./a.out # first weak symbol wins, value 1 != 2
    ld a.o c.o b.o && ./a.out # first weak symbol wins, value 2 == 2
    ld a.o b.o && ! ./a.out
    ld a.o c.o && ./a.out
    ! ld a.o # weaksym undefined

### Section Renaming/Merging

    echo '.global _start; .section .text.xxx; _start: mov $60,%eax; syscall' | as -o a.o
    ld a.o
    readelf -SW a.out | ! grep -E ' \.text[^ ]'
    [ $(readelf -SW a.o | grep ' \.text' | wc -l) = 1 ]

### Extraction Order of Archives

    echo ".global _start; .section .text; _start: call exit" | as -o a.o
    echo '.global exit; .section .text; exit: mov $60,%eax; syscall' | as -o b.o
    echo '.global exit; .section .text; exit: mov $1,%edi; mov $60,%eax; syscall' | as -o c.o
    ar rc b.a b.o
    ld a.o b.a && ./a.out
    ! ld a.o b.a c.o # duplicate definition of exit; b.o is extracted from b.a first
    ld a.o c.o b.a && ! ./a.out # b.o is not extracted from b.a, exit already defined

## Testing with musl libc
Your linker should support static linking against [musl libc](https://musl.libc.org/). Our CI image has `musl-gcc` preinstalled. On your system, use the following commands to download and compile musl:

    git clone git://git.musl-libc.org/musl --depth=10
    mkdir musl-build
    cd musl-build
    ../musl/configure --prefix=$PWD/../musl-install --disable-shared
    # Adjust parallelism to your system
    make -j10
    # Fix GCC config to not set dynamic linker, we only support static binaries
    sed -i 's/^-dynamic-linker [^ ]* //' lib/musl-gcc.specs
    make install

Afterwards, you can find a GCC wrapper that uses musl in `./musl-install/bin/musl-gcc`. You can compile programs with your linker as follows:

    musl-gcc -B /path/to/your/ld/build/dir/ -static -o main main.c

Compare your output against GNU ld and make sure that the resulting executable behaves as expected.

## Bonus (+10%): glibc and C++ Support
Implement support for the following features and support static linking of C++ projects against glibc:

- Merge `.eh_frame` sections properly and generate the `.eh_frame_hdr` binary search table. This is required for C++ exceptions/stack unwinding.
- Handle comdat groups (`SHF_GROUP` with `GRP_COMDAT`). This is required for C++ templates.
- Handle `STT_GNU_IFUNC` symbols; this needs generation of PLT/GOT and `R_X86_64_IRELATIVE` dynamic relocations. This is required for glibc support. If the symbols `__rela_iplt_{start,end}` are defined, glibc will take care of these relocations itself on start-up.

Extend your tests appropriately and make sure that ifuncs, C++ templates, and C++ exceptions work as intended.
