#pragma once

// ─── X-Port (.xps) format — PS2 GameFAQs ─────────────────────────────────────
//
// Produced by Datel's X-Port USB adapter for PS2.  Very similar to the
// Action Replay MAX format but with a different magic and minor header
// differences.
//
// File layout:
//   0x00 – 0x03   Magic: "Ps2/" (4 bytes)
//   0x04 – 0x0B   Directory name (8 bytes, ASCII)
//   0x0C – 0x0F   Data length (little-endian uint32)
//   0x10 – EOF    LZO1X-compressed save data (same layout as .max payload)

#include <filesystem>
#include <cstddef>
#include <cstdint>

namespace cyan {

class PS2MemoryCard;

namespace formats {

static constexpr char XPS_MAGIC[] = "Ps2/";

class XPortHandler {
public:
    static bool importFrom(const std::filesystem::path& xps_path,
                           PS2MemoryCard& card);

    static bool exportTo(const PS2MemoryCard& card,
                         std::size_t          save_index,
                         const std::filesystem::path& out_path);

    static bool isValid(const std::filesystem::path& path);

private:
#pragma pack(push, 1)
    struct Header {
        char          magic[4];
        char          dir_name[8];
        std::uint32_t data_length;
    };
#pragma pack(pop)
};

} // namespace formats
} // namespace cyan
