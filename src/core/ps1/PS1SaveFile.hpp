#pragma once

#include "core/SaveFile.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace cyan {

class PS1SaveFile final : public SaveFile {
public:
    PS1SaveFile(std::string              game_id,
                std::string              title,
                Region                   region,
                std::size_t              block_count,
                std::vector<std::uint8_t> raw_data,
                std::size_t              slot_index);

    std::string               getGameID()     const override { return m_game_id;     }
    std::string               getTitle()      const override { return m_title;       }
    Region                    getRegion()     const override { return m_region;      }
    std::size_t               getBlockCount() const override { return m_block_count; }
    std::vector<IconFrame>    getIcons()      const override { return m_icons;       }
    std::vector<std::uint8_t> getRawData()    const override { return m_raw_data;   }
    std::size_t               getSlotIndex() const override { return m_slot_index; }

private:
    static std::vector<IconFrame> parseIcons(const std::vector<std::uint8_t>& raw);

    std::string               m_game_id;
    std::string               m_title;
    Region                    m_region;
    std::size_t               m_block_count;
    std::size_t               m_slot_index;
    std::vector<std::uint8_t> m_raw_data;
    std::vector<IconFrame>    m_icons;
};

} // namespace cyan
