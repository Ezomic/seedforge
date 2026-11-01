#pragma once

#include "ichor/save/chronicle.h"
#include "ichor/save/save_migration.h"

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
    // Null means the formats this build ships; tests pass their own.
    const MigrationRegistry* migrations = nullptr;
};

enum class LoadStatus {
    Loaded,
    NoSuchSlot,
    Corrupt,        // the slot and every backup failed their checks
    WrongWorld,     // saved under another world seed or generator version
    UnsupportedVersion, // newer than this build reads, or older with no migration path; the file is left alone
    MigrationFailed,    // a migration step refused the file; it is left as it was
    RestoreFailed,  // the snapshot itself did not restore into the scene
};

struct LoadResult {
    LoadStatus status = LoadStatus::NoSuchSlot;
    bool fromBackup = false;     // the newest slot file was bad and a backup was used
    std::uint32_t backupIndex = 0; // which backup, 1 the newest; 0 when the slot file itself loaded
    bool dataChanged = false;    // loaded, but the data layer differs from when it was saved
    SlotInfo info;
    Chronicle chronicle;         // the story up to the moment of the save
    std::uint32_t migratedFrom = 0; // the format the file was in, when loading carried it up to the current one
    std::uint32_t savedFormat = 0;  // the format of the file an UnsupportedVersion or MigrationFailed refusal was about
    bool chronicleDamaged = false; // the slot held a chronicle that could not be read, so it came back empty
};

// The line the player is shown at startup when a slot came back from a backup; empty when the slot file itself loaded.
std::string restoreNotice(std::string_view slot, const LoadResult& result);

class SaveSlots {
public:
    static constexpr const char* kIronmanSlot = "ironman";

    SaveSlots(std::filesystem::path directory, SavePolicy policy = {});

    // Slot names are plain: letters, digits, dash and underscore.
    static bool validName(std::string_view name);

    bool save(std::string_view name, const Scene& scene, const SaveContext& context, const Chronicle& chronicle = {});
    LoadResult load(std::string_view name, Scene& scene, const SaveContext& expected);

    // Slots that have a readable newest file, by name.
    std::vector<SlotInfo> list() const;

    const std::string& lastError() const { return error_; }

private:
    const MigrationRegistry& migrations() const { return policy_.migrations ? *policy_.migrations : MigrationRegistry::standard(); }
    std::filesystem::path pathOf(std::string_view name, std::uint32_t backup) const;

    std::filesystem::path directory_;
    SavePolicy policy_;
    std::string error_;
};

}
