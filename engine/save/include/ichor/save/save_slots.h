#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

class Scene;

// Save slots as ledger snapshots inside a campaign (doc 14). The world is
// forged once and never saved again; a slot holds only the scene snapshot
// and the facts needed to tell whether it still belongs to this campaign.
struct SaveContext {
    std::uint64_t worldSeed = 0;
    std::uint32_t genVersion = 0;
    std::uint64_t dataDigest = 0; // DataSet::digest() when the slot was written
};

struct SlotInfo {
    std::string name;
    SaveContext context;
    std::uint64_t tick = 0;
};

struct SavePolicy {
    // One rolling slot, no manual saves.
    bool ironman = false;
    std::uint32_t backups = 3;
};

enum class LoadStatus {
    Loaded,
    NoSuchSlot,
    Corrupt,        // the slot and every backup failed their checks
    WrongWorld,     // saved under another world seed or generator version
    UnsupportedVersion,
    RestoreFailed,  // the snapshot itself did not restore into the scene
};

struct LoadResult {
    LoadStatus status = LoadStatus::NoSuchSlot;
    bool fromBackup = false;     // the newest slot file was bad and a backup was used
    bool dataChanged = false;    // loaded, but the data layer differs from when it was saved
    SlotInfo info;
};

class SaveSlots {
public:
    static constexpr const char* kIronmanSlot = "ironman";

    SaveSlots(std::filesystem::path directory, SavePolicy policy = {});

    // Slot names are plain: letters, digits, dash and underscore.
    static bool validName(std::string_view name);

    bool save(std::string_view name, const Scene& scene, const SaveContext& context);
    LoadResult load(std::string_view name, Scene& scene, const SaveContext& expected);

    // Slots that have a readable newest file, by name.
    std::vector<SlotInfo> list() const;

    const std::string& lastError() const { return error_; }

private:
    std::filesystem::path pathOf(std::string_view name, std::uint32_t backup) const;

    std::filesystem::path directory_;
    SavePolicy policy_;
    std::string error_;
};

}
