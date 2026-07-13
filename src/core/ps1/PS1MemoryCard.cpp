#include "PS1MemoryCard.hpp"
#include "PS1SaveFile.hpp"
#include "core/formats/DexDriveHandler.hpp"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iconv.h>
#include <iostream>
#include <numeric>

extern bool g_verbose;

#define PS1_IMPORT_ERR(msg) \
    if (g_verbose) { std::cerr << "[PS1 Import] " << msg << '\n' << std::flush; }

namespace cyan {

// ─── Shift-JIS → UTF-8 helper ────────────────────────────────────────────────
// PS1 save titles are stored as 64-byte null-terminated Shift-JIS strings.

static std::string shiftjis_to_utf8(const char* src, std::size_t max_len) {
    const std::size_t src_len = ::strnlen(src, max_len);
    if (src_len == 0) return {};

    iconv_t cd = ::iconv_open("UTF-8", "SHIFT_JIS");
    if (cd == reinterpret_cast<iconv_t>(-1)) {
        std::string s(src, src_len);
        for (auto& c : s)
            if (static_cast<unsigned char>(c) > 0x7Fu) c = '?';
        return s;
    }

    std::string result(src_len * 3, '\0'); // UTF-8 worst-case: 3× expansion
    char*       in_ptr   = const_cast<char*>(src);
    char*       out_ptr  = result.data();
    std::size_t in_left  = src_len;
    std::size_t out_left = result.size();

    ::iconv(cd, &in_ptr, &in_left, &out_ptr, &out_left);
    ::iconv_close(cd);

    result.resize(result.size() - out_left);
    return result;
}

// ─── load / save / loadFromBuffer ────────────────────────────────────────────

bool PS1MemoryCard::load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    file.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    if (size != ps1::CARD_SIZE) return false;

    file.read(reinterpret_cast<char*>(m_data.data()), ps1::CARD_SIZE);
    if (!file) return false;

    if (!validateHeader()) return false;

    m_path   = path;
    m_loaded = true;
    return true;
}

bool PS1MemoryCard::save(const std::filesystem::path& path) {
    if (!m_loaded) return false;

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;

    file.write(reinterpret_cast<const char*>(m_data.data()), ps1::CARD_SIZE);
    return file.good();
}

bool PS1MemoryCard::loadFromBuffer(const std::array<std::uint8_t, ps1::CARD_SIZE>& buf,
                                    const std::filesystem::path& source_path) {
    m_data = buf;
    if (!validateHeader()) return false;

    m_path   = source_path;
    m_loaded = true;
    return true;
}

// ─── getSaves: parse all 15 directory slots ───────────────────────────────────
//
// Layout recap (0-based slot index s):
//   Directory frame : byte offset (s + 1) * FRAME_SIZE
//   Data block start: byte offset (64 + s * FRAMES_PER_BLOCK) * FRAME_SIZE
//                   = (s + 1) * BLOCK_SIZE   (= 0x2000 for s=0)
//
// This assumes sequential block allocation, which holds for the vast majority
// of real-world memory cards.  Following the next_block link chain is a
// future improvement.

std::vector<std::shared_ptr<SaveFile>> PS1MemoryCard::getSaves() const {
    if (!m_loaded) return {};

    std::vector<std::shared_ptr<SaveFile>> results;
    results.reserve(ps1::NUM_SLOTS);

    for (std::size_t s = 0; s < ps1::NUM_SLOTS; ++s) {
        const auto* frame = reinterpret_cast<const ps1::DirectoryFrame*>(
            m_data.data() + ps1::FRAME_SIZE * (s + 1));

        // Only emit an entry for the first block of each save.
        if ((frame->state & 0xFFu) != ps1::SLOT_FIRST) continue;

        // ── Game ID ──────────────────────────────────────────────────────────
        // filename field: 20 bytes, null-terminated.
        // Format: 2-char region code (BA/BE/BI) + product code (e.g. "SLUS-00568")
        const std::string filename(frame->filename,
                                   ::strnlen(frame->filename, 20));

        // ── Region ───────────────────────────────────────────────────────────
        Region region = Region::Unknown;
        if (filename.size() >= 2) {
            const char r1 = filename[0], r2 = filename[1];
            if (r1 == 'B') {
                if      (r2 == 'A') region = Region::NTSC_US;
                else if (r2 == 'E') region = Region::PAL;
                else if (r2 == 'I' || r2 == 'J') region = Region::NTSC_JP;
            }
        }

        // ── Title (Shift-JIS → UTF-8) ─────────────────────────────────────
        // Icon header for slot s starts at byte (s + 1) * BLOCK_SIZE.
        // Title is 64 bytes at ICON_TITLE_OFF (0x04) within that header.
        const std::size_t block_byte = static_cast<std::size_t>(ps1::BLOCK_SIZE) * (s + 1u);
        std::string title;
        if (block_byte + ps1::ICON_TITLE_OFF + 64u <= ps1::CARD_SIZE) {
            const char* title_raw = reinterpret_cast<const char*>(
                m_data.data() + block_byte + ps1::ICON_TITLE_OFF);
            title = shiftjis_to_utf8(title_raw, 64);
        }
        if (title.empty()) title = filename;

        // ── Raw block data (follows the next_block linked list) ───────────
        // Slot i's data block is at (i+1)*BLOCK_SIZE.  next_block==0xFFFF marks end.
        // A guard counter prevents infinite loops on corrupt cards.
        std::vector<std::uint8_t> raw_data;
        {
            std::uint16_t cur   = static_cast<std::uint16_t>(s);
            int           guard = 0;
            while (guard++ < static_cast<int>(ps1::NUM_SLOTS)) {
                if (cur >= ps1::NUM_SLOTS) break;
                const std::size_t blk_off = ps1::BLOCK_SIZE * (cur + 1u);
                if (blk_off + ps1::BLOCK_SIZE > ps1::CARD_SIZE) break;
                raw_data.insert(raw_data.end(),
                    m_data.begin() + static_cast<std::ptrdiff_t>(blk_off),
                    m_data.begin() + static_cast<std::ptrdiff_t>(blk_off + ps1::BLOCK_SIZE));
                const auto* dir = reinterpret_cast<const ps1::DirectoryFrame*>(
                    m_data.data() + ps1::FRAME_SIZE * (cur + 1u));
                if (dir->next_block == 0xFFFFu) break;
                cur = dir->next_block;
            }
        }

        results.push_back(std::make_shared<PS1SaveFile>(
            filename, title, region,
            static_cast<std::size_t>(frame->block_count),
            std::move(raw_data),
            s));
    }

    return results;
}

// ─── Stubs ────────────────────────────────────────────────────────────────────

bool PS1MemoryCard::importSave(const std::filesystem::path& path) {
    if (!m_loaded) { PS1_IMPORT_ERR("Card not loaded."); return false; }

    // ── 1. Read file ──────────────────────────────────────────────────────────
    std::ifstream f(path, std::ios::binary);
    if (!f) { PS1_IMPORT_ERR("Cannot open file: " << path); return false; }

    f.seekg(0, std::ios::end);
    const std::size_t size = static_cast<std::size_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    PS1_IMPORT_ERR("File size: " << size << " bytes, path: " << path);

    // ── 2. Full card merge: raw 128 KB card or DexDrive .gme ─────────────────
    //   Both formats are merged save-by-save rather than imported as raw data.
    const std::string ext  = path.extension().string();
    const bool is_raw_card = (size == ps1::CARD_SIZE);
    const bool is_gme_card = (ext == ".gme");

    if (is_raw_card || is_gme_card) {
        PS1_IMPORT_ERR("Detected full PS1 card image — entering merge mode.");
        PS1MemoryCard src;
        bool loaded = false;

        if (is_gme_card) {
            // DexDriveHandler knows the real header size (0xF40 = 3904 bytes).
            loaded = formats::DexDriveHandler::importFrom(path, src);
        } else {
            // Raw .mcr / .ps1 card image.
            loaded = src.load(path);
        }

        if (!loaded) { PS1_IMPORT_ERR("Failed to parse source card: " << path); return false; }

        const auto src_saves = src.getSaves();
        PS1_IMPORT_ERR("Source card has " << src_saves.size() << " save(s).");
        bool any_ok = false;

        for (const auto& sv : src_saves) {
            const std::size_t slot_idx = sv->getSlotIndex();

            // Build an MCS file in a temp path and call importSave recursively.
            const std::string tmp_path =
                "/tmp/cyanmem_merge_" + std::to_string(slot_idx) + ".mcs";

            {
                std::ofstream tmp(tmp_path, std::ios::binary | std::ios::trunc);
                if (!tmp) { PS1_IMPORT_ERR("Cannot create temp MCS: " << tmp_path); continue; }

                // Write the 128-byte directory frame for this slot.
                const std::uint8_t* frame_ptr =
                    src.m_data.data() + ps1::FRAME_SIZE * (slot_idx + 1u);
                tmp.write(reinterpret_cast<const char*>(frame_ptr), ps1::FRAME_SIZE);

                // Write the raw block data.
                const auto& raw = sv->getRawData();
                tmp.write(reinterpret_cast<const char*>(raw.data()),
                          static_cast<std::streamsize>(raw.size()));
            }

            PS1_IMPORT_ERR("Merging slot " << slot_idx << " (" << sv->getGameID() << ")");
            if (importSave(std::filesystem::path(tmp_path))) any_ok = true;
            std::filesystem::remove(tmp_path);
        }

        PS1_IMPORT_ERR("Merge complete. Imported: " << (any_ok ? "at least one save" : "none"));
        return any_ok;
    }

    // ── 3. Detect single-save format (MCS or raw) ────────────────────────────
    bool        is_mcs = false;
    std::size_t N      = 0u;

    if (size > 128u && (size - 128u) % ps1::BLOCK_SIZE == 0u) {
        is_mcs = true;
        N      = (size - 128u) / ps1::BLOCK_SIZE;
        PS1_IMPORT_ERR("Format: MCS, blocks=" << N);
    } else if (size > 0u && size % ps1::BLOCK_SIZE == 0u) {
        is_mcs = false;
        N      = size / ps1::BLOCK_SIZE;
        PS1_IMPORT_ERR("Format: raw, blocks=" << N);
    } else {
        PS1_IMPORT_ERR("Unrecognised file size: " << size);
        return false;
    }

    std::vector<std::uint8_t> file_data(size);
    f.read(reinterpret_cast<char*>(file_data.data()), static_cast<std::streamsize>(size));
    if (!f) { PS1_IMPORT_ERR("Read failed after " << f.gcount() << " bytes."); return false; }

    // ── 4. Seamless overwrite: delete any existing save with same filename ────
    if (is_mcs) {
        ps1::DirectoryFrame probe{};
        std::memcpy(&probe, file_data.data(), sizeof(ps1::DirectoryFrame));
        const std::string incoming_id(probe.filename,
                                      ::strnlen(probe.filename, sizeof(probe.filename)));
        if (!incoming_id.empty()) {
            const auto existing = getSaves();
            for (std::size_t si = 0u; si < existing.size(); ++si) {
                if (existing[si]->getGameID() == incoming_id) {
                    PS1_IMPORT_ERR("Overwriting existing PS1 save: " << incoming_id);
                    if (!deleteSave(si)) {
                        PS1_IMPORT_ERR("Failed to delete existing save: " << incoming_id);
                        return false;
                    }
                    break;
                }
            }
        }
    }

    // ── 5. Collect free slots ─────────────────────────────────────────────────
    std::vector<std::size_t> free_slots;
    free_slots.reserve(ps1::NUM_SLOTS);
    for (std::size_t cur = 0u; cur < ps1::NUM_SLOTS; ++cur) {
        const auto* frame = reinterpret_cast<const ps1::DirectoryFrame*>(
            m_data.data() + ps1::FRAME_SIZE * (cur + 1u));
        const auto state = static_cast<std::uint8_t>(frame->state);
        if (state == ps1::SLOT_DELETED || state == ps1::SLOT_FREE)
            free_slots.push_back(cur);
    }
    PS1_IMPORT_ERR("Free slots: " << free_slots.size() << ", needed: " << N);
    if (free_slots.size() < N) {
        PS1_IMPORT_ERR("Not enough free slots on card.");
        return false;
    }

    // ── 6. Prepare the base directory frame ───────────────────────────────────
    ps1::DirectoryFrame base_frame{};
    if (is_mcs) {
        std::memcpy(&base_frame, file_data.data(), sizeof(ps1::DirectoryFrame));
    } else {
        std::memset(&base_frame, 0, sizeof(ps1::DirectoryFrame));
        base_frame.block_count = static_cast<std::uint32_t>(N * ps1::BLOCK_SIZE);
    }

    // ── 7. Write data blocks and directory frames ─────────────────────────────
    const std::size_t payload_offset = is_mcs ? 128u : 0u;

    for (std::size_t i = 0u; i < N; ++i) {
        const std::size_t   cur_slot  = free_slots[i];
        const std::uint16_t next_slot = (i == N - 1u)
            ? 0xFFFFu
            : static_cast<std::uint16_t>(free_slots[i + 1u]);

        const std::size_t card_off = ps1::BLOCK_SIZE * (cur_slot + 1u);
        const std::size_t src_off  = payload_offset + i * ps1::BLOCK_SIZE;
        std::memcpy(m_data.data() + card_off,
                    file_data.data() + src_off,
                    ps1::BLOCK_SIZE);

        auto* frame = reinterpret_cast<ps1::DirectoryFrame*>(
            m_data.data() + ps1::FRAME_SIZE * (cur_slot + 1u));

        // All linked frames carry the same filename/metadata as block 0;
        // only state, next_block, and checksum differ per block.
        *frame       = base_frame;
        frame->state = (i == 0u) ? ps1::SLOT_FIRST
                     : (i == N - 1u) ? ps1::SLOT_LAST
                     : ps1::SLOT_MIDDLE;
        frame->next_block = next_slot;
        frame->checksum   = calcChecksum(
            reinterpret_cast<const std::uint8_t*>(frame), 127u);
    }

    PS1_IMPORT_ERR("Import successful — saving card.");
    return save(m_path);
}

bool PS1MemoryCard::exportSave(std::size_t index, const std::filesystem::path& out_path) {
    if (!m_loaded) return false;
    const auto saves = getSaves();
    if (index >= saves.size()) return false;
    const auto& raw = saves[index]->getRawData();
    if (raw.empty()) return false;
    std::ofstream f(out_path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(raw.data()),
            static_cast<std::streamsize>(raw.size()));
    return f.good();
}

bool PS1MemoryCard::deleteSave(std::size_t index) {
    if (!m_loaded) return false;
    const auto saves = getSaves();
    if (index >= saves.size()) return false;

    std::uint16_t cur_slot = static_cast<std::uint16_t>(saves[index]->getSlotIndex());
    int guard = 0;
    while (cur_slot != 0xFFFFu && guard++ < static_cast<int>(ps1::NUM_SLOTS)) {
        const std::size_t frame_off = ps1::FRAME_SIZE * (static_cast<std::size_t>(cur_slot) + 1u);
        auto* frame = reinterpret_cast<ps1::DirectoryFrame*>(m_data.data() + frame_off);
        const std::uint16_t next_slot = frame->next_block;
        frame->state    = ps1::SLOT_DELETED;
        frame->checksum = calcChecksum(m_data.data() + frame_off, 127u);
        cur_slot = next_slot;
    }

    return save(m_path);
}

// ─── Helpers ──────────────────────────────────────────────────────────────────

std::size_t PS1MemoryCard::getUsedBlocks() const {
    if (!m_loaded) return 0;

    std::size_t used = 0;
    for (std::size_t i = 0; i < ps1::NUM_SLOTS; ++i) {
        const auto* frame = reinterpret_cast<const ps1::DirectoryFrame*>(
            m_data.data() + ps1::FRAME_SIZE * (i + 1));
        if ((frame->state & 0xFFu) == ps1::SLOT_FIRST)
            used += frame->block_count;
    }
    return used;
}

std::uint8_t PS1MemoryCard::calcChecksum(const std::uint8_t* data, std::size_t len) const {
    return std::reduce(data, data + len, std::uint8_t{0},
        [](std::uint8_t a, std::uint8_t b) { return static_cast<std::uint8_t>(a ^ b); });
}

bool PS1MemoryCard::validateHeader() const {
    return m_data[0] == 'M' && m_data[1] == 'C';
}

std::shared_ptr<PS1MemoryCard> PS1MemoryCard::createNew(const std::filesystem::path& path) {
    auto card = std::make_shared<PS1MemoryCard>();

    card->m_data.fill(0x00u);

    // Frame 0: "MC" magic + XOR checksum.
    card->m_data[0] = 'M';
    card->m_data[1] = 'C';
    card->m_data[127] = card->calcChecksum(card->m_data.data(), 127u); // 0x0E

    // Frames 1–15: directory entries, all free.
    for (std::size_t s = 0u; s < ps1::NUM_SLOTS; ++s) {
        const std::size_t frame_off = ps1::FRAME_SIZE * (s + 1u);
        auto* frame = reinterpret_cast<ps1::DirectoryFrame*>(
            card->m_data.data() + frame_off);
        frame->state      = ps1::SLOT_FREE;
        frame->next_block = 0xFFFFu;
        frame->checksum   = card->calcChecksum(
            reinterpret_cast<const std::uint8_t*>(frame), 127u);
    }

    // Frames 16–35: broken-sector list. Checksum of all-zero frame is 0x00,
    // already satisfied by the initial fill — no action needed.

    card->m_path   = path;
    card->m_loaded = true;
    if (!card->save(path)) return nullptr;
    return card;
}

} // namespace cyan
