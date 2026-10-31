#include "ichor/combat_bridge/bridge_corpus.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

// Replays the checked-in corpus and writes one line per fight to `out` (SEED-743). CI runs this on Windows and Linux and compares the two
// files, so the exit code says whether this build agrees with the files, and the files say whether the platforms agree with each other.
int replay(const std::filesystem::path& out)
{
    using namespace ichor::bridge_corpus;
    const ReplayReport report = replayCorpus(ICHOR_BRIDGE_CORPUS_DIR);
    for (const std::string& warning : report.warnings) std::fprintf(stderr, "combat_bridge_corpus: %s\n", warning.c_str());
    const std::string text = report.text();
    std::fputs(text.c_str(), stdout);
    if (!out.empty()) {
        std::ofstream file(out, std::ios::binary);
        file << text;
        if (!file) {
            std::fprintf(stderr, "combat_bridge_corpus: cannot write %s\n", out.string().c_str());
            return 1;
        }
    }
    if (!report.error.empty()) {
        std::fprintf(stderr, "combat_bridge_corpus: %s\n", report.error.c_str());
        return 1;
    }
    if (!report.matches()) {
        std::fputs("combat_bridge_corpus: a fight replays to another outcome than its file records (the lines above whose recorded and replayed parts differ).\n"
                   "If a combat knob or the world forge was changed on purpose, run bin/regen-combat-bridge-corpus and commit the files; "
                   "otherwise this is a determinism regression.\n",
            stderr);
        return 1;
    }
    std::printf("%zu fights replay to their recorded outcome\n", report.lines.size());
    return 0;
}

}

// Re-records the combat bridge replay corpus: one file per fight in the plan, replacing the folder's *.replay files.
// Run it through bin/regen-combat-bridge-corpus after a combat knob or the world forge is changed on purpose.
// With --replay it replays the checked-in corpus instead and writes the report file CI compares across platforms.
int main(int argc, char** argv)
{
    using namespace ichor::bridge_corpus;
    if (argc >= 2 && std::string_view(argv[1]) == "--replay") {
        if (argc > 3) {
            std::fputs("usage: combat_bridge_corpus --replay [report-file]\n", stderr);
            return 2;
        }
        return replay(argc == 3 ? argv[2] : "");
    }
    if (argc > 2) {
        std::fputs("usage: combat_bridge_corpus [output-dir] | --replay [report-file]\n", stderr);
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
