#include "ActionReplayMaxHandler.hpp"
#include "core/ps2/PS2MemoryCard.hpp"
#include <fstream>
#include <cstring>

namespace cyan::formats {

bool ActionReplayMaxHandler::isValid(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char magic[4]{};
    f.read(magic, 4);
    return f && std::memcmp(magic, MAX_MAGIC, 4) == 0;
}

bool ActionReplayMaxHandler::importFrom(const std::filesystem::path& /*max_path*/,
                                         PS2MemoryCard& /*card*/) {
    // TODO: read Header, decompress LZO payload, inject into card FAT.
    return false;
}

bool ActionReplayMaxHandler::exportTo(const PS2MemoryCard& /*card*/,
                                       std::size_t          /*save_index*/,
                                       const std::filesystem::path& /*out_path*/) {
    // TODO: dump PS2 VFS directory, LZO-compress, write with Header.
    return false;
}

bool ActionReplayMaxHandler::decompress([[maybe_unused]] const std::uint8_t* src,
                                         [[maybe_unused]] std::size_t src_len,
                                         [[maybe_unused]] std::uint8_t* dst,
                                         [[maybe_unused]] std::size_t dst_capacity,
                                         [[maybe_unused]] std::size_t& out_len) {
    // TODO: implement LZO1X decompression (or link liblzo2).
    return false;
}

} // namespace cyan::formats
