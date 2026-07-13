#include "MadCatzHandler.hpp"
#include "core/ps1/PS1MemoryCard.hpp"
#include <fstream>

namespace cyan::formats {

bool MadCatzHandler::isValid(const std::filesystem::path& path) {
    // .mcx files have no distinct magic; validate by checking file size is
    // a multiple of the PS1 block size plus the 128-byte header.
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) return false;
    const auto data_size = size - HEADER_SIZE;
    return data_size > 0 && (data_size % 8192u) == 0;
}

bool MadCatzHandler::importFrom(const std::filesystem::path& /*mcx_path*/,
                                 PS1MemoryCard& /*card*/) {
    // TODO: read 128-byte directory frame, copy data blocks into a free slot.
    return false;
}

bool MadCatzHandler::exportTo(const PS1MemoryCard& /*card*/,
                               std::size_t          /*slot_index*/,
                               const std::filesystem::path& /*out_path*/) {
    // TODO: write directory frame then raw block data.
    return false;
}

} // namespace cyan::formats
