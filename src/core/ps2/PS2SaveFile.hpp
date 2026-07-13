#pragma once

#include "core/SaveFile.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace cyan {

// Concrete SaveFile produced by PS2MemoryCard::getSaves().
// game_id  = raw directory name (e.g. "BASLUS-20622") — always set
// title    = Shift-JIS title decoded from icon.sys (fallback: game_id)
class PS2SaveFile final : public SaveFile {
public:
    PS2SaveFile(std::string game_id, std::string title, std::size_t block_count = 1u);

    std::string               getGameID()     const override { return m_game_id;    }
    std::string               getTitle()      const override { return m_title;      }
    Region                    getRegion()     const override { return m_region;      }
    std::size_t               getBlockCount() const override { return m_block_count; }
    std::vector<IconFrame>    getIcons()      const override { return {};  }
    std::vector<std::uint8_t> getRawData()    const override { return {};  }
    std::size_t               getSlotIndex() const override { return 0u; }

private:
    std::string m_game_id;
    std::string m_title;
    Region      m_region{Region::Unknown};
    std::size_t m_block_count{1u};
};

} // namespace cyan
