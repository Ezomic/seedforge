#include "ichor/combat_bridge/bridge_corpus.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// Re-records the combat bridge replay corpus: one file per fight in the plan, replacing the folder's *.replay files.
// Run it through bin/regen-combat-bridge-corpus after a combat knob or the world forge is changed on purpose.
int main(int argc, char** argv)
{
    using namespace ichor::bridge_corpus;
    if (argc > 2) {
        std::fputs("usage: combat_bridge_corpus [output-dir]\n", stderr);
        return 2;
    }
    const std::filesystem::path dir = argc == 2 ? argv[1] : ICHOR_BRIDGE_CORPUS_DIR;

    std::map<std::uint64_t, World> worlds;
    std::vector<Entry> entries;
    for (const Slot& slot : plan()) {
        auto found = worlds.find(slot.worldSeed);
        if (found == worlds.end()) {
            std::string error;
            World world = forgeWorld(slot.worldSeed, kGridSize, error);
            if (!world.game) {
                std::fprintf(stderr, "combat_bridge_corpus: world %llu did not forge: %s\n", static_cast<unsigned long long>(slot.worldSeed), error.c_str());
                return 1;
            }
            found = worlds.emplace(slot.worldSeed, std::move(world)).first;
        }
        Pick pick = pickEntry(found->second, slot.skill, slot.playerWins, slot.pick);
        if (!pick.entry) {
            std::fprintf(stderr, "combat_bridge_corpus: %.*s in world %llu has no finished fight\n", static_cast<int>(slot.skill.size()), slot.skill.data(),
                static_cast<unsigned long long>(slot.worldSeed));
            return 1;
        }
        if (!pick.exact) {
            std::fprintf(stderr, "combat_bridge_corpus: %.*s in world %llu never %s on scene seeds 1 to %llu, kept the first finished fight\n",
                static_cast<int>(slot.skill.size()), slot.skill.data(), static_cast<unsigned long long>(slot.worldSeed),
                slot.playerWins ? "won" : "lost", static_cast<unsigned long long>(kSearchSeeds));
        }
        entries.push_back(std::move(*pick.entry));
    }

    std::error_code error;
    std::filesystem::create_directories(dir, error);
    if (error) {
        std::fprintf(stderr, "combat_bridge_corpus: cannot create %s\n", dir.string().c_str());
        return 1;
    }
    for (const auto& item : std::filesystem::directory_iterator(dir, error)) {
        if (item.path().extension() == ".replay") std::filesystem::remove(item.path(), error);
    }
    int index = 0;
    for (const Entry& entry : entries) {
        index += 1;
        char prefix[16];
        std::snprintf(prefix, sizeof(prefix), "%02d-", index);
        const std::string name = std::string(prefix) + entryName(entry);
        const std::filesystem::path path = dir / (name + ".replay");
        std::ofstream out(path, std::ios::binary);
        out << encodeEntry(entry);
        if (!out) {
            std::fprintf(stderr, "combat_bridge_corpus: cannot write %s\n", path.string().c_str());
            return 1;
        }
        std::printf("%s: %s in %d ticks (%s)\n", path.filename().string().c_str(), entry.expected.playerLost ? "foe wins" : "player wins",
            entry.expected.ticks, ichor::stage0::name(entry.expected.cause));
    }
    return 0;
}
