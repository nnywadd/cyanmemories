#include "PS1SaveFile.hpp"
#include "PS1MemoryCard.hpp"
#include <algorithm>

namespace cyan {

PS1SaveFile::PS1SaveFile(std::string              game_id,
                         std::string              title,
                         Region                   region,
                         std::size_t              block_count,
                         std::vector<std::uint8_t> raw_data,
                         std::size_t              slot_index)
    : m_game_id    (std::move(game_id))
    , m_title      (std::move(title))
    , m_region     (region)
    , m_block_count(block_count)
    , m_slot_index (slot_index)
    , m_raw_data   (std::move(raw_data))
    , m_icons      (parseIcons(m_raw_data))
{}

// ─── Icon decoding ────────────────────────────────────────────────────────────
//
// PS1 icon header is at the start of the save's first data block:
//   0x00  uint16  icon flags
//   0x02  uint16  animation frame count (1, 2, or 3)
//   0x04  64 B    Shift-JIS title (already decoded by PS1MemoryCard::getSaves)
//   0x60  32 B    16 × RGB555 LE palette (0x0000 = fully transparent)
//   0x80+  128 B per frame  (4-bit indexed, 2 pixels per byte, lo nibble = left pixel)

static std::array<std::uint8_t, 4> rgb555_to_rgba(std::uint16_t c) noexcept {
    if (c == 0u) return {0u, 0u, 0u, 0u};
    const auto r = static_cast<std::uint8_t>(((c >>  0u) & 0x1Fu) * 255u / 31u);
    const auto g = static_cast<std::uint8_t>(((c >>  5u) & 0x1Fu) * 255u / 31u);
    const auto b = static_cast<std::uint8_t>(((c >> 10u) & 0x1Fu) * 255u / 31u);
    return {r, g, b, 255u};
}

std::vector<IconFrame> PS1SaveFile::parseIcons(const std::vector<std::uint8_t>& raw) {
    // Must have at least one full frame of pixel data.
    if (raw.size() < ps1::ICON_PIXEL_OFF + 128u) return {};

    // Frame count (LE uint16 at ICON_ANIM_FRAME_OFF), clamped 1–3.
    const auto raw_frames =
        static_cast<std::uint16_t>(raw[ps1::ICON_ANIM_FRAME_OFF]) |
        (static_cast<std::uint16_t>(raw[ps1::ICON_ANIM_FRAME_OFF + 1u]) << 8u);
    const std::uint16_t num_frames = std::clamp<std::uint16_t>(raw_frames, 1u, 3u);

    // Decode 16-colour palette (16 × RGB555 LE at ICON_PALETTE_OFF).
    std::array<std::array<std::uint8_t, 4>, 16> palette{};
    for (std::size_t i = 0u; i < 16u; ++i) {
        const std::size_t off = ps1::ICON_PALETTE_OFF + i * 2u;
        if (off + 1u >= raw.size()) break;
        const auto entry =
            static_cast<std::uint16_t>(raw[off]) |
            (static_cast<std::uint16_t>(raw[off + 1u]) << 8u);
        palette[i] = rgb555_to_rgba(entry);
    }

    // Decode each animation frame.
    std::vector<IconFrame> frames;
    frames.reserve(num_frames);
    for (std::uint16_t f = 0u; f < num_frames; ++f) {
        const std::size_t frame_off = ps1::ICON_PIXEL_OFF + static_cast<std::size_t>(f) * 128u;
        if (frame_off + 128u > raw.size()) break;

        IconFrame frame{};
        // Each byte holds two 4-bit palette indices: lo nibble = left (even) pixel.
        for (std::size_t p = 0u; p < 128u; ++p) {
            const std::uint8_t byte = raw[frame_off + p];
            const std::uint8_t lo   = byte & 0x0Fu;
            const std::uint8_t hi   = (byte >> 4u) & 0x0Fu;
            const std::size_t  px   = p * 2u;
            const auto& ca = palette[lo];
            const auto& cb = palette[hi];
            frame.pixels[px * 4u + 0u] = ca[0]; frame.pixels[px * 4u + 1u] = ca[1];
            frame.pixels[px * 4u + 2u] = ca[2]; frame.pixels[px * 4u + 3u] = ca[3];
            frame.pixels[(px + 1u) * 4u + 0u] = cb[0]; frame.pixels[(px + 1u) * 4u + 1u] = cb[1];
            frame.pixels[(px + 1u) * 4u + 2u] = cb[2]; frame.pixels[(px + 1u) * 4u + 3u] = cb[3];
        }
        frames.push_back(std::move(frame));
    }

    return frames;
}

} // namespace cyan
