#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ichor {

// art/texture_budget.json: the largest edge a texture may keep, by the folder its glTF lives in.
struct TextureBudget {
    struct Entry {
        std::string prefix; // a path under the repository root, such as "art/props/terrain/"
        std::uint32_t maxSize = 0;
    };
    std::uint32_t defaultMax = 1024; // for a path no prefix matches
    std::vector<Entry> entries;
};

// {"default": 1024, "prefixes": [{"prefix": "art/props/", "max": 1024}, ...]}. Each problem found is added to `problems`;
// the budget is left holding only the entries that were valid, and false is returned when the file cannot be read at all.
bool loadTextureBudget(const std::filesystem::path& file, TextureBudget& budget, std::vector<std::string>& problems);

// The cap for a glTF at `sourcePath`. The longest matching prefix wins. Only the part of the path from its "art/" folder on
// is compared, so an absolute path and a repository-relative one give the same answer.
std::uint32_t maxTextureSize(const TextureBudget& budget, const std::filesystem::path& sourcePath);

}
