#pragma once

#include "core/MemoryCard.hpp"
#include <array>
#include <cstdint>

namespace cyan {

// ─── PS1 raw memory card format constants ────────────────────────────────────
//
// Total size  : 131 072 bytes (128 KiB)
// Atom        : "frame" = 128 bytes
// Block       : 64 frames = 8 192 bytes
// Layout      : 1 directory block + 15 save-data blocks
//
// Frame 0            0x0000 – 0x007F   Header ("MC" magic + checksum)
// Frames 1–15        0x0080 – 0x07FF   15 directory entries (one per slot)
// Frames 16–35       0x0800 – 0x11FF   Broken sector list (unused by most emu)
// Frame 36           0x1200 – 0x127F   Header backup
// Frames 37–51       0x1280 – 0x19FF   Directory backup
// Frames 52–63       0x1A00 – 0x1FFF   Unused padding
// Frames 64–1023     0x2000 – 0xFFFF   15 × save blocks (64 frames each)

namespace ps1 {

static constexpr std::uint32_t CARD_SIZE        = 131'072u;
static constexpr std::uint32_t FRAME_SIZE       = 128u;
static constexpr std::uint32_t FRAMES_PER_BLOCK = 64u;
static constexpr std::uint32_t BLOCK_SIZE       = FRAME_SIZE * FRAMES_PER_BLOCK; // 8 192
static constexpr std::uint32_t NUM_SLOTS        = 15u;

// Directory entry state bytes
static constexpr std::uint8_t SLOT_FIRST   = 0x51;
static constexpr std::uint8_t SLOT_MIDDLE  = 0x52;
static constexpr std::uint8_t SLOT_LAST    = 0x53;
static constexpr std::uint8_t SLOT_FREE    = 0xFF;
static constexpr std::uint8_t SLOT_DELETED = 0xA0;

// Offsets within a directory frame
static constexpr std::uint32_t DIR_STATE_OFF      = 0x00; // 4 bytes
static constexpr std::uint32_t DIR_BLOCKCOUNT_OFF = 0x04; // 4 bytes
static constexpr std::uint32_t DIR_NEXTLINK_OFF   = 0x08; // 2 bytes  (0xFFFF = last)
static constexpr std::uint32_t DIR_FILENAME_OFF   = 0x0A; // 20 bytes (region+product+id)
static constexpr std::uint32_t DIR_CHECKSUM_OFF   = 0x7F; // 1 byte   (XOR of prev 127)

// Icon data within a save block
static constexpr std::uint32_t ICON_FLAG_OFF       = 0x00; // 2 bytes
static constexpr std::uint32_t ICON_ANIM_FRAME_OFF = 0x02; // 2 bytes (1/2/3 frames)
static constexpr std::uint32_t ICON_TITLE_OFF      = 0x04; // 64 bytes (Shift-JIS)
static constexpr std::uint32_t ICON_PALETTE_OFF    = 0x60; // 32 bytes (16 × RGB555)
static constexpr std::uint32_t ICON_PIXEL_OFF      = 0x80; // 128 bytes per frame

#pragma pack(push, 1)
struct DirectoryFrame {
    std::uint32_t state;
    std::uint32_t block_count;
    std::uint16_t next_block;
    char          filename[20];
    std::uint8_t  _padding[97];
    std::uint8_t  checksum;
};
static_assert(sizeof(DirectoryFrame) == FRAME_SIZE);
#pragma pack(pop)

} // namespace ps1

// ─────────────────────────────────────────────────────────────────────────────

class PS1MemoryCard final : public MemoryCard {
public:
    PS1MemoryCard()  = default;
    ~PS1MemoryCard() = default;

    bool load(const std::filesystem::path& path) override;
    bool save(const std::filesystem::path& path) override;

    // Inject a raw 128 KiB buffer directly (used by format handlers like DexDriveHandler).
    // source_path is recorded as the card's logical path for display purposes.
    bool loadFromBuffer(const std::array<std::uint8_t, ps1::CARD_SIZE>& buf,
                        const std::filesystem::path& source_path = {});

    std::vector<std::shared_ptr<SaveFile>> getSaves()                                   const override;
    bool importSave(const std::filesystem::path& path)                                        override;
    bool exportSave(std::size_t index, const std::filesystem::path& path)                     override;
    bool deleteSave(std::size_t index)                                                         override;

    MemoryCardType  getType()        const override { return MemoryCardType::PS1; }
    std::string     getDisplayName() const override { return "PS1 Memory Card (raw)"; }
    std::size_t     getCapacity()    const override { return ps1::NUM_SLOTS; }
    std::size_t     getUsedBlocks()  const override;

    const std::array<std::uint8_t, ps1::CARD_SIZE>& getRawCardData() const { return m_data; }

    static std::shared_ptr<PS1MemoryCard> createNew(const std::filesystem::path& path);

private:
    std::uint8_t calcChecksum(const std::uint8_t* data, std::size_t len) const;
    bool         validateHeader()                                          const;

    std::array<std::uint8_t, ps1::CARD_SIZE> m_data{};
};

} // namespace cyan
