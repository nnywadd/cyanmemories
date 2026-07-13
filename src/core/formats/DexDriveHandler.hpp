#pragma once

// ─── DexDrive (.gme) format — PS1 GameFAQs ───────────────────────────────────
//
// A .gme file wraps a raw 128 KiB PS1 memory card image with a 3 904-byte
// proprietary header block added by the InterAct DexDrive accessory.
//
// Header layout (3 904 bytes):
//   0x0000 – 0x000B   Magic: "123-456-STD\0"
//   0x0010 – 0x000F   DexDrive identifier string
//   0x0040 – 0x013F   15 × 16-byte slot comment strings (ASCII)
//   0x0140 – 0x0F3F   15 × 256-byte slot description strings (ASCII)
//   Remainder         Padding to 3 904 bytes
// 0x0F40 – 0x2093F   Raw 128 KiB PS1 memory card image

#include <filesystem>
#include <string>
#include <vector>

namespace cyan {

class PS1MemoryCard;

namespace formats {

static constexpr char     GME_MAGIC[]          = "123-456-STD";
static constexpr std::size_t GME_HEADER_SIZE   = 3904u;
static constexpr std::size_t GME_COMMENT_SIZE  = 16u;
static constexpr std::size_t GME_DESC_SIZE     = 256u;
static constexpr std::size_t GME_SLOT_COUNT    = 15u;

class DexDriveHandler {
public:
    // Import: read a .gme file and populate a PS1MemoryCard.
    static bool importFrom(const std::filesystem::path& gme_path,
                           PS1MemoryCard& card);

    // Export: write the entire card image to a .gme file.
    static bool exportTo(const PS1MemoryCard& card,
                         const std::filesystem::path& out_path);

    // Validate: return true if the file has the correct GME magic.
    static bool isValid(const std::filesystem::path& path);

private:
    struct Header {
        char magic[12];
        char identifier[20];
        char comments[GME_SLOT_COUNT][GME_COMMENT_SIZE];
        char descriptions[GME_SLOT_COUNT][GME_DESC_SIZE];
    };
};

} // namespace formats
} // namespace cyan
