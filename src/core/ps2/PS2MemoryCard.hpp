#pragma once

#include "core/MemoryCard.hpp"
#include <cstdint>
#include <vector>

namespace cyan {

// ─── PS2 memory card format constants ────────────────────────────────────────
//
// The PS2 memory card is an 8 MiB flash device exposed over a SIO2 interface.
// The on-card filesystem is similar to FAT with ECC.  PCSX2 stores a raw image
// of the flash (with ECC data) in a .ps2 file.
//
// Physical layout (1024 pages × 512-byte page + 16-byte ECC = 528 bytes/page):
//   - Superblock  : page 0
//   - Root dir    : typically page 8
//   - Each game   : one directory entry, e.g.  "BASLUS-20622"
//   - Each save   : files inside that directory
//
// For a .ps2 image from PCSX2 the ECC bytes may be stripped (512-byte pages).

namespace ps2 {

static constexpr std::uint32_t PAGE_SIZE_RAW  = 512u;
static constexpr std::uint32_t PAGE_SIZE_ECC  = 528u;  // + 16 bytes ECC
static constexpr std::uint32_t PAGES_PER_CARD = 16'384u;
static constexpr std::uint32_t CARD_SIZE_RAW  = PAGE_SIZE_RAW * PAGES_PER_CARD; // 8 MiB
static constexpr std::uint32_t CARD_SIZE_ECC  = PAGE_SIZE_ECC * PAGES_PER_CARD;

static constexpr std::uint32_t CLUSTER_SIZE        = 1024u; // 2 pages
static constexpr std::uint16_t PAGES_PER_CLUSTER   = 2u;
static constexpr std::uint16_t PAGES_PER_BLOCK      = 16u;  // erase-block granularity
static constexpr std::uint32_t FAT_ENTRY_FREE = 0x7FFF'FFFFu;
static constexpr std::uint32_t FAT_ENTRY_END  = 0xFFFF'FFFFu;

// Superblock magic
static constexpr char SUPERBLOCK_MAGIC[] = "Sony PS2 Memory Card Format ";

#pragma pack(push, 1)
struct Superblock {
    char          magic[28];
    char          version[12];
    std::uint16_t page_size;         // bytes per page (normally 512)
    std::uint16_t pages_per_cluster; // normally 2
    std::uint16_t pages_per_block;   // erase-block granularity
    std::uint16_t _reserved;
    std::uint32_t clusters_per_card;
    std::uint32_t alloc_offset;      // first allocatable cluster
    std::uint32_t alloc_end;         // last allocatable cluster + 1
    std::uint32_t rootdir_cluster;   // cluster index of root directory
    std::uint32_t backup_block1;
    std::uint32_t backup_block2;
    std::uint32_t _reserved2[2];     // 8 bytes of reserved padding → ifc_list at 0x50
    std::uint32_t ifc_list[32];      // indirect FAT cluster list (32-bit cluster indices)
    std::uint32_t bad_block_list[32];
    std::uint8_t  card_type;
    std::uint8_t  card_flags;
    std::uint16_t _padding;          // pad to exactly 340 bytes
};
static_assert(sizeof(Superblock) == 340, "Superblock must be exactly 340 bytes");
#pragma pack(pop)

#pragma pack(push, 1)
struct DirEntry {
    std::uint16_t mode;            // 0x00
    std::uint16_t _reserved;       // 0x02
    std::uint32_t length;          // 0x04
    std::uint8_t  created[8];      // 0x08
    std::uint32_t cluster;         // 0x10
    std::uint32_t dir_entry;       // 0x14
    std::uint8_t  modified[8];     // 0x18
    std::uint32_t attr;            // 0x20
    std::uint8_t  _padding1[28];   // 0x24
    char          name[32];        // 0x40
    std::uint8_t  _padding2[416];  // 0x60
};
static_assert(sizeof(DirEntry) == 512, "DirEntry must be exactly 512 bytes");
#pragma pack(pop)

// Mode bits
static constexpr std::uint16_t MODE_DIR  = 0x0020u; // entry is a directory

} // namespace ps2

// ─────────────────────────────────────────────────────────────────────────────

class PS2MemoryCard final : public MemoryCard {
public:
    PS2MemoryCard()  = default;
    ~PS2MemoryCard() = default;

    bool load(const std::filesystem::path& path) override;
    bool save(const std::filesystem::path& path) override;

    std::vector<std::shared_ptr<SaveFile>> getSaves()                                         const override;
    bool importSave(const std::filesystem::path& path)                                              override;
    bool exportSave(std::size_t index, const std::filesystem::path& path)                           override;
    bool deleteSave(std::size_t index)                                                               override;

    MemoryCardType  getType()        const override { return MemoryCardType::PS2; }
    std::string     getDisplayName() const override { return "PS2 Memory Card (.ps2)"; }
    std::size_t     getCapacity()    const override;
    std::size_t     getUsedBlocks()  const override;

    static std::shared_ptr<PS2MemoryCard> createNew(const std::filesystem::path& path);

private:
    bool parseSuperblock();
    void buildFAT();
    void flushFAT();
    std::vector<std::uint8_t> readCluster (std::uint32_t cluster_idx) const;
    std::vector<std::uint8_t> readChain   (std::uint32_t start_cluster) const;
    void                      writeCluster (std::uint32_t cluster_idx,   const std::vector<std::uint8_t>& data);
    void                      writeChain   (std::uint32_t start_cluster, const std::vector<std::uint8_t>& data);
    void                      freeChain    (std::uint32_t start_cluster);
    std::uint32_t             allocateChain(std::size_t count);

    std::vector<std::uint8_t>  m_data;
    ps2::Superblock            m_superblock{};
    bool                       m_has_ecc{false};
    std::vector<std::uint32_t> m_fat; // one entry per cluster; built once after load
};

} // namespace cyan
