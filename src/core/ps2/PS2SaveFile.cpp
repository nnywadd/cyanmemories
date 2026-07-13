#include "PS2SaveFile.hpp"

namespace cyan {

PS2SaveFile::PS2SaveFile(std::string game_id, std::string title, std::size_t block_count)
    : m_game_id    (std::move(game_id))
    , m_title      (std::move(title))
    , m_block_count(block_count)
{
    // Derive region from first 2 characters of the directory name.
    // Convention: BA=NTSC-US, BE=PAL, BI/BJ=NTSC-JP  (mirrors PS1 scheme).
    if (m_game_id.size() >= 2u && m_game_id[0] == 'B') {
        switch (m_game_id[1]) {
            case 'A':             m_region = Region::NTSC_US; break;
            case 'E':             m_region = Region::PAL;     break;
            case 'I': case 'J':   m_region = Region::NTSC_JP; break;
            default:              m_region = Region::Unknown; break;
        }
    }
}

} // namespace cyan
