#pragma once

#include "ichor/graph/embedding.h"
#include "ichor/graph/graph.h"

#include <worldforge/content_manifest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// The bake step that turns the manifest's dungeon slots (doc 08 phase 11) into dungeon layouts
// (doc 09). A slot names a POI, an archetype and a seed; the archetype is a theme of the regional
// skeletons in data/dungeon_skeletons ("crypt" picks sunken_crypt), so the layout is the authored
// shape of that skeleton: rooms, links and grid cells. When several skeletons share a theme the
// slot's seed picks one, so the choice is stable and does not depend on any load order.
//
// Layouts are generated on demand (when the player nears) and cached by slot, never at load time.
// Every skeleton is checked when the catalogue loads (layout validator, mission validator,
// embedding check) and a baked layout is checked again, so a layout handed out always passes.
//
// Not thread safe: one owner, normally the streamer's thread.

struct DungeonSkeleton {
    std::string id;
    std::string name;
    std::string theme;
    graph::Graph graph;
    std::map<graph::NodeId, graph::embedding::Cell> cells;
};

inline constexpr std::size_t kMaxSkeletonFiles = 64;
inline constexpr std::size_t kMaxSkeletonFileBytes = 1u << 20;
inline constexpr std::size_t kMaxSkeletonRooms = 64;
inline constexpr std::size_t kMaxSkeletonLinks = 256;

class DungeonSkeletonCatalog {
public:
    // Reads every .json file of `dir` in name order. Records that fail to parse or validate are
    // skipped and named in errors(); the rest still load.
    static DungeonSkeletonCatalog loadDirectory(const std::filesystem::path& dir);
    static DungeonSkeletonCatalog loadText(std::string_view fileName, std::string_view json);

    // Skeletons sorted by (theme, id).
    const std::vector<DungeonSkeleton>& all() const { return skeletons_; }
    const std::vector<std::string>& errors() const { return errors_; }

    // The skeletons of a theme, in id order; empty for an unknown theme.
    std::vector<const DungeonSkeleton*> forTheme(std::string_view theme) const;

private:
    void add(std::string_view fileName, std::string_view json);
    void finish();

    std::vector<DungeonSkeleton> skeletons_;
    std::vector<std::string> errors_;
};

struct BakedDungeon {
    std::string poiId;
    std::string archetype;
    std::string skeletonId;
    std::uint64_t seed = 0;
    graph::Graph graph;
    std::map<graph::NodeId, graph::embedding::Cell> cells;
    std::uint64_t layoutHash = 0; // graph::forge::layoutHash, stable across platforms
};

struct DungeonLayoutResult {
    const BakedDungeon* dungeon = nullptr; // valid until the DungeonLayouts is destroyed
    std::string error;                     // empty on success

    bool ok() const { return error.empty() && dungeon != nullptr; }
};

class DungeonLayouts {
public:
    // Copies the slots and keeps a reference to the catalogue, which must outlive this object.
    // A slot whose seed is not dungeonSeed(worldSeed, poi), or that repeats a POI id, is rejected
    // with a message in slotErrors() and cannot be baked.
    DungeonLayouts(const worldforge::ContentManifest& manifest, const DungeonSkeletonCatalog& catalog);

    // Bakes (once) and returns the layout of a POI's slot. Errors: no such slot, an archetype
    // with no skeleton, a rejected slot, or a skeleton that fails validation. Failures are cached
    // too, so asking again costs nothing.
    DungeonLayoutResult layout(const std::string& poiId);

    // POI ids that have a usable slot, sorted.
    std::vector<std::string> poiIds() const;
    // Slots whose archetype names no skeleton, as "poi: message", sorted by POI id.
    std::vector<std::string> unknownArchetypes() const;
    const std::vector<std::string>& slotErrors() const { return slotErrors_; }
    std::size_t bakedCount() const { return baked_.size(); }

private:
    struct Entry {
        std::unique_ptr<BakedDungeon> dungeon;
        std::string error;
    };

    Entry bake(const worldforge::DungeonSlot& slot) const;

    const DungeonSkeletonCatalog& catalog_;
    std::map<std::string, worldforge::DungeonSlot> slots_;
    std::map<std::string, Entry> baked_;
    std::vector<std::string> slotErrors_;
};

}
