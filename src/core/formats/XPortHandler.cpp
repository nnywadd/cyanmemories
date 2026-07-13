#include "XPortHandler.hpp"
#include "core/ps2/PS2MemoryCard.hpp"
#include <fstream>
#include <cstring>

namespace cyan::formats {

bool XPortHandler::isValid(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char magic[4]{};
    f.read(magic, 4);
    return f && std::memcmp(magic, XPS_MAGIC, 4) == 0;
}

bool XPortHandler::importFrom(const std::filesystem::path& /*xps_path*/,
                               PS2MemoryCard& /*card*/) {
    // TODO: read Header, decompress LZO1X payload, inject into card FAT.
    // Payload layout mirrors .max; reuse ActionReplayMaxHandler::decompress.
    return false;
}

bool XPortHandler::exportTo(const PS2MemoryCard& /*card*/,
                             std::size_t          /*save_index*/,
                             const std::filesystem::path& /*out_path*/) {
    // TODO: dump PS2 VFS directory, LZO1X-compress, write with XPS header.
    return false;
}

} // namespace cyan::formats
