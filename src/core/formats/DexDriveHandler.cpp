#include "DexDriveHandler.hpp"
#include "core/ps1/PS1MemoryCard.hpp"
#include <array>
#include <cstring>
#include <fstream>

namespace cyan::formats {

// Total valid .gme file size: 3 904-byte header + 131 072-byte raw PS1 image.
static constexpr std::size_t GME_TOTAL_SIZE = GME_HEADER_SIZE + ps1::CARD_SIZE; // 134 976

bool DexDriveHandler::isValid(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char magic[sizeof(GME_MAGIC) - 1]{};
    f.read(magic, sizeof(magic));
    return f && std::memcmp(magic, GME_MAGIC, sizeof(magic)) == 0;
}

bool DexDriveHandler::importFrom(const std::filesystem::path& gme_path,
                                  PS1MemoryCard& card) {
    std::ifstream f(gme_path, std::ios::binary);
    if (!f) return false;

    // Validate file size.
    f.seekg(0, std::ios::end);
    const auto file_size = static_cast<std::size_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    if (file_size < GME_TOTAL_SIZE) return false;

    // Validate magic bytes without consuming the seek position.
    {
        char magic[sizeof(GME_MAGIC) - 1]{};
        f.read(magic, sizeof(magic));
        if (!f || std::memcmp(magic, GME_MAGIC, sizeof(magic)) != 0) return false;
    }

    // Skip the rest of the 3 904-byte header.
    f.seekg(static_cast<std::streamoff>(GME_HEADER_SIZE), std::ios::beg);
    if (!f) return false;

    // Read the raw 128 KiB PS1 memory card image.
    std::array<std::uint8_t, ps1::CARD_SIZE> buf{};
    f.read(reinterpret_cast<char*>(buf.data()), ps1::CARD_SIZE);
    if (!f) return false;

    // Inject into the card object, recording the .gme path as the source.
    return card.loadFromBuffer(buf, gme_path);
}

bool DexDriveHandler::exportTo(const PS1MemoryCard& card,
                                const std::filesystem::path& out_path) {
    std::ofstream f(out_path, std::ios::binary | std::ios::trunc);
    if (!f) return false;

    // Build the 3904-byte header.  The Header struct (4112 bytes) is larger than
    // GME_HEADER_SIZE; we write exactly GME_HEADER_SIZE bytes of it so that the
    // card image starts at the correct file offset (0x0F40).
    Header hdr{};
    std::memcpy(hdr.magic, GME_MAGIC, sizeof(GME_MAGIC) - 1);

    for (const auto& s : card.getSaves()) {
        const std::size_t slot = s->getSlotIndex();
        if (slot < GME_SLOT_COUNT) {
            const std::string& title = s->getTitle();
            std::strncpy(hdr.comments[slot],     title.c_str(), GME_COMMENT_SIZE - 1u);
            std::strncpy(hdr.descriptions[slot], title.c_str(), GME_DESC_SIZE    - 1u);
        }
    }

    f.write(reinterpret_cast<const char*>(&hdr),
            static_cast<std::streamsize>(GME_HEADER_SIZE));

    // Append the full 128 KiB raw memory card image.
    const auto& raw = card.getRawCardData();
    f.write(reinterpret_cast<const char*>(raw.data()),
            static_cast<std::streamsize>(raw.size()));

    return f.good();
}

} // namespace cyan::formats
