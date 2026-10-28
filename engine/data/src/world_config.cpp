#include "ichor/data/world_config.h"

#include "ichor/core/hash.h"

#include <algorithm>
#include <utility>

namespace ichor {

namespace {

WorldConfigResult failed(std::string error)
{
    WorldConfigResult result;
    result.error = std::move(error);
    return result;
}

WorldConfigResult fromPicks(std::vector<SlotAssignment> slots, std::span<const Axiom> pool)
{
    std::vector<Axiom> picked;
    for (const SlotAssignment& slot : slots) {
        const auto it = std::find_if(pool.begin(), pool.end(), [&](const Axiom& a) { return a.id == slot.axiomId; });
        picked.push_back(*it);
    }
    WorldConfigResult result;
    result.config.slots = std::move(slots);
    result.config.merged = mergeDirectives(picked);
    return result;
}

bool lists(const Axiom& axiom, const std::string& id)
{
    return std::find(axiom.conflicts.begin(), axiom.conflicts.end(), id) != axiom.conflicts.end();
}

struct LoadedPool {
    std::string error;
    std::vector<Axiom> pool;
    ConflictList blocklist;
};

LoadedPool loadPool(const std::filesystem::path& dataRoot)
{
    LoadedPool out;
    AxiomSet set;
    if (!set.loadDirectory(dataRoot)) {
        out.error = set.problems().empty() ? "the axioms did not load" : set.problems().front();
        return out;
    }
    if (!out.blocklist.loadDataRoot(dataRoot, set)) {
        out.error = out.blocklist.problems().empty() ? "the conflict list did not load" : out.blocklist.problems().front();
        return out;
    }
    for (const auto& [id, axiom] : set.axioms()) {
        out.pool.push_back(axiom);
    }
    return out;
}

}

bool axiomsMayCoexist(const Axiom& a, const Axiom& b, const ConflictList& blocklist)
{
    return a.slot != b.slot && !lists(a, b.id) && !lists(b, a.id) && blocklist.mayCoexist(a, b);
}

WorldConfigResult rollWorldConfig(std::uint64_t worldSeed, std::span<const Axiom> pool, const ConflictList& blocklist)
{
    SlotRollResult roll = rollSlots(worldSeed, pool, [&](const Axiom& chosen, const Axiom& candidate) { return axiomsMayCoexist(chosen, candidate, blocklist); });
    if (!roll.ok()) {
        return failed(roll.message);
    }
    return fromPicks(std::move(roll.slots), pool);
}

WorldConfigResult pinWorldConfig(std::string_view ids, std::span<const Axiom> pool, const ConflictList& blocklist)
{
    if (ids.empty()) {
        return failed("no axiom ids given");
    }
    std::vector<const Axiom*> picked;
    std::size_t start = 0;
    while (start <= ids.size()) {
        const std::size_t comma = std::min(ids.find(',', start), ids.size());
        const std::string id(ids.substr(start, comma - start));
        start = comma + 1;
        if (id.empty()) {
            return failed("an empty axiom id in the list");
        }
        const auto it = std::find_if(pool.begin(), pool.end(), [&](const Axiom& a) { return a.id == id; });
        if (it == pool.end()) {
            return failed("unknown axiom '" + id + "'");
        }
        for (const Axiom* earlier : picked) {
            if (earlier->id == id) {
                return failed("axiom '" + id + "' is named twice");
            }
            if (!axiomsMayCoexist(*earlier, *it, blocklist)) {
                return failed("axiom '" + id + "' may not share a world with '" + earlier->id + "'");
            }
        }
        picked.push_back(&*it);
    }
    std::sort(picked.begin(), picked.end(), [](const Axiom* a, const Axiom* b) { return a->slot < b->slot; });
    std::vector<SlotAssignment> slots;
    for (const Axiom* axiom : picked) {
        slots.push_back({axiom->slot, axiom->id, axiomSlotShownAtCreation(axiom->slot)});
    }
    return fromPicks(std::move(slots), pool);
}

WorldConfigResult loadRolledWorldConfig(std::uint64_t worldSeed, const std::filesystem::path& dataRoot)
{
    const LoadedPool loaded = loadPool(dataRoot);
    return loaded.error.empty() ? rollWorldConfig(worldSeed, loaded.pool, loaded.blocklist) : failed(loaded.error);
}

WorldConfigResult loadPinnedWorldConfig(std::string_view ids, const std::filesystem::path& dataRoot)
{
    const LoadedPool loaded = loadPool(dataRoot);
    return loaded.error.empty() ? pinWorldConfig(ids, loaded.pool, loaded.blocklist) : failed(loaded.error);
}

std::string worldConfigCanonicalText(const WorldConfig& config)
{
    std::string text;
    for (const SlotAssignment& slot : config.slots) {
        text += std::string(1, axiomSlotLetter(slot.slot)) + "=" + slot.axiomId + ";";
    }
    return text + "|" + mergedCanonicalText(config.merged);
}

std::uint64_t worldConfigDigest(const WorldConfig& config)
{
    return hash64(worldConfigCanonicalText(config));
}

std::string worldConfigIds(const WorldConfig& config)
{
    std::string ids;
    for (const SlotAssignment& slot : config.slots) {
        ids += (ids.empty() ? "" : ",") + slot.axiomId;
    }
    return ids;
}

}
