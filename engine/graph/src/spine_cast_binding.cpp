#include "ichor/graph/spine_cast_binding.h"

#include "ichor/core/rng.h"

#include <algorithm>
#include <map>
#include <set>

namespace ichor::graph::spine {

namespace {

void add(std::vector<Finding>& out, Code code, std::string slot, std::string message)
{
    out.push_back({code, std::move(slot), std::move(message)});
}

bool validId(const std::string& id)
{
    return !id.empty() && id.size() <= kMaxIdLength;
}

bool shortAll(const std::vector<std::string>& v)
{
    return std::all_of(v.begin(), v.end(), [](const std::string& x) { return x.size() <= kMaxIdLength; });
}

bool hasTag(const std::vector<std::string>& tags, const std::string& tag)
{
    return tag.empty() || std::find(tags.begin(), tags.end(), tag) != tags.end();
}

bool hostile(const Faction& a, const Faction& b)
{
    const auto has = [](const Faction& f, const std::string& id) {
        return std::find(f.hostileTo.begin(), f.hostileTo.end(), id) != f.hostileTo.end();
    };
    return has(a, b.id) || has(b, a.id);
}

std::vector<Finding> checkSpine(const Spine& spine)
{
    std::vector<Finding> out;
    if (spine.slots.empty()) {
        add(out, Code::Empty, "", "spine has no slots");
        return out;
    }
    if (spine.slots.size() > kMaxSlots) {
        add(out, Code::TooLarge, "", "spine has too many slots");
        return out;
    }
    std::map<std::string, const Slot*> byId;
    for (const Slot& s : spine.slots) {
        if (!validId(s.id) || s.needsTag.size() > kMaxIdLength || s.memberOf.size() > kMaxIdLength ||
            s.hostileTo.size() > kMaxIdLength) {
            add(out, Code::BadSlotId, "", "slot id empty or a slot string too long");
            continue;
        }
        if (!byId.emplace(s.id, &s).second)
            add(out, Code::DuplicateSlot, s.id, "duplicate slot " + s.id);
    }
    if (!out.empty())
        return out;
    for (const Slot& s : spine.slots) {
        if (!s.memberOf.empty()) {
            const auto it = byId.find(s.memberOf);
            if (s.kind != SlotKind::Npc || it == byId.end() || it->second->kind != SlotKind::Faction)
                add(out, Code::BadReference, s.id, "slot " + s.id + " has a bad memberOf");
        }
        if (!s.hostileTo.empty()) {
            const auto it = byId.find(s.hostileTo);
            if (s.kind != SlotKind::Faction || it == byId.end() || it->second->kind != SlotKind::Faction ||
                s.hostileTo == s.id)
                add(out, Code::BadReference, s.id, "slot " + s.id + " has a bad hostileTo");
        }
    }
    if (!out.empty())
        return out;
    for (const Slot& s : spine.slots) {
        const Slot* cur = &s;
        for (std::size_t hops = 0; !cur->hostileTo.empty(); ++hops) {
            if (hops > spine.slots.size()) {
                add(out, Code::ReferenceCycle, s.id, "hostileTo cycle at slot " + s.id);
                break;
            }
            cur = byId.at(cur->hostileTo);
        }
    }
    return out;
}

std::vector<Finding> checkCast(const Cast& cast)
{
    std::vector<Finding> out;
    if (cast.factions.size() + cast.npcs.size() > kMaxCastEntities) {
        add(out, Code::TooLarge, "", "cast is too large");
        return out;
    }
    std::set<std::string> seen;
    const auto check = [&](const std::string& id, const std::vector<std::string>& tags) {
        if (!validId(id) || tags.size() > kMaxTags || !shortAll(tags)) {
            add(out, Code::BadCastId, "", "cast entity id empty or too long, or bad tags");
            return;
        }
        if (!seen.insert(id).second)
            add(out, Code::DuplicateCast, "", "duplicate cast id " + id);
    };
    for (const Faction& f : cast.factions) {
        if (f.hostileTo.size() > kMaxTags || !shortAll(f.hostileTo))
            add(out, Code::BadCastId, "", "faction hostility list too long");
        check(f.id, f.tags);
    }
    for (const Npc& n : cast.npcs) {
        if (n.faction.size() > kMaxIdLength)
            add(out, Code::BadCastId, "", "npc faction id too long");
        check(n.id, n.tags);
    }
    return out;
}

// Faction slots first (a hostileTo target before its user), then Npc slots,
// so every memberOf and hostileTo target is bound before it is read.
std::vector<const Slot*> bindOrder(const Spine& spine)
{
    std::vector<const Slot*> order;
    std::set<std::string> placed;
    std::vector<const Slot*> pending;
    for (const Slot& s : spine.slots)
        if (s.kind == SlotKind::Faction)
            pending.push_back(&s);
    while (!pending.empty()) {
        const std::size_t before = pending.size();
        for (auto it = pending.begin(); it != pending.end();) {
            if ((*it)->hostileTo.empty() || placed.contains((*it)->hostileTo)) {
                placed.insert((*it)->id);
                order.push_back(*it);
                it = pending.erase(it);
            } else {
                ++it;
            }
        }
        if (pending.size() == before)
            break;
    }
    for (const Slot& s : spine.slots)
        if (s.kind == SlotKind::Npc)
            order.push_back(&s);
    return order;
}

struct Search {
    const Cast& cast;
    std::vector<const Slot*> order;
    std::vector<std::vector<std::size_t>> candidates; // per order index, into factions or npcs
    std::map<std::string, std::size_t> factionOf;     // slot id -> faction index
    std::map<std::string, std::size_t> npcOf;         // slot id -> npc index
    std::set<std::size_t> usedFactions;
    std::set<std::size_t> usedNpcs;
    std::size_t nodes = 0;
    bool overBudget = false;

    bool allowed(const Slot& slot, std::size_t c) const
    {
        if (slot.kind == SlotKind::Faction) {
            if (usedFactions.contains(c))
                return false;
            return slot.hostileTo.empty() || hostile(cast.factions[c], cast.factions[factionOf.at(slot.hostileTo)]);
        }
        if (usedNpcs.contains(c))
            return false;
        return slot.memberOf.empty() || cast.npcs[c].faction == cast.factions[factionOf.at(slot.memberOf)].id;
    }

    void place(const Slot& slot, std::size_t c)
    {
        if (slot.kind == SlotKind::Faction) {
            usedFactions.insert(c);
            factionOf[slot.id] = c;
        } else {
            usedNpcs.insert(c);
            npcOf[slot.id] = c;
        }
    }

    void unplace(const Slot& slot, std::size_t c)
    {
        if (slot.kind == SlotKind::Faction) {
            usedFactions.erase(c);
            factionOf.erase(slot.id);
        } else {
            usedNpcs.erase(c);
            npcOf.erase(slot.id);
        }
    }

    bool solve(std::size_t depth)
    {
        if (depth == order.size())
            return true;
        const Slot& slot = *order[depth];
        for (const std::size_t c : candidates[depth]) {
            if (++nodes > kSearchBudget) {
                overBudget = true;
                return false;
            }
            if (!allowed(slot, c))
                continue;
            place(slot, c);
            if (solve(depth + 1))
                return true;
            unplace(slot, c);
            if (overBudget)
                return false;
        }
        return false;
    }
};

void shuffle(std::vector<std::size_t>& v, Rng& rng)
{
    for (std::size_t i = v.size(); i > 1; --i)
        std::swap(v[i - 1], v[rng.nextU32() % i]);
}

} // namespace

Result bind(const Spine& spine, const Cast& cast, std::uint64_t seed)
{
    Result result;
    result.findings = checkSpine(spine);
    if (!result.findings.empty())
        return result;
    result.findings = checkCast(cast);
    if (!result.findings.empty())
        return result;

    // Base order by id so the cast vectors' own order cannot leak in.
    std::vector<std::size_t> factionIdx(cast.factions.size());
    std::vector<std::size_t> npcIdx(cast.npcs.size());
    for (std::size_t i = 0; i < factionIdx.size(); ++i)
        factionIdx[i] = i;
    for (std::size_t i = 0; i < npcIdx.size(); ++i)
        npcIdx[i] = i;
    std::sort(factionIdx.begin(), factionIdx.end(),
              [&](std::size_t a, std::size_t b) { return cast.factions[a].id < cast.factions[b].id; });
    std::sort(npcIdx.begin(), npcIdx.end(), [&](std::size_t a, std::size_t b) { return cast.npcs[a].id < cast.npcs[b].id; });

    Search search{cast, bindOrder(spine), {}, {}, {}, {}, {}, 0, false};
    const Rng root(seed);
    for (const Slot* slot : search.order) {
        Rng rng = root.derive(slot->id);
        const bool isFaction = slot->kind == SlotKind::Faction;
        std::vector<std::size_t> kept;
        for (const std::size_t c : isFaction ? factionIdx : npcIdx)
            if (hasTag(isFaction ? cast.factions[c].tags : cast.npcs[c].tags, slot->needsTag))
                kept.push_back(c);
        if (kept.empty()) {
            add(result.findings, Code::NoCandidate, slot->id, "no cast member can fill slot " + slot->id);
            continue;
        }
        shuffle(kept, rng);
        search.candidates.push_back(std::move(kept));
    }
    if (!result.findings.empty())
        return result;

    if (!search.solve(0)) {
        if (search.overBudget)
            add(result.findings, Code::OverBudget, "", "binding search exceeded its budget");
        else
            add(result.findings, Code::Unsatisfiable, "", "no assignment satisfies every constraint");
        return result;
    }
    for (const Slot& s : spine.slots) {
        const bool isFaction = s.kind == SlotKind::Faction;
        result.bindings.push_back(
            {s.id, isFaction ? cast.factions[search.factionOf.at(s.id)].id : cast.npcs[search.npcOf.at(s.id)].id});
    }
    result.ok = true;
    return result;
}

} // namespace ichor::graph::spine
