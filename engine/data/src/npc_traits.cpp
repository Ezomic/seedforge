#include "ichor/data/npc_traits.h"

#include "ichor/core/hash.h"
#include "ichor/core/rng.h"

#include <algorithm>
#include <map>

namespace ichor {

namespace {

bool readString(const DataRecord& record, const char* name, std::string& out, std::string& error)
{
    const auto it = record.fields.find(name);
    const auto* value = it == record.fields.end() ? nullptr : std::get_if<std::string>(&it->second);
    if (value == nullptr) {
        error = "trait '" + record.id + "': \"" + name + "\" must be a string";
        return false;
    }
    out = *value;
    return true;
}

}

std::optional<TraitPool> TraitPool::fromTable(const DataTable& table, std::string& error)
{
    std::vector<Trait> traits;
    for (const auto& [id, record] : table.records) {
        Trait trait;
        trait.id = id;
        const auto incompatible = record.fields.find("incompatible");
        const auto* list = incompatible == record.fields.end() ? nullptr : std::get_if<std::vector<std::string>>(&incompatible->second);
        if (list == nullptr) {
            error = "trait '" + id + "': \"incompatible\" must be a list of strings";
            return std::nullopt;
        }
        trait.incompatible = *list;
        if (!readString(record, "name", trait.name, error) || !readString(record, "description", trait.description, error)) {
            return std::nullopt;
        }
        traits.push_back(std::move(trait));
    }
    return fromTraits(std::move(traits), error);
}

std::optional<TraitPool> TraitPool::fromTraits(std::vector<Trait> traits, std::string& error)
{
    if (traits.size() < kMaxNpcTraits) {
        error = "a trait pool needs at least " + std::to_string(kMaxNpcTraits) + " traits, got " + std::to_string(traits.size());
        return std::nullopt;
    }

    std::map<std::string, std::size_t, std::less<>> index;
    for (std::size_t i = 0; i < traits.size(); ++i) {
        if (traits[i].id.empty()) {
            error = "a trait has an empty id";
            return std::nullopt;
        }
        if (!index.emplace(traits[i].id, i).second) {
            error = "duplicate trait id '" + traits[i].id + "'";
            return std::nullopt;
        }
    }

    TraitPool pool;
    pool.conflicts_.assign(traits.size() * traits.size(), false);
    for (std::size_t i = 0; i < traits.size(); ++i) {
        for (const std::string& other : traits[i].incompatible) {
            const auto it = index.find(other);
            if (it == index.end()) {
                error = "trait '" + traits[i].id + "' is incompatible with unknown trait '" + other + "'";
                return std::nullopt;
            }
            if (it->second == i) {
                error = "trait '" + traits[i].id + "' is incompatible with itself";
                return std::nullopt;
            }
            pool.conflicts_[i * traits.size() + it->second] = true;
        }
    }
    for (std::size_t i = 0; i < traits.size(); ++i) {
        for (std::size_t j = i + 1; j < traits.size(); ++j) {
            if (pool.conflicts_[i * traits.size() + j] != pool.conflicts_[j * traits.size() + i]) {
                error = "incompatibility of '" + traits[i].id + "' and '" + traits[j].id + "' is not symmetric";
                return std::nullopt;
            }
        }
    }
    pool.traits_ = std::move(traits);
    return pool;
}

NpcTraits assignTraits(std::uint64_t worldSeed, std::uint64_t npcId, const TraitPool& pool)
{
    Rng rng = Rng(hashCombine(worldSeed, npcId)).derive("npc.traits");

    NpcTraits result;
    result.wanted = rng.range(static_cast<std::uint32_t>(kMinNpcTraits), static_cast<std::uint32_t>(kMaxNpcTraits));

    const std::size_t count = pool.traits().size();
    std::vector<std::size_t> chosen;
    while (chosen.size() < result.wanted) {
        std::vector<std::size_t> candidates;
        for (std::size_t i = 0; i < count; ++i) {
            const bool taken = std::find(chosen.begin(), chosen.end(), i) != chosen.end();
            const bool conflicts = std::any_of(chosen.begin(), chosen.end(), [&](std::size_t c) { return pool.incompatible(i, c); });
            if (!taken && !conflicts) {
                candidates.push_back(i);
            }
        }
        if (candidates.empty()) {
            break;
        }
        const auto pick = rng.range(0, static_cast<std::uint32_t>(candidates.size() - 1));
        chosen.push_back(candidates[pick]);
    }

    for (const std::size_t i : chosen) {
        result.ids.push_back(pool.traits()[i].id);
    }
    return result;
}

}
