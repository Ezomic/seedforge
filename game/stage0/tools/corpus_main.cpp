#include "ichor/stage0/replay_corpus.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// Re-records the Stage 0 replay corpus: one file per fight in the plan, replacing the folder's *.replay files.
// Run it through bin/regen-stage0-corpus after a combat knob is changed on purpose.
int main(int argc, char** argv)
{
    using namespace ichor::stage0;
    if (argc > 2) {
        std::fputs("usage: stage0_corpus [output-dir]\n", stderr);
        return 2;
    }
    const std::filesystem::path dir = argc == 2 ? argv[1] : ICHOR_CORPUS_DIR;
    std::vector<CorpusEntry> entries;
    for (const CorpusSpec& spec : corpusPlan()) {
        auto entry = recordCorpusEntry(spec.skill, spec.seed);
        if (!entry) {
            std::fprintf(stderr, "stage0_corpus: %.*s seed %llu did not finish\n", static_cast<int>(spec.skill.size()), spec.skill.data(),
                static_cast<unsigned long long>(spec.seed));
            return 1;
        }
        entries.push_back(std::move(*entry));
    }

    std::error_code error;
    std::filesystem::create_directories(dir, error);
    if (error) {
        std::fprintf(stderr, "stage0_corpus: cannot create %s\n",dir.string().c_str());
        return 1;
    }
    for (const auto& item : std::filesystem::directory_iterator(dir, error)) {
        if (item.path().extension() == ".replay") std::filesystem::remove(item.path(), error);
    }
    int index = 0;
    for (const CorpusEntry& entry : entries) {
        index += 1;
        char prefix[16];
        std::snprintf(prefix, sizeof(prefix), "%02d-", index);
        const std::filesystem::path path = dir / (std::string(prefix) + entry.name + ".replay");
        std::ofstream out(path, std::ios::binary);
        out << encodeCorpusEntry(entry);
        if (!out) {
            std::fprintf(stderr, "stage0_corpus: cannot write %s\n",path.string().c_str());
            return 1;
        }
        std::printf("%s: %s in %d ticks (%s)\n",path.filename().string().c_str(), entry.expected.playerLost ? "foe wins" : "player wins",
            entry.expected.ticks, name(entry.expected.cause));
    }
    return 0;
}
