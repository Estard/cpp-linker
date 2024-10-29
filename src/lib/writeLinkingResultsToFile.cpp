#include "writeLinkingResultsToFile.hpp"
#include "convenient_functions.hpp"
#include "statusreport.hpp"

#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>

namespace cppld {

namespace /*internal*/ {

struct PrepareRelaWrite {
    struct {
        in<ProcessedRela> rela;
        size_t gotAddress;
        readonly_span<size_t> outputSectionAddresses;
        size_t outputAddress;
        off_t fileOffset;
    } in;

    struct {
        out<size_t> relaValue;
        out<size_t> relaValueSize;
        out<off_t> filePos;
    } out;
};
auto prepareRelaWrite(PrepareRelaWrite p) -> StatusCode {
    auto& [rela, gotAddress, outputSectionAddresses, outputAddress, fileOffset] = p.in;
    auto& [relaValue, relaValueSize, filePos] = p.out;

    size_t A = static_cast<size_t>(rela.addend);
    size_t GOT = gotAddress;
    size_t S = outputSectionAddresses[rela.symbolSectionID] + rela.symbolValue;
    size_t G = rela.symbolValue;
    size_t P = outputAddress + rela.outputSectionOffset;

    if (rela.note == ProcessedRela::Note::undefinedWeak) {
        S = 0;
    }

    if (rela.note == ProcessedRela::Note::absoluteValue) {
        S = rela.symbolValue;
    }

    relaValue = 0;
    relaValueSize = 0;
    filePos = fileOffset + static_cast<off_t>(rela.outputSectionOffset);
    switch (rela.type) {
        case R_X86_64_NONE: break;
        case R_X86_64_64:
            relaValue = S + A;
            relaValueSize = sizeof(uint64_t);
            break;

        case R_X86_64_PLT32: // There is not procedure linkage table. But since we are in a no-pic file the value is known anyway
        case R_X86_64_PC32:
            relaValue = S + A - P;
            relaValueSize = sizeof(uint32_t);
            break;

        case R_X86_64_GOT32:
            relaValue = G + A;
            relaValueSize = sizeof(uint32_t);
            break;
        //case R_X86_64_COPY: break;
        case R_X86_64_GLOB_DAT:
        case R_X86_64_JUMP_SLOT:
            relaValue = S;
            relaValueSize = sizeof(size_t);
            break;

        //case R_X86_64_RELATIVE: break;
        case R_X86_64_GOTPCREL:
            relaValue = G + GOT + A - P;
            relaValueSize = sizeof(uint32_t);

            break;
        case R_X86_64_32:
            relaValue = S + A;
            relaValueSize = sizeof(uint32_t);

            break;
        case R_X86_64_32S:
            relaValue = S + A;
            relaValueSize = sizeof(int32_t);

            break;
        case R_X86_64_16:
            relaValue = S + A;
            relaValueSize = sizeof(uint16_t);

            break;
        case R_X86_64_PC16:
            relaValue = S + A - P;
            relaValueSize = sizeof(uint16_t);

            break;
        case R_X86_64_8:
            relaValue = S + A;
            relaValueSize = sizeof(uint8_t);
            break;
        case R_X86_64_PC8:
            relaValue = S + A - P;
            relaValueSize = sizeof(uint8_t);
            break;
        //case R_X86_64_DTPMOD64: break;
        //case R_X86_64_DTPOFF64: break;
        //case R_X86_64_TPOFF64: break;
        //case R_X86_64_TLSGD: break;
        //case R_X86_64_TLSLD: break;
        //case R_X86_64_DTPOFF32: break;
        //case R_X86_64_GOTTPOFF: break;
        //case R_X86_64_TPOFF32: break;
        case R_X86_64_PC64:
            relaValue = S + A - P;
            relaValueSize = sizeof(uint64_t);

            break;
        case R_X86_64_GOTOFF64:
            relaValue = S + A - GOT;
            relaValueSize = sizeof(uint64_t);

            break;
        case R_X86_64_GOTPC32:
            relaValue = GOT + A - P;
            relaValueSize = sizeof(uint32_t);
            break;
        case R_X86_64_GOT64:
            relaValue = G + A;
            relaValueSize = sizeof(uint64_t);

            break;
        case R_X86_64_GOTPCREL64:
            relaValue = G + GOT + A - P;
            relaValueSize = sizeof(uint64_t);

            break;
        case R_X86_64_GOTPC64:
            relaValue = GOT + A - P;
            relaValueSize = sizeof(uint64_t);
            break;
        //case R_X86_64_GOTPLT64: break;
        //case R_X86_64_PLTOFF64: break;
        case R_X86_64_SIZE32:
            relaValue = rela.symbolValue + A;
            relaValueSize = sizeof(uint32_t);

            break;
        case R_X86_64_SIZE64:
            relaValue = rela.symbolValue + A;
            relaValueSize = sizeof(uint64_t);

            break;
        //case R_X86_64_GOTPC32_TLSDESC: break;
        //case R_X86_64_TLSDESC_CALL: break;
        //case R_X86_64_TLSDESC: break;
        //case R_X86_64_IRELATIVE: break;
        //case R_X86_64_RELATIVE64: break;
        case R_X86_64_GOTPCRELX:
        case R_X86_64_REX_GOTPCRELX:
            relaValue = G + GOT + A - P;
            relaValueSize = sizeof(uint32_t);
            break;
        default:
            return report(StatusCode::not_ok, "unsupported relocation type ", rela.type);
    }

    return StatusCode::ok;
}
} // namespace

auto writeLinkingResultsToFile(parametersFor::WriteLinkingResultsToFile p) -> StatusCode {
    auto& [elfAddresses, sectionHeaders, outputFileName, elfHeader, programHeaders,
           outputSectionHeaders, outputToInputSections, materializedViews,
           outputSectionAddresses, outputSectionFileOffsets, outputSectionSizes,
           outputSectionTypes, inputSectionCopyCommands, gotAddress, processedRelas] = p.in;
    auto _outputFileNameString = std::string(outputFileName);
    auto _outputFileCstring = _outputFileNameString.c_str();

    constexpr auto filePermissions = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH;
    struct CloseOnExitAndMakeExecutable {
        int fd;
        operator int() const { return fd; }
        ~CloseOnExitAndMakeExecutable() {
            if (fd >= 0) {
                ::close(fd);
            }
        }
        // Set file permissions to make it immediately executable after the linker has run
    } outFD{::open(_outputFileCstring, O_CREAT | O_RDWR | O_TRUNC, filePermissions)};
    if (outFD == -1) {
        return report(StatusCode::system_failure, "Could not open file \"", outputFileName, "\2 to write output");
    }
    auto fileSize = (elfHeader.e_shoff + outputSectionHeaders.size() * sizeof(Elf64_Shdr));
    if (::ftruncate(outFD, static_cast<off_t>(fileSize)) == -1) {
        return report(StatusCode::system_failure, "Could not resize file \"", outputFileName, "\" to expected size: ", fileSize);
    }

    struct UnMapOnExit {
        std::byte* mem;
        size_t fileSize;
        operator std::byte*() const { return mem; }
        ~UnMapOnExit() { // Technically unecessary since process exit does it, too
            if (mem) {
                ::munmap(mem, fileSize);
            }
        }
    } destination{static_cast<std::byte*>(::mmap(nullptr, fileSize, PROT_WRITE, MAP_SHARED, outFD, 0)), fileSize};
    if (!destination)
        return report(StatusCode::system_failure, "Could not map file to write output");
    StatusCode status{StatusCode::ok};

    // memcpy is faster than a bunch of system calls
    // it might not be the most ideal method to write something to a file,
    // but it is close enough to lld that it doesn't matter 
    std::memcpy(destination, &elfHeader, sizeof(Elf64_Ehdr));
    std::memcpy(destination + sizeof(Elf64_Ehdr), programHeaders.data(), programHeaders.size() * sizeof(Elf64_Phdr));

    parallel_for_each_indexed(materializedViews, [&](std::byte* mem, size_t outSecID) {
        if (outputSectionTypes[outSecID] == SHT_NOBITS) return;

        auto& relas = processedRelas[outSecID];
        auto outputAddress = outputSectionAddresses[outSecID];
        auto fileOffset = static_cast<off_t>(outputSectionFileOffsets[outSecID]);
        auto sectionSize = outputSectionSizes[outSecID];

        if (mem) {
            std::memcpy(destination + fileOffset, mem, sectionSize);
        } else {
            for (auto secRef : outputToInputSections[outSecID]) {
                auto& section = sectionHeaders[secRef.elfIndex][secRef.headerIndex];
                auto sectionAddress = elfAddresses[secRef.elfIndex] + section.sh_offset;

                auto& copyCmds = inputSectionCopyCommands[secRef.elfIndex][secRef.headerIndex];
                auto performCopy = overloaded{
                    [&](std::vector<PartCopy> const& copyCmdVec) {
                        size_t inSectionOffset{0};
                        for (auto& cmd : copyCmdVec) {
                            std::memcpy(destination + fileOffset + cmd.dstOffset, sectionAddress + inSectionOffset, cmd.size);
                            inSectionOffset += cmd.size;
                        }
                    },
                    [&](PartCopy const& cmd) {
                        std::memcpy(destination + fileOffset + cmd.dstOffset, sectionAddress, cmd.size);
                    },
                    [](auto&&) {
                        //std::unreachable();
                    }};
                std::visit(performCopy, copyCmds);
            }
        }

        for_each_indexed(relas, [&](ProcessedRela const& rela, size_t) {
            size_t relaValue{};
            size_t relaValueSize{};
            off_t relaFilePos{};
            auto relaStatus = prepareRelaWrite({.in{rela,
                                                    gotAddress,
                                                    outputSectionAddresses,
                                                    outputAddress,
                                                    fileOffset},
                                                .out{relaValue,
                                                     relaValueSize,
                                                     relaFilePos}});
            if (relaStatus != StatusCode::ok) {
                std::atomic_ref{status}.store(relaStatus);
                return;
            }

            if (relaValueSize) {
                std::memcpy(destination + relaFilePos, &relaValue, relaValueSize);
            }
        });
    });

    if (status != StatusCode::ok) return status;

    std::memcpy(destination + elfHeader.e_shoff, outputSectionHeaders.data(), outputSectionHeaders.size() * sizeof(Elf64_Shdr));

    return status;
}
} // namespace cppld