#pragma once

#include "ichor/save/save_slots.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// A campaign owns the forged world; slots are small snapshots inside it (doc 14).
//
//   <root>/<campaign>/world/manifest.bin   written last, so its presence means the world is whole
//   <root>/<campaign>/world/<file>         one flat file per baked blob, written once, never again
//   <root>/<campaign>/slots/               a SaveSlots directory
//
// Nothing here rewrites a world: create() refuses an existing campaign, and no call
// touches a world file after it is written. The manifest names every blob with its
// size and hash, so a world can be checked without knowing how its blobs are read.

struct WorldBlob {
    std::string name;
    std::span<const std::uint8_t> bytes;
};

struct WorldFileInfo {
    std::string name;
    std::uint64_t size = 0;
    std::uint64_t hash = 0;
};

struct WorldManifest {
    std::uint64_t worldSeed = 0;
    std::uint32_t genVersion = 0;
    std::vector<WorldFileInfo> files; // sorted by name
};

enum class CampaignStatus {
    Ok,
    BadName,        // a campaign or blob name is not plain, or blob names repeat
    TooLarge,       // too many blobs, or one is over the size limit
    AlreadyExists,  // create() never touches an existing campaign
    NoSuchCampaign,
    Incomplete,     // the world folder has no manifest, e.g. forging was interrupted
    CorruptWorld,   // the manifest is damaged, or a blob is missing or differs from the manifest
    WriteFailed,
};

struct CampaignLimits {
    std::uint32_t maxFiles = 4096;
    std::uint64_t maxFileBytes = std::uint64_t{1} << 30;
};

class Campaign {
public:
    static constexpr const char* kManifestFile = "manifest.bin";

    // Lowercase letters, digits, dash and underscore, 1 to 48 characters.
    static bool validName(std::string_view name);
    // As validName plus '.', no leading dot, up to 64 characters, and not the manifest's own name.
    static bool validBlobName(std::string_view name);

    // Forges the campaign's world to disk. On any failure the folder this call made is removed.
    static CampaignStatus create(const std::filesystem::path& root, std::string_view name, std::uint64_t worldSeed,
        std::uint32_t genVersion, std::span<const WorldBlob> blobs, const CampaignLimits& limits = {});

    // Reads the manifest and checks every blob exists at its recorded size. Cheap; see verifyWorld() for hashes.
    static std::optional<Campaign> open(const std::filesystem::path& root, std::string_view name, CampaignStatus* status = nullptr);

    // Campaign names under root that hold a complete world, sorted.
    static std::vector<std::string> list(const std::filesystem::path& root);

    const std::string& name() const { return name_; }
    const WorldManifest& manifest() const { return manifest_; }

    // What a slot written in this campaign must carry to count as belonging to it.
    SaveContext slotContext(std::uint64_t dataDigest = 0) const;
    SaveSlots slots(SavePolicy policy = {}) const;
    std::filesystem::path worldDirectory() const { return directory_ / "world"; }
    std::filesystem::path slotsDirectory() const { return directory_ / "slots"; }

    // Hashes every blob against the manifest. Slow on a real world; run it on demand, not on every load.
    bool verifyWorld() const;

    // Reads one blob, checked against its recorded size and hash. Nullopt on any failure.
    std::optional<std::vector<std::uint8_t>> readBlob(std::string_view blobName) const;

private:
    Campaign(std::filesystem::path directory, std::string name, WorldManifest manifest);

    std::filesystem::path directory_;
    std::string name_;
    WorldManifest manifest_;
};

}
