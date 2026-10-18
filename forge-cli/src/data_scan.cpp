#include <forgecli/forge.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace forgecli {

namespace {

constexpr std::uintmax_t kMaxDataFileBytes = 64ull * 1024 * 1024;

bool collectIds(const std::filesystem::path& dir, std::vector<std::string>& out, std::string& error)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return true;
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".json") files.push_back(entry.path());
    }
    if (ec) {
        error = "cannot list " + dir.string() + ": " + ec.message();
        return false;
    }
    std::sort(files.begin(), files.end());
    for (const std::filesystem::path& file : files) {
        const std::uintmax_t bytes = std::filesystem::file_size(file, ec);
        if (ec || bytes > kMaxDataFileBytes) {
            error = "cannot read " + file.string();
            return false;
        }
        std::ifstream in(file, std::ios::binary);
        const nlohmann::json json = nlohmann::json::parse(in, nullptr, false);
        if (!in || !json.is_array()) {
            error = file.string() + " is not a JSON array";
            return false;
        }
        for (const nlohmann::json& record : json) {
            if (!record.is_object() || !record.contains("id") || !record["id"].is_string()) {
                error = file.string() + " has a record without a string id";
                return false;
            }
            out.push_back(record["id"].get<std::string>());
        }
    }
    return true;
}

bool sortUnique(std::vector<std::string>& ids, std::string_view pool, std::string& error)
{
    std::sort(ids.begin(), ids.end());
    const auto dup = std::adjacent_find(ids.begin(), ids.end());
    if (dup != ids.end()) {
        error = "duplicate id \"" + *dup + "\" in the " + std::string(pool) + " pool";
        return false;
    }
    return true;
}

}

ContentPools scanContentPools(const std::string& dataDir, std::string& error)
{
    ContentPools pools;
    error.clear();
    const std::filesystem::path root = dataDir;
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) {
        error = "data directory " + dataDir + " does not exist";
        return pools;
    }
    const bool ok = collectIds(root / "catalogue", pools.itemIds, error) && collectIds(root / "notable_items", pools.itemIds, error)
        && collectIds(root / "npcs", pools.npcArchetypes, error) && collectIds(root / "dungeon_skeletons", pools.dungeonArchetypes, error)
        && sortUnique(pools.itemIds, "item", error) && sortUnique(pools.npcArchetypes, "NPC archetype", error)
        && sortUnique(pools.dungeonArchetypes, "dungeon archetype", error);
    if (!ok) pools = {};
    return pools;
}

}
