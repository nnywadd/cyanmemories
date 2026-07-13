#include "PS2MemoryCard.hpp"
#include "PS2SaveFile.hpp"
#include "ArchiveExtractor.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iconv.h>
#include <iostream>

extern bool g_verbose;

// Logs to stderr when -v is passed on the command line.
// msg may contain << chained expressions (e.g. "file: " << name).
#define IMPORT_ERR(msg) \
    if (g_verbose) { std::cerr << "[PS2 Import Error] " << msg << '\n' << std::flush; }

namespace cyan {

// ─── Shift-JIS → UTF-8 (same logic as PS1MemoryCard.cpp) ────────────────────
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

    std::string result(src_len * 3, '\0');
    char*       in_ptr   = const_cast<char*>(src);
    char*       out_ptr  = result.data();
    std::size_t in_left  = src_len;
    std::size_t out_left = result.size();

    ::iconv(cd, &in_ptr, &in_left, &out_ptr, &out_left);
    ::iconv_close(cd);

    result.resize(result.size() - out_left);
    return result;
}

bool PS2MemoryCard::load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    file.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    if (size != ps2::CARD_SIZE_RAW && size != ps2::CARD_SIZE_ECC) return false;

    m_has_ecc = (size == ps2::CARD_SIZE_ECC);
    m_data.resize(size);
    file.read(reinterpret_cast<char*>(m_data.data()), static_cast<std::streamsize>(size));
    if (!file) return false;

    if (!parseSuperblock()) return false;

    buildFAT();

    m_path   = path;
    m_loaded = true;
    return true;
}

bool PS2MemoryCard::save(const std::filesystem::path& path) {
    if (!m_loaded) return false;

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;

    file.write(reinterpret_cast<const char*>(m_data.data()),
               static_cast<std::streamsize>(m_data.size()));
    return file.good();
}

// ─── FAT construction ─────────────────────────────────────────────────────────
//
// Three-level indirection:
//   superblock.ifc_list[i]  → Indirect FAT Cluster (IFC)
//   IFC data[j]             → Direct FAT Cluster (DFC)
//   DFC data[k]             → actual FAT entry for cluster (i*N*M + j*M + k)
//
// Each FAT entry is a uint32_t; the top bit is the allocation flag.
// We collect all entries into m_fat (one per cluster) until clusters_per_card is reached.

void PS2MemoryCard::buildFAT() {
    m_fat.clear();
    const std::size_t max_clusters = m_superblock.clusters_per_card;
    if (max_clusters == 0u) return;
    m_fat.reserve(max_clusters);

    const std::size_t entries_per_cluster =
        (m_superblock.pages_per_cluster ? m_superblock.pages_per_cluster : 2u)
        * ps2::PAGE_SIZE_RAW / sizeof(std::uint32_t); // typically 256

    for (const auto ifc_idx : m_superblock.ifc_list) {
        if (m_fat.size() >= max_clusters) break;
        if (ifc_idx == 0u ||
            ifc_idx == ps2::FAT_ENTRY_FREE ||
            ifc_idx == ps2::FAT_ENTRY_END) continue;

        // Read the IFC cluster → array of direct FAT cluster indices.
        const auto ifc_data = readCluster(ifc_idx);
        const auto* dfc_ptrs = reinterpret_cast<const std::uint32_t*>(ifc_data.data());
        const std::size_t n_dfc = ifc_data.size() / sizeof(std::uint32_t);

        for (std::size_t d = 0u; d < n_dfc && m_fat.size() < max_clusters; ++d) {
            const std::uint32_t dfc_idx = dfc_ptrs[d];
            if (dfc_idx == 0u ||
                dfc_idx == ps2::FAT_ENTRY_FREE ||
                dfc_idx == ps2::FAT_ENTRY_END) {
                // Empty slot — fill with FAT_ENTRY_FREE placeholders so indices stay aligned.
                const std::size_t fill = std::min(entries_per_cluster,
                                                  max_clusters - m_fat.size());
                m_fat.insert(m_fat.end(), fill, ps2::FAT_ENTRY_FREE);
                continue;
            }

            // Read the DFC cluster → actual FAT entries.
            const auto dfc_data = readCluster(dfc_idx);
            const auto* fat_entries = reinterpret_cast<const std::uint32_t*>(dfc_data.data());
            const std::size_t n_entries = dfc_data.size() / sizeof(std::uint32_t);
            for (std::size_t e = 0u; e < n_entries && m_fat.size() < max_clusters; ++e)
                m_fat.push_back(fat_entries[e]);
        }
    }
}

// ─── Chain reader ─────────────────────────────────────────────────────────────

std::vector<std::uint8_t> PS2MemoryCard::readChain(std::uint32_t start_cluster) const {
    std::vector<std::uint8_t> result;
    if (m_fat.empty()) return result;

    std::uint32_t cur   = start_cluster;
    const int     limit = static_cast<int>(m_fat.size()) + 1;
    for (int guard = 0; guard < limit; ++guard) {
        if (cur >= static_cast<std::uint32_t>(m_fat.size())) break;
        auto chunk = readCluster(cur + m_superblock.alloc_offset);
        result.insert(result.end(), chunk.begin(), chunk.end());

        const std::uint32_t raw_next = m_fat[cur];
        if (raw_next == ps2::FAT_ENTRY_END ||
            raw_next == ps2::FAT_ENTRY_FREE ||
            raw_next == 0u) break;
        cur = raw_next & 0x7FFF'FFFFu;
    }
    return result;
}

// ─── Cluster reader ───────────────────────────────────────────────────────────

std::vector<std::uint8_t> PS2MemoryCard::readCluster(std::uint32_t cluster_idx) const {
    const std::uint32_t ppc         = m_superblock.pages_per_cluster
                                      ? m_superblock.pages_per_cluster : 2u;
    const std::uint32_t start_page  = cluster_idx * ppc;
    const std::uint32_t page_stride = m_has_ecc ? ps2::PAGE_SIZE_ECC : ps2::PAGE_SIZE_RAW;

    std::vector<std::uint8_t> buf(ppc * ps2::PAGE_SIZE_RAW, 0u);
    for (std::uint32_t i = 0u; i < ppc; ++i) {
        const std::size_t phys_off = static_cast<std::size_t>(start_page + i) * page_stride;
        if (phys_off + ps2::PAGE_SIZE_RAW > m_data.size()) break;
        std::memcpy(buf.data() + i * ps2::PAGE_SIZE_RAW,
                    m_data.data() + phys_off,
                    ps2::PAGE_SIZE_RAW);
    }
    return buf;
}

// ─── Write helpers ────────────────────────────────────────────────────────────

void PS2MemoryCard::writeCluster(std::uint32_t cluster_idx,
                                  const std::vector<std::uint8_t>& data) {
    const std::uint32_t ppc         = m_superblock.pages_per_cluster
                                      ? m_superblock.pages_per_cluster : 2u;
    const std::uint32_t start_page  = cluster_idx * ppc;
    const std::uint32_t page_stride = m_has_ecc ? ps2::PAGE_SIZE_ECC : ps2::PAGE_SIZE_RAW;

    for (std::uint32_t i = 0u; i < ppc; ++i) {
        const std::size_t phys_off = static_cast<std::size_t>(start_page + i) * page_stride;
        const std::size_t src_off  = static_cast<std::size_t>(i) * ps2::PAGE_SIZE_RAW;
        if (phys_off + ps2::PAGE_SIZE_RAW > m_data.size()) break;
        if (src_off  + ps2::PAGE_SIZE_RAW > data.size())   break;
        std::memcpy(m_data.data() + phys_off, data.data() + src_off, ps2::PAGE_SIZE_RAW);
        // ECC bytes at phys_off + PAGE_SIZE_RAW are left untouched.
    }
}

void PS2MemoryCard::writeChain(std::uint32_t start_cluster,
                                const std::vector<std::uint8_t>& data) {
    if (m_fat.empty()) return;

    std::uint32_t cur    = start_cluster;
    std::size_t   offset = 0u;
    const int     limit  = static_cast<int>(m_fat.size()) + 1;

    for (int guard = 0; guard < limit; ++guard) {
        if (cur >= static_cast<std::uint32_t>(m_fat.size())) break;

        std::vector<std::uint8_t> chunk(ps2::CLUSTER_SIZE, 0u);
        if (offset < data.size()) {
            const std::size_t copy_len =
                std::min(static_cast<std::size_t>(ps2::CLUSTER_SIZE),
                         data.size() - offset);
            std::memcpy(chunk.data(), data.data() + offset, copy_len);
        }
        writeCluster(cur + m_superblock.alloc_offset, chunk);
        offset += ps2::CLUSTER_SIZE;

        const std::uint32_t raw_next = m_fat[cur];
        if (raw_next == ps2::FAT_ENTRY_END  ||
            raw_next == ps2::FAT_ENTRY_FREE ||
            raw_next == 0u) break;
        cur = raw_next & 0x7FFF'FFFFu;
    }
}

void PS2MemoryCard::freeChain(std::uint32_t start_cluster) {
    if (m_fat.empty()) return;

    std::uint32_t cur   = start_cluster;
    const int     limit = static_cast<int>(m_fat.size()) + 1;

    for (int guard = 0; guard < limit; ++guard) {
        if (cur >= static_cast<std::uint32_t>(m_fat.size())) break;
        const std::uint32_t raw_next = m_fat[cur]; // read before overwriting
        m_fat[cur] = ps2::FAT_ENTRY_FREE;
        if (raw_next == ps2::FAT_ENTRY_END  ||
            raw_next == ps2::FAT_ENTRY_FREE ||
            raw_next == 0u) break;
        cur = raw_next & 0x7FFF'FFFFu;
    }
}

void PS2MemoryCard::flushFAT() {
    const std::size_t entries_per_cluster =
        (m_superblock.pages_per_cluster ? m_superblock.pages_per_cluster : 2u)
        * ps2::PAGE_SIZE_RAW / sizeof(std::uint32_t);

    const std::size_t max_clusters = m_superblock.clusters_per_card;
    std::size_t fat_idx = 0u;

    for (const auto ifc_idx : m_superblock.ifc_list) {
        if (fat_idx >= max_clusters) break;
        // Mirror buildFAT: skip invalid IFC entries without advancing fat_idx.
        if (ifc_idx == 0u ||
            ifc_idx == ps2::FAT_ENTRY_FREE ||
            ifc_idx == ps2::FAT_ENTRY_END) continue;

        const auto        ifc_data  = readCluster(ifc_idx);
        const auto*       dfc_ptrs  = reinterpret_cast<const std::uint32_t*>(ifc_data.data());
        const std::size_t n_dfc     = ifc_data.size() / sizeof(std::uint32_t);

        for (std::size_t d = 0u; d < n_dfc && fat_idx < max_clusters; ++d) {
            const std::uint32_t dfc_idx   = dfc_ptrs[d];
            const std::size_t   n_entries =
                std::min(entries_per_cluster, max_clusters - fat_idx);

            if (dfc_idx == 0u ||
                dfc_idx == ps2::FAT_ENTRY_FREE ||
                dfc_idx == ps2::FAT_ENTRY_END) {
                fat_idx += n_entries; // empty DFC slot — no cluster to write
                continue;
            }

            // Pack updated m_fat entries into a byte buffer and write to the DFC cluster.
            std::vector<std::uint8_t> buf(entries_per_cluster * sizeof(std::uint32_t), 0u);
            for (std::size_t e = 0u; e < n_entries; ++e) {
                const std::uint32_t entry = (fat_idx + e < m_fat.size())
                    ? m_fat[fat_idx + e] : ps2::FAT_ENTRY_FREE;
                std::memcpy(buf.data() + e * sizeof(std::uint32_t),
                            &entry, sizeof(std::uint32_t));
            }
            writeCluster(dfc_idx, buf);
            fat_idx += n_entries;
        }
    }
}

// ─── getSaves ─────────────────────────────────────────────────────────────────

std::vector<std::shared_ptr<SaveFile>> PS2MemoryCard::getSaves() const {
    if (!m_loaded || m_fat.empty()) return {};

    // Read the full root directory by following the FAT chain.
    const auto root_data = readChain(m_superblock.rootdir_cluster);
    const std::size_t n_root = root_data.size() / sizeof(ps2::DirEntry);

    std::vector<std::shared_ptr<SaveFile>> results;

    for (std::size_t i = 0u; i < n_root; ++i) {
        const auto* rentry = reinterpret_cast<const ps2::DirEntry*>(
            root_data.data() + i * sizeof(ps2::DirEntry));

        if (rentry->mode == 0u) continue;
        if (!(rentry->mode & ps2::MODE_DIR)) continue;

        const std::string name(rentry->name, ::strnlen(rentry->name, sizeof(rentry->name)));
        if (name == "." || name == "..") continue;

        // Default title = directory name; overridden if icon.sys is found.
        std::string title = name;

        if (rentry->cluster < static_cast<std::uint32_t>(m_fat.size())) {
            // Read the game save directory and look for icon.sys.
            const auto game_dir = readChain(rentry->cluster);
            const std::size_t n_game = game_dir.size() / sizeof(ps2::DirEntry);

            for (std::size_t j = 0u; j < n_game; ++j) {
                const auto* fentry = reinterpret_cast<const ps2::DirEntry*>(
                    game_dir.data() + j * sizeof(ps2::DirEntry));

                if (fentry->mode == 0u) continue;
                if (fentry->mode & ps2::MODE_DIR) continue; // skip sub-dirs

                const std::string fname(fentry->name,
                                        ::strnlen(fentry->name, sizeof(fentry->name)));
                if (fname != "icon.sys") continue;

                // Read icon.sys data; Shift-JIS title is 68 bytes at offset 0xC0.
                // (0x40 contains 3D lighting matrices for the PS2 OSD engine.)
                if (fentry->cluster < static_cast<std::uint32_t>(m_fat.size())) {
                    const auto icon_data = readChain(fentry->cluster);
                    constexpr std::size_t TITLE_OFF = 0xC0u;
                    constexpr std::size_t TITLE_LEN = 68u;
                    if (icon_data.size() >= TITLE_OFF + TITLE_LEN) {
                        const char* raw = reinterpret_cast<const char*>(
                            icon_data.data() + TITLE_OFF);
                        std::string decoded = shiftjis_to_utf8(raw, TITLE_LEN);
                        if (!decoded.empty()) {
                            std::replace(decoded.begin(), decoded.end(), '\n', ' ');
                            std::replace(decoded.begin(), decoded.end(), '\r', ' ');
                            title = std::move(decoded);
                        }
                    }
                }
                break;
            }
        }

        results.push_back(std::make_shared<PS2SaveFile>(name, title));
    }

    return results;
}

std::uint32_t PS2MemoryCard::allocateChain(std::size_t count) {
    if (count == 0) return ps2::FAT_ENTRY_FREE;
    std::uint32_t head = ps2::FAT_ENTRY_FREE;
    std::uint32_t last = ps2::FAT_ENTRY_FREE;
    for (std::size_t i = 0; i < m_fat.size() && count > 0; ++i) {
        if ((m_fat[i] & 0x8000'0000u) == 0u || m_fat[i] == ps2::FAT_ENTRY_FREE) {
            if (head == ps2::FAT_ENTRY_FREE) {
                head = static_cast<std::uint32_t>(i);
            } else {
                m_fat[last] = static_cast<std::uint32_t>(i) | 0x8000'0000u;
            }
            last = static_cast<std::uint32_t>(i);
            m_fat[last] = ps2::FAT_ENTRY_END;
            count--;
        }
    }
    if (count > 0) {
        if (head != ps2::FAT_ENTRY_FREE) freeChain(head);
        return ps2::FAT_ENTRY_FREE;
    }
    return head;
}

bool PS2MemoryCard::importSave(const std::filesystem::path& path) {
    if (!m_loaded || m_fat.empty()) { IMPORT_ERR("Card not loaded or FAT empty."); return false; }

    std::filesystem::path              import_src = path;
    std::optional<std::filesystem::path> temp_dir;

    if (std::filesystem::is_regular_file(path)) {
        temp_dir = ArchiveExtractor::extractArchive(path);
        if (!temp_dir) { IMPORT_ERR("Archive extraction failed for: " << path); return false; }
        import_src = *temp_dir;
    }

    auto cleanup_temp = [&]() {
        if (temp_dir) {
            std::error_code ec;
            std::filesystem::remove_all(temp_dir->parent_path(), ec);
        }
    };

    if (!std::filesystem::is_directory(import_src)) {
        IMPORT_ERR("Path is not a directory: " << import_src);
        cleanup_temp();
        return false;
    }
    const std::string game_id = import_src.filename().string();

    // ── Read mutable root directory ───────────────────────────────────────────
    auto root_data = readChain(m_superblock.rootdir_cluster);

    // ── Delete existing save if present (seamless overwrite) ─────────────────
    {
        const auto existing = getSaves();
        for (std::size_t si = 0u; si < existing.size(); ++si) {
            if (existing[si]->getGameID() == game_id) {
                IMPORT_ERR("Overwriting existing PS2 save: " << game_id);
                if (!deleteSave(si)) {
                    IMPORT_ERR("Failed to delete existing save for overwrite: " << game_id);
                    cleanup_temp();
                    return false;
                }
                // Reload root data after deletion so free slot scan is current.
                root_data = readChain(m_superblock.rootdir_cluster);
                break;
            }
        }
    }

    // ── Scan for a free root directory entry ──────────────────────────────────
    std::size_t free_idx = root_data.size() / sizeof(ps2::DirEntry); // sentinel
    {
        const std::size_t n = root_data.size() / sizeof(ps2::DirEntry);
        for (std::size_t i = 0u; i < n; ++i) {
            const auto* e = reinterpret_cast<const ps2::DirEntry*>(
                root_data.data() + i * sizeof(ps2::DirEntry));
            if (e->mode == 0u && free_idx == root_data.size() / sizeof(ps2::DirEntry))
                free_idx = i;
        }
    }

    // Track all allocated chains and root-extension state for rollback.
    std::vector<std::uint32_t> alloc_chains;
    std::uint32_t last_root_cluster  = ps2::FAT_ENTRY_FREE;
    std::uint32_t prev_last_root_fat = ps2::FAT_ENTRY_END;

    auto rollback = [&]() {
        for (auto c : alloc_chains)
            if (c != ps2::FAT_ENTRY_FREE) freeChain(c);
        if (last_root_cluster != ps2::FAT_ENTRY_FREE)
            m_fat[last_root_cluster] = prev_last_root_fat;
    };

    // ── Expand root directory if no free slot ─────────────────────────────────
    if (free_idx == root_data.size() / sizeof(ps2::DirEntry)) {
        // Find the last cluster in the root chain.
        std::uint32_t cur = m_superblock.rootdir_cluster;
        while (cur < static_cast<std::uint32_t>(m_fat.size())) {
            const std::uint32_t next = m_fat[cur];
            if (next == ps2::FAT_ENTRY_END || next == ps2::FAT_ENTRY_FREE || next == 0u) {
                last_root_cluster  = cur;
                prev_last_root_fat = m_fat[cur];
                break;
            }
            cur = next & 0x7FFF'FFFFu;
        }

        const std::uint32_t new_cluster = allocateChain(1u);
        if (new_cluster == ps2::FAT_ENTRY_FREE) { IMPORT_ERR("Failed to allocate cluster for root directory expansion."); cleanup_temp(); return false; }
        alloc_chains.push_back(new_cluster);
        if (last_root_cluster != ps2::FAT_ENTRY_FREE)
            m_fat[last_root_cluster] = new_cluster | 0x8000'0000u;

        free_idx = root_data.size() / sizeof(ps2::DirEntry);
        root_data.resize(root_data.size() + ps2::CLUSTER_SIZE, 0u);
    }

    // ── Collect host files ────────────────────────────────────────────────────
    std::vector<std::filesystem::directory_entry> host_files;
    for (const auto& entry : std::filesystem::directory_iterator(import_src))
        if (entry.is_regular_file())
            host_files.push_back(entry);

    const std::size_t num_files = host_files.size();

    // ── Allocate cluster chains for each file ─────────────────────────────────
    std::vector<std::uint32_t> file_chains(num_files, ps2::FAT_ENTRY_FREE);
    std::vector<std::size_t>   file_sizes (num_files, 0u);
    for (std::size_t i = 0u; i < num_files; ++i) {
        file_sizes[i] = std::filesystem::file_size(host_files[i].path());
        const std::size_t clusters =
            (file_sizes[i] + ps2::CLUSTER_SIZE - 1u) / ps2::CLUSTER_SIZE;
        if (clusters > 0u) {
            file_chains[i] = allocateChain(clusters);
            if (file_chains[i] == ps2::FAT_ENTRY_FREE) { IMPORT_ERR("Failed to allocate cluster chain for file: " << host_files[i].path().filename().string()); rollback(); cleanup_temp(); return false; }
            alloc_chains.push_back(file_chains[i]);
        }
    }

    // ── Allocate game directory cluster chain ─────────────────────────────────
    const std::size_t n_dir_entries = 2u + num_files;
    const std::size_t dir_clusters  =
        (n_dir_entries * sizeof(ps2::DirEntry) + ps2::CLUSTER_SIZE - 1u) / ps2::CLUSTER_SIZE;
    const std::uint32_t game_dir_cluster = allocateChain(dir_clusters > 0u ? dir_clusters : 1u);
    if (game_dir_cluster == ps2::FAT_ENTRY_FREE) { IMPORT_ERR("Failed to allocate cluster chain for game directory: " << game_id); rollback(); cleanup_temp(); return false; }
    alloc_chains.push_back(game_dir_cluster);

    // ── Build game directory buffer ───────────────────────────────────────────
    std::vector<ps2::DirEntry> game_dir(n_dir_entries); // zero-initialised

    // "." entry
    {
        auto& e = game_dir[0];
        std::strncpy(e.name, ".", sizeof(e.name) - 1u);
        e.mode    = 0x8427u;
        e.length  = static_cast<std::uint32_t>(n_dir_entries);
        e.cluster = game_dir_cluster;
    }
    // ".." entry
    {
        auto& e = game_dir[1];
        std::strncpy(e.name, "..", sizeof(e.name) - 1u);
        e.mode    = 0x8427u;
        e.length  = static_cast<std::uint32_t>(root_data.size() / sizeof(ps2::DirEntry));
        e.cluster = m_superblock.rootdir_cluster;
    }
    // File entries
    for (std::size_t i = 0u; i < num_files; ++i) {
        const std::size_t fsize = file_sizes[i];

        // Read file content and pad to CLUSTER_SIZE multiple.
        std::vector<std::uint8_t> content(fsize, 0u);
        {
            std::ifstream f(host_files[i].path(), std::ios::binary);
            if (f)
                f.read(reinterpret_cast<char*>(content.data()),
                       static_cast<std::streamsize>(fsize));
        }
        const std::size_t padded =
            ((fsize + ps2::CLUSTER_SIZE - 1u) / ps2::CLUSTER_SIZE) * ps2::CLUSTER_SIZE;
        content.resize(padded, 0u);
        if (file_chains[i] != ps2::FAT_ENTRY_FREE)
            writeChain(file_chains[i], content);

        auto& e = game_dir[2u + i];
        const std::string fname = host_files[i].path().filename().string();
        std::strncpy(e.name, fname.c_str(), sizeof(e.name) - 1u);
        e.mode    = 0x8417u;
        e.length  = static_cast<std::uint32_t>(fsize);
        e.cluster = file_chains[i];
    }

    // Serialize and write game directory.
    const std::size_t dir_raw    = n_dir_entries * sizeof(ps2::DirEntry);
    const std::size_t dir_padded =
        ((dir_raw + ps2::CLUSTER_SIZE - 1u) / ps2::CLUSTER_SIZE) * ps2::CLUSTER_SIZE;
    std::vector<std::uint8_t> game_dir_bytes(dir_padded, 0u);
    std::memcpy(game_dir_bytes.data(), game_dir.data(), dir_raw);
    writeChain(game_dir_cluster, game_dir_bytes);

    // ── Update root directory ─────────────────────────────────────────────────
    {
        auto* e = reinterpret_cast<ps2::DirEntry*>(
            root_data.data() + free_idx * sizeof(ps2::DirEntry));
        std::strncpy(e->name, game_id.c_str(), sizeof(e->name) - 1u);
        e->mode    = 0x8427u;
        e->length  = static_cast<std::uint32_t>(n_dir_entries);
        e->cluster = game_dir_cluster;
    }
    // Update root "." entry's entry count.
    {
        auto* dot = reinterpret_cast<ps2::DirEntry*>(root_data.data());
        dot->length = static_cast<std::uint32_t>(root_data.size() / sizeof(ps2::DirEntry));
    }
    writeChain(m_superblock.rootdir_cluster, root_data);

    // ── Finalise ──────────────────────────────────────────────────────────────
    flushFAT();
    const bool ok = save(m_path);
    cleanup_temp();
    return ok;
}

bool PS2MemoryCard::exportSave(std::size_t ui_index,
                                const std::filesystem::path& out_path) {
    if (!m_loaded || m_fat.empty()) return false;

    const auto saves = getSaves();
    if (ui_index >= saves.size()) return false;
    const std::string game_id = saves[ui_index]->getGameID();

    // Create the save sub-folder: out_path / game_id
    const auto save_dir = out_path / game_id;
    std::error_code ec;
    std::filesystem::create_directories(save_dir, ec);
    if (ec) return false;

    // Find the game directory entry in the root directory.
    const auto root_data = readChain(m_superblock.rootdir_cluster);
    const std::size_t n_root = root_data.size() / sizeof(ps2::DirEntry);

    for (std::size_t i = 0u; i < n_root; ++i) {
        const auto* rentry = reinterpret_cast<const ps2::DirEntry*>(
            root_data.data() + i * sizeof(ps2::DirEntry));

        if (rentry->mode == 0u) continue;
        if (!(rentry->mode & ps2::MODE_DIR)) continue;
        const std::string name(rentry->name, ::strnlen(rentry->name, sizeof(rentry->name)));
        if (name == "." || name == "..") continue;
        if (name != game_id) continue;

        if (rentry->cluster >= static_cast<std::uint32_t>(m_fat.size())) return false;

        const auto game_dir    = readChain(rentry->cluster);
        const std::size_t n_game = game_dir.size() / sizeof(ps2::DirEntry);

        for (std::size_t j = 0u; j < n_game; ++j) {
            const auto* fentry = reinterpret_cast<const ps2::DirEntry*>(
                game_dir.data() + j * sizeof(ps2::DirEntry));

            if (fentry->mode == 0u) continue;
            if (fentry->mode & ps2::MODE_DIR) continue; // skip . and ..

            const std::string fname(fentry->name,
                                    ::strnlen(fentry->name, sizeof(fentry->name)));
            if (fname.empty()) continue;
            if (fentry->cluster >= static_cast<std::uint32_t>(m_fat.size())) continue;

            auto file_data = readChain(fentry->cluster);
            // Trim cluster-aligned padding to the actual file size.
            file_data.resize(
                std::min<std::size_t>(file_data.size(), fentry->length));

            std::ofstream f(save_dir / fname, std::ios::binary | std::ios::trunc);
            if (!f) continue;
            f.write(reinterpret_cast<const char*>(file_data.data()),
                    static_cast<std::streamsize>(file_data.size()));
        }
        return true;
    }

    return false; // game directory not found in root
}

bool PS2MemoryCard::deleteSave(std::size_t ui_index) {
    if (!m_loaded || m_fat.empty()) return false;

    const auto saves = getSaves();
    if (ui_index >= saves.size()) return false;
    const std::string target_id = saves[ui_index]->getGameID();

    // Read the root directory into a mutable buffer for in-place editing.
    auto root_data = readChain(m_superblock.rootdir_cluster);
    const std::size_t n_root = root_data.size() / sizeof(ps2::DirEntry);

    bool found = false;
    for (std::size_t i = 0u; i < n_root; ++i) {
        auto* rentry = reinterpret_cast<ps2::DirEntry*>(
            root_data.data() + i * sizeof(ps2::DirEntry));

        if (rentry->mode == 0u) continue;
        if (!(rentry->mode & ps2::MODE_DIR)) continue;
        const std::string name(rentry->name, ::strnlen(rentry->name, sizeof(rentry->name)));
        if (name == "." || name == "..") continue;
        if (name != target_id) continue;

        // Free the cluster chains of all files inside the game directory.
        if (rentry->cluster < static_cast<std::uint32_t>(m_fat.size())) {
            const auto        game_dir = readChain(rentry->cluster);
            const std::size_t n_game   = game_dir.size() / sizeof(ps2::DirEntry);

            for (std::size_t j = 0u; j < n_game; ++j) {
                const auto* fentry = reinterpret_cast<const ps2::DirEntry*>(
                    game_dir.data() + j * sizeof(ps2::DirEntry));
                if (fentry->mode == 0u) continue;
                const std::string fname(fentry->name,
                                        ::strnlen(fentry->name, sizeof(fentry->name)));
                if (fname == "." || fname == "..") continue;
                if (fentry->cluster < static_cast<std::uint32_t>(m_fat.size()))
                    freeChain(fentry->cluster);
            }

            // Free the game directory's own cluster chain.
            freeChain(rentry->cluster);
        }

        // Mark the root entry as deleted.
        rentry->mode = 0u;
        found = true;
        break;
    }

    if (!found) return false;

    // Write the modified root directory back, flush the FAT, then persist.
    writeChain(m_superblock.rootdir_cluster, root_data);
    flushFAT();
    return save(m_path);
}

std::size_t PS2MemoryCard::getCapacity() const {
    if (!m_loaded) return 0;
    const std::uint32_t page_size = m_superblock.page_size ? m_superblock.page_size : 512u;
    const std::uint32_t ppc       = m_superblock.pages_per_cluster ? m_superblock.pages_per_cluster : 2u;
    return (ps2::CARD_SIZE_RAW / page_size) / ppc;
}

std::size_t PS2MemoryCard::getUsedBlocks() const {
    // A cluster is allocated when bit 31 of its FAT entry is set.
    // FREE = 0x7FFFFFFF (bit 31 clear), END = 0xFFFFFFFF (bit 31 set, allocated).
    std::size_t count = 0;
    for (const auto entry : m_fat)
        if ((entry & 0x8000'0000u) != 0u) ++count;
    return count;
}

std::shared_ptr<PS2MemoryCard> PS2MemoryCard::createNew(const std::filesystem::path& path) {
    auto card = std::shared_ptr<PS2MemoryCard>(new PS2MemoryCard());
    card->m_data.resize(ps2::CARD_SIZE_RAW, 0u);
    card->m_has_ecc = false;

    // ── 1. Superblock (physical cluster 0) ───────────────────────────────────
    auto* sb = reinterpret_cast<ps2::Superblock*>(card->m_data.data());
    std::memcpy(sb->magic,   "Sony PS2 Memory Card Format ", 28);
    std::memcpy(sb->version, "1.2.0.0",                       7);
    sb->page_size         = ps2::PAGE_SIZE_RAW;
    sb->pages_per_cluster = ps2::PAGES_PER_CLUSTER;
    sb->pages_per_block   = ps2::PAGES_PER_BLOCK;
    sb->clusters_per_card = 8192u;   // 8 MiB / 1024
    sb->alloc_offset      = 41u;     // clusters 0..40 reserved for metadata
    sb->alloc_end         = 8192u - 41u; // 8151 allocatable clusters
    sb->rootdir_cluster   = 0u;      // relative to alloc_offset
    sb->ifc_list[0]       = 8u;      // IFC at physical cluster 8
    sb->card_type         = 2u;      // PS2 standard
    sb->card_flags        = 0x52u;

    // ── 2. IFC (physical cluster 8) ──────────────────────────────────────────
    // 32 DFC cluster pointers: physical clusters 9..40
    auto* ifc = reinterpret_cast<std::uint32_t*>(
        card->m_data.data() + 8u * ps2::CLUSTER_SIZE);
    for (int i = 0; i < 32; ++i)
        ifc[i] = static_cast<std::uint32_t>(9 + i);

    // ── 3. DFC / FAT (physical clusters 9..40) ───────────────────────────────
    // 32 clusters × 256 entries = 8192 FAT entries (one per allocatable cluster)
    auto* dfc = reinterpret_cast<std::uint32_t*>(
        card->m_data.data() + 9u * ps2::CLUSTER_SIZE);
    dfc[0] = 0x8000'0001u;      // root dir: cluster 0 → cluster 1 (allocated chain)
    dfc[1] = ps2::FAT_ENTRY_END; // cluster 1 → end of root dir chain
    for (int i = 2; i < 8192; ++i)
        dfc[i] = ps2::FAT_ENTRY_FREE;

    // ── 4. Root directory (physical cluster 41 = alloc_offset + 0) ───────────
    auto* root = reinterpret_cast<ps2::DirEntry*>(
        card->m_data.data() + 41u * ps2::CLUSTER_SIZE);
    root[0].mode    = 0x8427u;
    root[0].length  = 2u;  // "." and ".."
    root[0].cluster = 0u;  // relative cluster 0
    std::strcpy(root[0].name, ".");
    root[1].mode    = 0x8427u;
    root[1].length  = 0u;
    root[1].cluster = 0u;
    std::strcpy(root[1].name, "..");

    // ── 5. Finalise ───────────────────────────────────────────────────────────
    card->m_path       = path;
    card->m_loaded     = true;
    card->m_superblock = *sb;
    card->buildFAT();   // populate m_fat from the freshly written structures

    if (!card->save(path)) return nullptr;
    return card;
}

bool PS2MemoryCard::parseSuperblock() {
    const std::size_t page_size = m_has_ecc ? ps2::PAGE_SIZE_ECC : ps2::PAGE_SIZE_RAW;
    if (m_data.size() < page_size) return false;

    // Superblock lives at page 0 (byte 0).
    std::memcpy(&m_superblock, m_data.data(), sizeof(ps2::Superblock));
    return std::memcmp(m_superblock.magic, ps2::SUPERBLOCK_MAGIC,
                       sizeof(ps2::SUPERBLOCK_MAGIC) - 1) == 0;
}

} // namespace cyan
