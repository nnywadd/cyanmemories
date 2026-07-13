#include "ArchiveExtractor.hpp"
#include "PS2MemoryCard.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#include <zlib.h>

namespace {

// ─── LZARI Decoder ────────────────────────────────────────────────────────────
// Port of Haruhiko Okumura's public-domain LZARI (1989) to C++23.
// Adaptive arithmetic coding + LZ77 sliding window.

struct LzariDecoder {
    // LZ77 parameters
    static constexpr int N         = 4096;               // ring buffer size
    static constexpr int F         = 60;                 // max match length
    static constexpr int THRESHOLD = 2;                  // min useful match advantage
    static constexpr int N_CHAR    = 256 - THRESHOLD + F; // 314 char/length symbols
    static constexpr int N_GROUP   = 64;                 // upper-6-bit position groups

    // Arithmetic coder range (16-bit integers throughout)
    static constexpr int Q1      = 1 << 14;   // 16384
    static constexpr int Q2      = 2 * Q1;    // 32768
    static constexpr int Q3      = 3 * Q1;    // 49152
    static constexpr int Q4      = 4 * Q1;    // 65536
    static constexpr int MAX_CUM = Q1 - 1;    // 16383 — rescale threshold

    // ── Bit input ─────────────────────────────────────────────────────────────
    const std::uint8_t* src;
    std::size_t         src_len;
    std::size_t         src_pos = 0;
    int                 bit_buf = 0;
    int                 bit_cnt = 0;

    int getBit() {
        if (bit_cnt == 0) {
            bit_buf = (src_pos < src_len) ? static_cast<int>(src[src_pos++]) : 0;
            bit_cnt = 8;
        }
        return (bit_buf >> --bit_cnt) & 1;
    }

    // ── Arithmetic coder state ─────────────────────────────────────────────────
    int arith_low  = 0;
    int arith_high = Q4 - 1;
    int arith_val  = 0;

    void renorm() {
        for (;;) {
            if (arith_high < Q2) {
                // E1: top bit 0 — no adjustment needed
            } else if (arith_low >= Q2) {
                // E2: top bit 1 — subtract Q2
                arith_val -= Q2; arith_low -= Q2; arith_high -= Q2;
            } else if (arith_low >= Q1 && arith_high < Q3) {
                // E3: straddle midpoint — subtract Q1
                arith_val -= Q1; arith_low -= Q1; arith_high -= Q1;
            } else {
                break;
            }
            arith_low  <<= 1;
            arith_high  = (arith_high << 1) | 1;
            arith_val   = (arith_val  << 1) | getBit();
        }
    }

    // ── Adaptive frequency model ───────────────────────────────────────────────
    // Symbols sorted least-frequent-first (position 0 = rarest).
    // c_cum[i] = prefix sum of c_freq[0..i-1]; c_cum[n] = total.

    int c_freq[N_CHAR]{};
    int c_cum [N_CHAR + 1]{};
    int c_to_s[N_CHAR]{};   // symbol  → sorted position
    int s_to_c[N_CHAR]{};   // sorted position → symbol

    int p_freq[N_GROUP]{};
    int p_cum [N_GROUP + 1]{};
    int p_to_s[N_GROUP]{};
    int s_to_p[N_GROUP]{};

    void initModel(int* freq, int* cum, int* to_s, int* s_to, int n) {
        for (int i = 0; i < n; i++) {
            freq[i] = 1;
            cum[i]  = i;
            to_s[i] = i;
            s_to[i] = i;
        }
        cum[n] = n;
    }

    // Decode one symbol, update model, return the decoded symbol value.
    int decodeSymbol(int* freq, int* cum, int* to_s, int* s_to, int n) {
        int total = cum[n];
        int range = arith_high - arith_low + 1;
        int target = ((arith_val - arith_low + 1) * total - 1) / range;

        // Find largest p with cum[p] <= target
        int lo = 0, hi = n - 1;
        while (lo < hi) {
            int mid = (lo + hi + 1) / 2;
            if (cum[mid] <= target) lo = mid; else hi = mid - 1;
        }
        int p = lo;

        // Narrow the interval
        arith_high = arith_low + range * cum[p + 1] / total - 1;
        arith_low  = arith_low + range * cum[p]     / total;
        renorm();

        int sym = s_to[p];

        // Update model: increment freq[p], maintain non-decreasing sort
        freq[p]++;
        for (int i = p + 1; i <= n; i++) cum[i]++;

        int q = p;
        while (q < n - 1 && freq[q] > freq[q + 1]) {
            cum[q + 1] += freq[q + 1] - freq[q];  // adjust prefix sum for swap
            std::swap(freq[q], freq[q + 1]);
            int a = s_to[q], b = s_to[q + 1];
            std::swap(s_to[q], s_to[q + 1]);
            to_s[a] = q + 1;
            to_s[b] = q;
            ++q;
        }

        // Rescale if total is too high
        if (cum[n] >= MAX_CUM) {
            for (int i = 0; i < n; i++)
                freq[i] = std::max(1, freq[i] >> 1);
            cum[0] = 0;
            for (int i = 0; i < n; i++)
                cum[i + 1] = cum[i] + freq[i];
        }

        return sym;
    }

    // ── Ring buffer ────────────────────────────────────────────────────────────
    std::uint8_t text_buf[N];

    // ── Constructor ───────────────────────────────────────────────────────────
    LzariDecoder(const std::uint8_t* data, std::size_t len)
        : src(data), src_len(len)
    {
        std::fill(std::begin(text_buf), std::end(text_buf), std::uint8_t{0x20});
        initModel(c_freq, c_cum, c_to_s, s_to_c, N_CHAR);
        initModel(p_freq, p_cum, p_to_s, s_to_p, N_GROUP);
        for (int i = 0; i < 16; i++)
            arith_val = (arith_val << 1) | getBit();
    }

    // ── Main decode loop ───────────────────────────────────────────────────────
    std::vector<std::uint8_t> decode(std::uint32_t out_len) {
        std::vector<std::uint8_t> out;
        out.reserve(out_len);

        int r = N - F;   // initial ring-buffer write position

        while (out.size() < out_len) {
            int sym = decodeSymbol(c_freq, c_cum, c_to_s, s_to_c, N_CHAR);

            if (sym < 256) {
                // Literal byte
                auto ch = static_cast<std::uint8_t>(sym);
                out.push_back(ch);
                text_buf[r] = ch;
                r = (r + 1) & (N - 1);
            } else {
                // Match: length = sym - 256 + THRESHOLD + 1
                int match_len = sym - 256 + THRESHOLD + 1;

                // Position: upper 6 bits adaptive, lower 6 bits raw
                int upper6 = decodeSymbol(p_freq, p_cum, p_to_s, s_to_p, N_GROUP);
                int lower6 = 0;
                for (int i = 0; i < 6; i++)
                    lower6 = (lower6 << 1) | getBit();
                int match_pos = (upper6 << 6) | lower6;

                for (int i = 0; i < match_len && out.size() < out_len; i++) {
                    auto ch = text_buf[(match_pos + i) & (N - 1)];
                    out.push_back(ch);
                    text_buf[r] = ch;
                    r = (r + 1) & (N - 1);
                }
            }
        }

        return out;
    }
};

} // anonymous namespace

namespace cyan {

// ─── CBS (CodeBreaker) ────────────────────────────────────────────────────────

// Static 256-byte RC4 permutation table for the CodeBreaker format.
static constexpr std::array<std::uint8_t, 256> CBS_RC4S = {
    0x5f, 0x1f, 0x85, 0x6f, 0x31, 0xaa, 0x3b, 0x18, 0x21, 0xb9, 0xce, 0x1c, 0x07, 0x4c, 0x9c, 0xb4,
    0x81, 0xb8, 0xef, 0x98, 0x59, 0xae, 0xf9, 0x26, 0xe3, 0x80, 0xa3, 0x29, 0x2d, 0x73, 0x51, 0x62,
    0x7c, 0x64, 0x46, 0xf4, 0x34, 0x1a, 0xf6, 0xe1, 0xba, 0x3a, 0x0d, 0x82, 0x79, 0x0a, 0x5c, 0x16,
    0x71, 0x49, 0x8e, 0xac, 0x8c, 0x9f, 0x35, 0x19, 0x45, 0x94, 0x3f, 0x56, 0x0c, 0x91, 0x00, 0x0b,
    0xd7, 0xb0, 0xdd, 0x39, 0x66, 0xa1, 0x76, 0x52, 0x13, 0x57, 0xf3, 0xbb, 0x4e, 0xe5, 0xdc, 0xf0,
    0x65, 0x84, 0xb2, 0xd6, 0xdf, 0x15, 0x3c, 0x63, 0x1d, 0x89, 0x14, 0xbd, 0xd2, 0x36, 0xfe, 0xb1,
    0xca, 0x8b, 0xa4, 0xc6, 0x9e, 0x67, 0x47, 0x37, 0x42, 0x6d, 0x6a, 0x03, 0x92, 0x70, 0x05, 0x7d,
    0x96, 0x2f, 0x40, 0x90, 0xc4, 0xf1, 0x3e, 0x3d, 0x01, 0xf7, 0x68, 0x1e, 0xc3, 0xfc, 0x72, 0xb5,
    0x54, 0xcf, 0xe7, 0x41, 0xe4, 0x4d, 0x83, 0x55, 0x12, 0x22, 0x09, 0x78, 0xfa, 0xde, 0xa7, 0x06,
    0x08, 0x23, 0xbf, 0x0f, 0xcc, 0xc1, 0x97, 0x61, 0xc5, 0x4a, 0xe6, 0xa0, 0x11, 0xc2, 0xea, 0x74,
    0x02, 0x87, 0xd5, 0xd1, 0x9d, 0xb7, 0x7e, 0x38, 0x60, 0x53, 0x95, 0x8d, 0x25, 0x77, 0x10, 0x5e,
    0x9b, 0x7f, 0xd8, 0x6e, 0xda, 0xa2, 0x2e, 0x20, 0x4f, 0xcd, 0x8f, 0xcb, 0xbe, 0x5a, 0xe0, 0xed,
    0x2c, 0x9a, 0xd4, 0xe2, 0xaf, 0xd0, 0xa9, 0xe8, 0xad, 0x7a, 0xbc, 0xa8, 0xf2, 0xee, 0xeb, 0xf5,
    0xa6, 0x99, 0x28, 0x24, 0x6c, 0x2b, 0x75, 0x5d, 0xf8, 0xd3, 0x86, 0x17, 0xfb, 0xc0, 0x7b, 0xb3,
    0x58, 0xdb, 0xc7, 0x4b, 0xff, 0x04, 0x50, 0xe9, 0x88, 0x69, 0xc9, 0x2a, 0xab, 0xfd, 0x5b, 0x1b,
    0x8a, 0xd9, 0xec, 0x27, 0x44, 0x0e, 0x33, 0xc8, 0x6b, 0x93, 0x32, 0x48, 0xb6, 0x30, 0x43, 0xa5,
};

static std::optional<std::filesystem::path>
extractCBS(const std::filesystem::path& archive_path) {
    std::ifstream f(archive_path, std::ios::binary);
    if (!f)  return std::nullopt;

    // ── Header ────────────────────────────────────────────────────────────────
    char magic[4] = {};
    f.read(magic, 4);
    if (magic[0] != 'C' || magic[1] != 'F' || magic[2] != 'U' || magic[3] != '\0')
         return std::nullopt;

    // Offset 4: unknown 32-bit field (discard)
    // Offset 8: header length — file offset where the body begins
    std::uint32_t d04 = 0, hlen = 0;
    f.read(reinterpret_cast<char*>(&d04), 4);
    f.read(reinterpret_cast<char*>(&hlen), 4);

    // Offset 12: decompressed body size
    std::uint32_t dlen = 0;
    f.read(reinterpret_cast<char*>(&dlen), 4);

    // Offset 20 (0x14): game_id (32 chars, null-terminated)
    f.seekg(20);
    char game_id_buf[32] = {};
    f.read(game_id_buf, 32);
    const std::string game_id(game_id_buf, ::strnlen(game_id_buf, 32));
    if (game_id.empty())  return std::nullopt;

    // ── Create temp directory and save subdir ─────────────────────────────────
    std::string tmpl = "/tmp/cyanmemories_extract_XXXXXX";
    char*       tmp  = ::mkdtemp(tmpl.data());
    if (!tmp)  return std::nullopt;
    const std::filesystem::path temp_root(tmp);

    const std::filesystem::path save_dir = temp_root / game_id;
    std::error_code ec;
    std::filesystem::create_directories(save_dir, ec);
    if (ec) {
        std::filesystem::remove_all(temp_root);
         return std::nullopt;
    }

    // ── Read encrypted/compressed body ────────────────────────────────────────
    f.seekg(hlen);
    f.seekg(0, std::ios::end);
    const auto file_end = f.tellg();
    f.seekg(hlen);
    const std::size_t body_size = static_cast<std::size_t>(file_end) - hlen;

    std::vector<std::uint8_t> body(body_size);
    if (!f.read(reinterpret_cast<char*>(body.data()),
                static_cast<std::streamsize>(body_size))) {
        std::filesystem::remove_all(temp_root);
         return std::nullopt;
    }

    // ── RC4 decrypt (state machine seeded from static S-box) ─────────────────
    // i and j are kept as size_t; masking with 0xFF replaces modulo 256.
    {
        std::array<std::uint8_t, 256> S = CBS_RC4S;
        std::size_t i = 0, j = 0;
        for (auto& byte : body) {
            i = (i + 1u)       & 0xFFu;
            j = (j + S[i])     & 0xFFu;
            std::swap(S[i], S[j]);
            byte ^= S[(S[i] + S[j]) & 0xFFu];
        }
    }

    // ── Zlib decompress ───────────────────────────────────────────────────────
    std::vector<std::uint8_t> uncompressed(dlen);
    uLongf dest_len = dlen;
    if (uncompress(uncompressed.data(), &dest_len, body.data(),
                   static_cast<uLong>(body.size())) != Z_OK) {
        std::filesystem::remove_all(temp_root);
         return std::nullopt;
    }

    // ── Parse 64-byte file headers and extract files ──────────────────────────
    // Layout per entry: [0-15] unknown | [16-19] file_size (LE) |
    //                   [20-31] unknown | [32-63] name (null-terminated)
    // Immediately followed by file_size bytes of raw file data (no padding).
    std::size_t offset = 0;
    while (offset + 64u <= static_cast<std::size_t>(dest_len)) {
        const std::uint8_t* hdr = uncompressed.data() + offset;

        std::uint32_t file_size = 0;
        std::memcpy(&file_size, hdr + 16, 4);

        const char* name_ptr = reinterpret_cast<const char*>(hdr + 32);
        const std::string fname(name_ptr, ::strnlen(name_ptr, 32));

        offset += 64u;

        if (fname.empty()) break;
        if (offset + file_size > static_cast<std::size_t>(dest_len)) break;

        std::ofstream out(save_dir / fname, std::ios::binary | std::ios::trunc);
        if (out)
            out.write(reinterpret_cast<const char*>(uncompressed.data() + offset),
                      static_cast<std::streamsize>(file_size));

        offset += file_size;
    }

    return save_dir;
}

// ─── PSU (EMS) ────────────────────────────────────────────────────────────────

static std::optional<std::filesystem::path>
extractPSU(const std::filesystem::path& archive_path) {
    std::string tmpl = "/tmp/cyanmemories_extract_XXXXXX";
    char*       tmp  = ::mkdtemp(tmpl.data());
    if (!tmp)  return std::nullopt;
    const std::filesystem::path temp_root(tmp);

    std::ifstream f(archive_path, std::ios::binary);
    if (!f) {
        std::filesystem::remove_all(temp_root);
         return std::nullopt;
    }

    // First record = root game-directory entry; name field = game_id.
    ps2::DirEntry root{};
    if (!f.read(reinterpret_cast<char*>(&root), sizeof(ps2::DirEntry))) {
        std::filesystem::remove_all(temp_root);
         return std::nullopt;
    }

    const std::string game_id(root.name, ::strnlen(root.name, sizeof(root.name)));
    if (game_id.empty()) {
        std::filesystem::remove_all(temp_root);
         return std::nullopt;
    }

    const std::filesystem::path save_dir = temp_root / game_id;
    std::error_code ec;
    std::filesystem::create_directories(save_dir, ec);
    if (ec) {
        std::filesystem::remove_all(temp_root);
         return std::nullopt;
    }

    // Subsequent DIR records (. and ..) have no data block.
    // FILE records are followed by ceil(length / 1024) * 1024 bytes of data.
    ps2::DirEntry entry{};
    while (f.read(reinterpret_cast<char*>(&entry), sizeof(ps2::DirEntry))) {
        if (entry.mode == 0u) continue;
        if (entry.mode & ps2::MODE_DIR) continue;

        const std::string fname(entry.name, ::strnlen(entry.name, sizeof(entry.name)));
        if (fname.empty()) continue;

        const std::uint32_t file_size = entry.length;
        const std::size_t   padded    = ((file_size + 1023u) / 1024u) * 1024u;
        if (padded == 0u) continue;

        std::vector<std::uint8_t> data(padded);
        if (!f.read(reinterpret_cast<char*>(data.data()),
                    static_cast<std::streamsize>(padded)))
            break;

        std::ofstream out(save_dir / fname, std::ios::binary | std::ios::trunc);
        if (out)
            out.write(reinterpret_cast<const char*>(data.data()),
                      static_cast<std::streamsize>(file_size));
    }

    return save_dir;
}

// ─── MAX (Action Replay MAX) ──────────────────────────────────────────────────

static std::optional<std::filesystem::path>
extractMAX(const std::filesystem::path& archive_path) {
    std::ifstream f(archive_path, std::ios::binary);
    if (!f) return std::nullopt;

    // ── Header (92 bytes, little-endian) ─────────────────────────────────────
    char magic[12] = {};
    f.read(magic, 12);
    if (std::memcmp(magic, "Ps2PowerSave", 12) != 0) return std::nullopt;

    std::uint32_t crc = 0;
    f.read(reinterpret_cast<char*>(&crc), 4);           // ignore

    char dirname[32] = {};
    f.read(dirname, 32);

    char iconsysname[32] = {};
    f.read(iconsysname, 32);                            // ignore

    std::uint32_t clen   = 0;
    std::uint32_t dirlen = 0;
    std::uint32_t length = 0;
    f.read(reinterpret_cast<char*>(&clen),   4);
    f.read(reinterpret_cast<char*>(&dirlen), 4);
    f.read(reinterpret_cast<char*>(&length), 4);

    const std::string game_id(dirname, ::strnlen(dirname, 32));
    if (game_id.empty()) return std::nullopt;

    // ── Read payload ──────────────────────────────────────────────────────────
    f.seekg(0, std::ios::end);
    const auto file_end = f.tellg();
    f.seekg(92);
    const std::size_t remaining = static_cast<std::size_t>(file_end) - 92u;

    const bool is_uncompressed = (clen == length);
    const std::size_t read_len = is_uncompressed
        ? remaining
        : std::min<std::size_t>(clen >= 4u ? clen - 4u : 0u, remaining);

    if (read_len == 0) return std::nullopt;

    std::vector<std::uint8_t> payload(read_len);
    if (!f.read(reinterpret_cast<char*>(payload.data()),
                static_cast<std::streamsize>(read_len)))
        return std::nullopt;

    // ── Decompress ────────────────────────────────────────────────────────────
    std::vector<std::uint8_t> uncompressed;
    if (is_uncompressed) {
        uncompressed = std::move(payload);
    } else {
        LzariDecoder dec(payload.data(), payload.size());
        uncompressed = dec.decode(length);
        if (uncompressed.size() != length) {
            std::cerr << "[ArchiveExtractor] LZARI decompression size mismatch for "
                      << archive_path.filename() << "\n";
            return std::nullopt;
        }
    }

    // ── Create temp directory ─────────────────────────────────────────────────
    std::string tmpl = "/tmp/cyanmemories_extract_XXXXXX";
    char* tmp = ::mkdtemp(tmpl.data());
    if (!tmp) return std::nullopt;
    const std::filesystem::path temp_root(tmp);

    const std::filesystem::path save_dir = temp_root / game_id;
    std::error_code ec;
    std::filesystem::create_directories(save_dir, ec);
    if (ec) { std::filesystem::remove_all(temp_root); return std::nullopt; }

    // ── Extract files from uncompressed payload ───────────────────────────────
    // Each file: [4-byte size][32-byte name][size bytes data][alignment padding]
    // Alignment: off = ((off + 23) & ~15) - 8  (16-byte boundary skewed by -8)
    std::size_t off = 0;
    for (std::uint32_t fi = 0; fi < dirlen; fi++) {
        if (off + 36u > uncompressed.size()) break;

        std::uint32_t l = 0;
        std::memcpy(&l, uncompressed.data() + off, 4);

        char name_buf[32] = {};
        std::memcpy(name_buf, uncompressed.data() + off + 4, 32);
        const std::string fname(name_buf, ::strnlen(name_buf, 32));

        off += 36u;

        const std::size_t write_len =
            std::min<std::size_t>(l, uncompressed.size() - off);

        if (!fname.empty()) {
            std::ofstream out(save_dir / fname, std::ios::binary | std::ios::trunc);
            if (out)
                out.write(reinterpret_cast<const char*>(uncompressed.data() + off),
                          static_cast<std::streamsize>(write_len));
        }

        off += l;
        off = ((off + 23u) & ~std::size_t{15u}) - 8u;
    }

    return save_dir;
}

// ─── Public dispatch ──────────────────────────────────────────────────────────

std::optional<std::filesystem::path>
ArchiveExtractor::extractArchive(const std::filesystem::path& archive_path) {
    const std::string ext = archive_path.extension().string();

    if (ext == ".cbs") return extractCBS(archive_path);
    if (ext == ".psu") return extractPSU(archive_path);
    if (ext == ".max") return extractMAX(archive_path);

    return std::nullopt;
}

} // namespace cyan
