#include "ichor/npc/npc_memory.h"

#include <algorithm>

namespace ichor {

bool witnessedByTagOrPlace(const MemorySubject& subject, const ChronicleEntry& entry)
{
    if (std::find(entry.npcs.begin(), entry.npcs.end(), subject.npc) != entry.npcs.end()) {
        return true;
    }
    return subject.place != 0 && entry.place == subject.place;
}

std::uint32_t memorySalience(std::uint8_t weight, std::uint64_t tick, std::uint64_t now)
{
    const std::uint64_t age = std::min(now > tick ? now - tick : 0, kMaxMemoryAgeTicks);
    return static_cast<std::uint32_t>(std::uint64_t{weight} * 1000 * kMemoryHorizonTicks / (kMemoryHorizonTicks + age));
}

std::vector<RememberedEntry> memoryOf(const MemorySubject& subject, const std::vector<ChronicleEntry>& entries, std::uint64_t now,
                                      WitnessRule rule)
{
    const auto stronger = [](const RememberedEntry& a, const RememberedEntry& b) {
        if (a.salience != b.salience) {
            return a.salience > b.salience;
        }
        if (a.tick != b.tick) {
            return a.tick > b.tick;
        }
        return a.index < b.index;
    };
    std::vector<RememberedEntry> kept;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const ChronicleEntry& entry = entries[i];
        const std::uint32_t salience = memorySalience(entry.weight, entry.tick, now);
        if (salience == 0 || !rule(subject, entry)) {
            continue;
        }
        kept.push_back({i, entry.tick, salience});
        if (kept.size() >= kMaxRemembered * 4) {
            std::partial_sort(kept.begin(), kept.begin() + kMaxRemembered, kept.end(), stronger);
            kept.resize(kMaxRemembered);
        }
    }
    const std::size_t take = std::min(kept.size(), kMaxRemembered);
    std::partial_sort(kept.begin(), kept.begin() + take, kept.end(), stronger);
    kept.resize(take);
    return kept;
}

}
