# CPPLD

A Linker for static ELF executables using only the standard library and system APIs. 

## Design Notes

__in, out and inout parameters__

Functions take _in_ some data, perform some calculation on it and write _out_ some other data.
Occasionally, they also take data in and modify it in place, which is expressed by _inout_ (not going to be strictly functional here, since returning a modified copy would be inefficient).
Sometimes the input data couldn't be processed successfully in which case an error should be returned.

Unfortunately, the semantics of C++ sometimes struggles to properly express those concepts. 
Especially multiple return values pose issues. If e.g. 2 ints should be returned, either a struct has to be defined or a `std::pair` or a `std::tuple` needs to be returned instead. 
If the function can fail, there is the issue of how to communicate this issue to the caller. The standard C++ approach right now is to throw an exception. 
But throwing exceptions is not a suitable approach in case several errors should be reported since it rewinds the program immediately to the next catch block.
It is also an issue for reasoning about the control flow since a function suddenly has multiple, different exit paths.

New C++ features available in C++23 or C++ Syntax 2 would address those issues, those are, however, not yet available.

As a substitute for missing features, the major functions in this project are written to take in a special struct that groups contained members into "in", "out" and if required "inout". This approach allows for named initialization of function parameters. This also frees up the regular return value to return a StatusCode that is used to indicate the success or failure of the operation. 

An optimizing compiler should be able to elide unnecessary loads and stores to the stack in a MemToReg pass. So this approach should not have a significant negative impact on performance.

__Stream processing__
Data is organized into vectors of identical types.
Logically connected pieces of data appear at the same index.
This approach is essentially a simple form of a columnar database layout. 
Since often times large sets of identical data are processed one after another, this approach saves memory bandwidth and improves cache utilization by only loading in actually needed data.
Processing streams of data also opens up potential optimizations using SIMD instructions as the data layout is more optimal for this.

As a result, data streams are now grouped by processing and not by real world concept. This is why there is for example no 'Section'-class or subclasses there of.
It might take a while to get used to this.


__Multithreading__
Multithreading is done mostly via `std::async` but currently only for trivially parallel parts such as writing to the output file and parsing the initial set of input object files. 

A more sophisticated task system (e.g. a threadpool) could probably be employed to reduce the overhead of spawning threads. While the standard permits `std::async` to run on a threadpool, Only the msvc implementation does so.


## Features
- **Lazy archive extraction** – Archives are loaded only when deemed necessary. Backwards references are also possible. 
- **Most regular relocations** are accepted – TLS Relocations, relative relocations as well as R_X86_64_GOTPLT64, R_X86_64_PLTOFF64 and R_X86_64_COPY, however, are unsupported. This mostly stems from a lack of need and lack of specification.
- **Section Merging with de-duplication** – Section with SHF_MERGE optionally with SHF_STRINGS have their duplicate elements removed. Sometimes a section may want to merge with a section that doesn't have a SHF_MERGE flag. In this case, the merge flag is ignored, and the sections are concatenated regularly.
- **Synthesizing Symbol Table** – All defined symbols are transferred to the output. The type of the symbols is always retained and no special handling based on types is performed.
- **Linking with musl-libc** – Linking with musl's implementation of libc works well enough for printing to the screen. The `printf` function alone depends on large enough parts of the library to inspire confidence that it works sufficiently well. Even using `errno` works, showing that no relocations to thread local storage need to be supported. 
- **Program Headers** – For each segment a program header is generated. The first segment is a read or read write segment that also covers the elf and program headers. There is always such a segment since a global offset table is always generated. The fact that a global offset table is always present doesn't really matter since GNU ld produces an extra segment readonly segment to cover this region. The difference is at most 24 Byte which is likely covered by padding to align segments to page boundaries anyway. – A TLS segment is also created to cover thread local storage sections. While this is due to the lack of relocation support to those sections of lesser usefulness, the task specification didn't ask for anything else. 

## Notes on unsupported Features
- Section Groups lead to an error, but only because it's wanted that way. Those the `SHF_GROUP` flag could also just be ignored since relocations to such sections are defined as weak.
- For relocations against thread local storage no sufficient specification was provided on how they should work. Googling for those relocation types has as the top results mostly issue tracker entries where major linkers didn't handle them correctly. Not very encouraging
- The extra 10% / features to support C++ are not implemented. There was simply no time for that. 
- 'Corner cases' doesn't really mean much if one is generally unfamiliar and has little experience with the subject. Checking for too many output sections is covered. But adversarial input is weird to think about in this context. The worst case that could happen is that the resulting file doesn't work, or the linker doesn't produce an output. In both cases, this is expected behavior if garbage input is provided.
- Linker relaxations are not implemented, but they weren't required either.
## Benchmarks

### Method

To measure performance, 300 input files were generated with `gen_bench.cpp`.
Each file contains 200 section pairs of a data and a text section.
In the data section, global, unique 8 Byte value is defined.
The text section contains a global function which adds the corresponding data value to the result and calls the next function in the next section, unless it is the last generated function, in which case it returns.

The function chain is invoked as via `call_chain.c` which prints the result of the chain for verification upon execution.

`call_chain.c` is linked against musl's version of libc together will all other objects files and libraries that musl-gcc links per default.



Linkers tested against each other: *GNU ld 2.38*, *LLD 15.0.7* and *cppld (Jonas Lehmann implementation)*.
The latter was compiled with  `G++12.3 -O3 -march=native -flto`

The following options were supplied to all linkers to ensure approximately the same processing steps are performed:
`-static --build-id=none --no-eh-frame-hdr -z noexecstack --start-group <linked files> --end-group`


Performance was measured with `perf` using the following options:
`perf stat -a -r 24 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,cache-references`


The system the performance was measured on:
`(Ubuntu 22.04, i5-10210u (4 cores, 8 threads)@ 4GHz, 16 GB DDR4 2400-cl17`

 
### Results
__GNU LD__
```
(24 runs):

         10.138,28 msec task-clock                #    8,016 CPUs utilized            ( +-  0,10% )
     6.509.838.137      cycles                    #    0,643 GHz                      ( +-  0,33% )
     9.446.739.002      instructions              #    1,45  insn per cycle           ( +-  0,19% )
     1.791.715.524      branches                  #  177,081 M/sec                    ( +-  0,18% )
        11.482.049      branch-misses             #    0,64% of all branches          ( +-  0,79% )
       135.040.584      cache-misses              #   68,842 % of all cache refs      ( +-  0,17% )
       197.295.651      cache-references          #   19,499 M/sec                    ( +-  0,39% )

           1,26483 +- 0,00129 seconds time elapsed  ( +-  0,10% )
```


__LD.LLD__
```
(24 runs):

            810,77 msec task-clock                #    8,105 CPUs utilized            ( +-  0,44% )
       666.133.111      cycles                    #    0,833 GHz                      ( +-  2,01% )
       653.429.753      instructions              #    1,00  insn per cycle           ( +-  1,45% )
       124.455.778      branches                  #  155,667 M/sec                    ( +-  1,47% )
           899.644      branch-misses             #    0,75% of all branches          ( +-  6,59% )
        16.072.318      cache-misses              #   56,058 % of all cache refs      ( +-  1,03% )
        29.315.305      cache-references          #   36,667 M/sec                    ( +-  1,49% )

          0,100037 +- 0,000444 seconds time elapsed  ( +-  0,44% )
```


__CPPLD__

```
(24 runs):

            779,17 msec task-clock                #    7,832 CPUs utilized            ( +-  1,03% )
       475.702.916      cycles                    #    0,598 GHz                      ( +-  1,45% )
       279.800.042      instructions              #    0,58  insn per cycle           ( +-  2,59% )
        51.277.356      branches                  #   64,488 M/sec                    ( +-  2,72% )
           493.148      branch-misses             #    0,93% of all branches          ( +-  4,91% )
         9.116.397      cache-misses              #   50,355 % of all cache refs      ( +-  0,91% )
        17.837.614      cache-references          #   22,433 M/sec                    ( +-  1,07% )

           0,09949 +- 0,00100 seconds time elapsed  ( +-  1,01% )
```

### Discussion

Taking GNU ld as the baseline, lld and cppld are bot around 12-13 times faster.

It can be observed that while cppld executes fewer instruction than lld but has poorer instructions per cycle. This makes it effectively a tie between the two with cppld taking a slight lead.
This shows that the design choices in terms of architecture are effective even without the benefit of outside libraries.

Inspecting causes for poor performance shows that a major culprit is calculating hash values and performing queries into the hash tables, e.g. for Symbol insertion and for Symbol lookup.
Conceptually, the solution to this problem is to "just use a faster hashmap". However, a potentially better performing alternative such as `std::flat_map` is only available with C++23, using 3rd party dependencies is not desired (standard lib and system API only) and writing a high performing hash map for this specific use case is not in the scope of this project. Attempts have been made to improve performance using local allocators, but this doesn't change the fact that the memory of a `std::unordered_map` is just all over the place.

Another bottleneck is writing the results to the output file. Currently, the result file mapped into memory using mmap and then filled by writing the values to the right place. This approach is better than issuing several write/pwrite system calls though it doesn't seem ideal. 
Writing to the output file seems to introduce a lot more cache misses than it realistically should. Further investigations need to be made.

It should also be noted that this is a single, synthetic benchmark, unfortunately no large enough c project was at hand to test linking performance on a real project.


# Conclusion

Elf Linking is just a glorified memcpy operation.
