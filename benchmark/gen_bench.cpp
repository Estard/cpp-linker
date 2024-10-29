#include "bits/stdc++.h" // I am lazy
#include "fmt/format.h"

int main() {
    constexpr size_t num_files{300};
    constexpr size_t num_section_pairs_per_file{200};

    for (size_t f{0}; f < num_files; ++f) {
        auto filename = fmt::format("asm_{}.S", f);
        std::ofstream file(filename, std::ios::binary | std::ios::trunc);
        for (size_t s{0}; s < num_section_pairs_per_file; ++s) {
            file << fmt::format(".section .data.{0}_{1};"
                                ".global datum_{0}_{1};"
                                " datum_{0}_{1}: .8byte 1;\n",
                                f, s);
            if ((s + 1) < num_section_pairs_per_file) {
                file << fmt::format(".section .text.{0}_{1};"
                                    ".global chain_{0}_{1};"
                                    " chain_{0}_{1}: add (datum_{0}_{1}),%rdi;"
                                    "jmp chain_{0}_{2}\n",
                                    f, s, s + 1);
            }
            else{
                if((f+1) < num_files){
                file << fmt::format(".section .text.{0}_{1};"
                                    ".global chain_{0}_{1};"
                                    ".extern chain_{2}_0;"
                                    " chain_{0}_{1}: add (datum_{0}_{1}),%rdi;"
                                    "jmp chain_{2}_0\n",
                                    f, s, f+1);
                }
                else{
                    file << fmt::format(".section .text.{0}_{1};"
                                    ".global chain_{0}_{1};"
                                    " chain_{0}_{1}: add (datum_{0}_{1}),%rdi;"
                                    "mov %rdi,%rax; ret;\n",
                                    f, s);
                }
            }
            file.flush();
        }

        std::ignore = std::system(fmt::format("as -o chain_{0}.o asm_{0}.S",f).c_str());
    }

    return 0;
}