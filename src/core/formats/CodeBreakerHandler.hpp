#pragma once

// ─── CodeBreaker (.cbs) format — PS2 GameFAQs ────────────────────────────────
//
// Produced by Pelican's CodeBreaker device for PS2.  The .cbs file uses a
// custom header followed by deflate-compressed save data.
//
// File layout:
//   0x00 – 0x03   Magic: "CFU\0" or similar (verify per real sample)
//   0x04 – 0x0F   Directory name (null-terminated ASCII, 12 bytes)
//   0x10 – 0x13   Uncompressed data size (little-endian uint32)
//   0x14 – 0x17   Compressed data size   (little-endian uint32)
//   0x18 – EOF    Deflate-compressed save data
//
// The decompressed payload is the same PS2 VFS directory dump used by .max.

#include <filesystem>
#include <cstddef>
#include <cstdint>

namespace cyan {

class PS2MemoryCard;

namespace formats {

class CodeBreakerHandler {
public:
    static bool importFrom(const std::filesystem::path& cbs_path,
                           PS2MemoryCard& card);

    static bool exportTo(const PS2MemoryCard& card,
                         std::size_t          save_index,
                         const std::filesystem::path& out_path);

    static bool isValid(const std::filesystem::path& path);

private:
#pragma pack(push, 1)
    struct Header {
        char          magic[4];
        char          dir_name[12];
        std::uint32_t uncompressed_size;
        std::uint32_t compressed_size;
    };
#pragma pack(pop)
};

} // namespace formats
} // namespace cyan
