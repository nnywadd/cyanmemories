#pragma once

// ─── MadCatz (.mcx) format — PS1 GameFAQs ────────────────────────────────────
//
// The MadCatz (.mcx) format is a single-save wrapper produced by MadCatz
// memory card accessories.  It is structurally identical to the raw .mcs
// single-save format but uses the .mcx extension.
//
// File layout:
//   0x0000 – 0x007F   Directory frame (128 bytes, mirrors PS1 dir entry)
//   0x0080 – EOF      Raw save block data (variable length, N × 8 192 bytes)

#include <filesystem>
#include <cstddef>

namespace cyan {

class PS1MemoryCard;

namespace formats {

class MadCatzHandler {
public:
    static bool importFrom(const std::filesystem::path& mcx_path,
                           PS1MemoryCard& card);

    static bool exportTo(const PS1MemoryCard& card,
                         std::size_t          slot_index,
                         const std::filesystem::path& out_path);

    static bool isValid(const std::filesystem::path& path);

private:
    static constexpr std::size_t HEADER_SIZE = 128u; // one PS1 directory frame
};

} // namespace formats
} // namespace cyan
