#pragma once
#include <filesystem>
#include <optional>

namespace cyan {

class ArchiveExtractor {
public:
    // Extracts a supported archive to a temporary directory.
    // On success, returns the path to the extracted game save folder (temp_root/GAMEID/).
    // The caller must delete temp_root (parent_path() of the returned value) when done.
    // Returns std::nullopt on failure or unsupported format.
    static std::optional<std::filesystem::path>
    extractArchive(const std::filesystem::path& archive_path);
};

} // namespace cyan
