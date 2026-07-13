#pragma once

// ─── Action Replay Max (.max) format — PS2 GameFAQs ──────────────────────────
//
// Produced by Datel's Action Replay MAX cheat device for PS2.  Each .max file
// contains exactly one game save.
//
// File layout:
//   0x00 – 0x03   Magic: "XMEM" (4 bytes)
//   0x04 – 0x0B   Save name (8 bytes, ASCII game-directory name)
//   0x0C – 0x0F   Data length (little-endian uint32)
//   0x10 – EOF    Compressed save data (LZO1X compression)
//
// The decompressed data mirrors the PS2 VFS directory structure:
//   4-byte file count, then repeated:  [ 64-byte dir-entry | file data ]

#include <filesystem>
#include <cstddef>
#include <cstdint>

namespace cyan {

class PS2MemoryCard;

namespace formats {

static constexpr char MAX_MAGIC[] = "XMEM";

class ActionReplayMaxHandler {
public:
    static bool importFrom(const std::filesystem::path& max_path,
                           PS2MemoryCard& card);

    static bool exportTo(const PS2MemoryCard& card,
                         std::size_t          save_index,
                         const std::filesystem::path& out_path);

    static bool isValid(const std::filesystem::path& path);

private:
#pragma pack(push, 1)
    struct Header {
        char          magic[4];
        char          save_name[8];
        std::uint32_t data_length;
    };
#pragma pack(pop)

    // LZO decompression stub — to be implemented.
    static bool decompress(const std::uint8_t* src, std::size_t src_len,
                           std::uint8_t*       dst, std::size_t dst_capacity,
                           std::size_t&        out_len);
};

} // namespace formats
} // namespace cyan
