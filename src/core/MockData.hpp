#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cyan {

// Lightweight struct used by the UI before real binary parsing is wired up.
struct MockSaveInfo {
    std::string game_id;
    std::string title;
    std::string region;
    std::uint32_t block_count{1};
    std::string card_type; // "PS1" or "PS2"
};

inline std::vector<MockSaveInfo> makeMockPS1Saves() {
    return {
        { "BASLUS-00568", "Final Fantasy VII",               "NTSC-U/C", 1, "PS1" },
        { "BESCES-00867", "Castlevania: Symphony of the Night", "PAL",    1, "PS1" },
        { "BASLUS-01251", "Metal Gear Solid",                "NTSC-U/C", 1, "PS1" },
        { "BASCUS-94163", "Crash Bandicoot: Warped",         "NTSC-U/C", 1, "PS1" },
        { "BESCES-01704", "Silent Hill",                     "PAL",       1, "PS1" },
        { "BASLUS-00543", "Resident Evil 2",                 "NTSC-U/C", 1, "PS1" },
        { "BASPJ-00700",  "テイルズ オブ デスティニー", "NTSC-J", 2, "PS1" },
        { "BASLUS-00973", "Chrono Cross",                    "NTSC-U/C", 1, "PS1" },
        { "BASLUS-00552", "Spyro the Dragon",                "NTSC-U/C", 1, "PS1" },
        { "BESCES-02835", "Gran Turismo 2",                  "PAL",       1, "PS1" },
    };
}

inline std::vector<MockSaveInfo> makeMockPS2Saves() {
    return {
        { "BASLUS-20622", "Kingdom Hearts II",          "NTSC-U/C", 1, "PS2" },
        { "BASLUS-20403", "Shadow of the Colossus",     "NTSC-U/C", 1, "PS2" },
        { "BESCES-52483", "God of War",                 "PAL",       1, "PS2" },
        { "BASLUS-20444", "Final Fantasy X",            "NTSC-U/C", 2, "PS2" },
        { "BASLUS-20552", "Metal Gear Solid 3: Snake Eater", "NTSC-U/C", 1, "PS2" },
        { "BASPJ-10117",  "ペルソナ4", "NTSC-J",   1, "PS2" },
        { "BESCES-52005", "Ico",                        "PAL",       1, "PS2" },
        { "BASLUS-20776", "Okami",                      "NTSC-U/C", 1, "PS2" },
    };
}

} // namespace cyan
