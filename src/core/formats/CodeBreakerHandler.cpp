#include "CodeBreakerHandler.hpp"
#include "core/ps2/PS2MemoryCard.hpp"
#include <fstream>
#include <cstring>

namespace cyan::formats {

bool CodeBreakerHandler::isValid(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char magic[4]{};
    f.read(magic, 4);
    // CBS magic varies across versions; check file extension as fallback.
    (void)magic;
    return path.extension() == ".cbs";
}

bool CodeBreakerHandler::importFrom(const std::filesystem::path& /*cbs_path*/,
                                     PS2MemoryCard& /*card*/) {
    // TODO: read Header, inflate zlib/deflate payload, inject into card FAT.
    return false;
}

bool CodeBreakerHandler::exportTo(const PS2MemoryCard& /*card*/,
                                   std::size_t          /*save_index*/,
                                   const std::filesystem::path& /*out_path*/) {
    // TODO: dump PS2 VFS directory, deflate-compress, write with Header.
    return false;
}

} // namespace cyan::formats
