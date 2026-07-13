#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace cyan {

enum class Region { NTSC_US, NTSC_JP, PAL, Unknown };

// One 16×16 RGBA frame of a PS1 animated icon (or first frame of a PS2 icon).
struct IconFrame {
    std::array<std::uint8_t, 16 * 16 * 4> pixels{};
};

class SaveFile {
public:
    virtual ~SaveFile() = default;

    virtual std::string             getGameID()    const = 0;
    virtual std::string             getTitle()     const = 0;
    virtual Region                  getRegion()    const = 0;
    virtual std::size_t             getBlockCount() const = 0;
    virtual std::vector<IconFrame>  getIcons()     const = 0;
    virtual std::vector<std::uint8_t> getRawData()    const = 0;
    virtual std::size_t               getSlotIndex() const = 0;

    virtual std::string getRegionString() const {
        switch (getRegion()) {
            case Region::NTSC_US: return "NTSC-U/C";
            case Region::NTSC_JP: return "NTSC-J";
            case Region::PAL:     return "PAL";
            default:              return "Unknown";
        }
    }
};

} // namespace cyan
