#pragma once

#include "SaveFile.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace cyan {

enum class MemoryCardType { PS1, PS2 };

class MemoryCard {
public:
    virtual ~MemoryCard() = default;

    // Load / persist
    virtual bool load(const std::filesystem::path& path) = 0;
    virtual bool save(const std::filesystem::path& path) = 0;

    // Save-slot operations
    virtual std::vector<std::shared_ptr<SaveFile>> getSaves() const = 0;
    virtual bool importSave(const std::filesystem::path& path) = 0;
    virtual bool exportSave(std::size_t index, const std::filesystem::path& path) = 0;
    virtual bool deleteSave(std::size_t index) = 0;

    // Metadata
    virtual MemoryCardType  getType()        const = 0;
    virtual std::string     getDisplayName() const = 0;
    virtual std::size_t     getCapacity()    const = 0; // in blocks
    virtual std::size_t     getUsedBlocks()  const = 0;

    const std::filesystem::path& getPath()   const noexcept { return m_path; }
    bool                         isLoaded()  const noexcept { return m_loaded; }

protected:
    std::filesystem::path m_path;
    bool                  m_loaded{false};
};

} // namespace cyan
