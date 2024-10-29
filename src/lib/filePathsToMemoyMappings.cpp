#include "cppld.hpp"
#include "statusreport.hpp"
#include "convenient_functions.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace cppld {

MemoryMappings::~MemoryMappings() {
    for_each_indexed(addresses, [&](void* address, size_t index) {
        ::munmap(address, memSizes[index]);
    });
}

auto filePathsToMemoryMappings(parametersFor::FilePathsToMemoryMappings p) -> StatusCode {
    auto& [filenames] = p.in;
    auto& [mappings] = p.out;

    mappings.addresses.reserve(filenames.size());
    mappings.memSizes.reserve(filenames.size());

    for (auto& filename : filenames) {
        struct CloseFDOnExit {
            int fd;
            operator int() const { return fd; }
            ~CloseFDOnExit() {
                if (fd >= 0) ::close(fd);
            }
        } fd{::open(filename.data(), O_RDONLY)};
        if (fd == -1) return report(StatusCode::not_ok, "could not open file: ", filename);

        struct stat mstat {};
        if (::fstat(fd, &mstat) == -1) return report(StatusCode::not_ok, "can't read file stats for: ", filename);
        if (!S_ISREG(mstat.st_mode)) return report(StatusCode::not_ok, "file is not regular: ", filename);

        size_t memSize = static_cast<size_t>(mstat.st_size);
        auto address = ::mmap(nullptr, memSize, PROT_READ, MAP_PRIVATE, fd, 0);
        if (!address) return report(StatusCode::not_ok, "unable to memory map file: ", filename);

        mappings.addresses.push_back(static_cast<std::byte*>(address));
        mappings.memSizes.push_back(memSize);
    }
    return StatusCode::ok;
}
} // namespace cppld