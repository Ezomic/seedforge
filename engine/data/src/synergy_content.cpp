#include "ichor/data/synergy_content.h"

#include <algorithm>
#include <map>
#include <system_error>

namespace ichor {

namespace {

constexpr std::size_t kMaxFiles = 256;

void need(bool present, const std::string& path, const std::string& rule, std::vector<std::string>& problems, const std::string& label, bool required)
{
    if (required && !present) {
        problems.push_back(label + ": " + path + ": required for content tier " + rule);
    } else if (!required && present) {
        problems.push_back(label + ": " + path + ": not allowed for content tier " + rule);
    }
}

}

bool validateSynergyContent(std::string_view label, const std::vector<Synergy>& synergies, std::vector<std::string>& problems)
{
    const std::size_t before = problems.size();
    const std::string prefix(label);
    for (const Synergy& s : synergies) {
        const std::string at = s.id + ".";
        if (s.contentTier == ContentTier::Unset) {
            problems.push_back(prefix + ": " + at + "content_tier: missing (expected 1 or 2)");
            continue;
        }
        const bool flagship = s.contentTier == ContentTier::Flagship;
        const std::string rule = flagship ? "1" : "2";
        need(!s.worldName.empty(), at + "world_name", rule, problems, prefix, true);
        need(!s.faction.empty(), at + "faction", rule, problems, prefix, flagship);
        need(!s.dungeonType.empty(), at + "dungeon_type", rule, problems, prefix, flagship);
        need(!s.mechanic.empty(), at + "mechanic", rule, problems, prefix, flagship);
        if (!flagship) {
            need(!s.flavour.empty(), at + "flavour", rule, problems, prefix, true);
        }
    }
    return problems.size() == before;
}

bool loadSynergyContentFile(const std::filesystem::path& file, const AxiomPool& pool, std::vector<Synergy>& out, std::vector<std::string>& problems)
{
    std::vector<Synergy> parsed;
    const std::size_t before = problems.size();
    if (!loadSynergyFile(file, pool, parsed, problems)) {
        return false;
    }
    std::string label;
    try {
        label = file.generic_string();
    } catch (const std::exception&) {
        label = "<unprintable path>";
    }
    if (!validateSynergyContent(label, parsed, problems)) {
        return false;
    }
    out = std::move(parsed);
    return problems.size() == before;
}

bool loadSynergyContentDirectory(const std::filesystem::path& directory, const AxiomPool& pool, std::vector<Synergy>& out, std::vector<std::string>& problems)
{
    const std::string dirLabel = [&] {
        try {
            return directory.generic_string();
        } catch (const std::exception&) {
            return std::string("<unprintable path>");
        }
    }();
    std::vector<std::filesystem::path> files;
    std::error_code code;
    for (std::filesystem::directory_iterator it(directory, code), end; !code && it != end; it.increment(code)) {
        if (it->is_regular_file(code) && it->path().extension() == ".json") {
            files.push_back(it->path());
            if (files.size() > kMaxFiles) {
                problems.push_back(dirLabel + ": more than " + std::to_string(kMaxFiles) + " json files");
                return false;
            }
        }
    }
    if (code) {
        problems.push_back(dirLabel + ": cannot read directory: " + code.message());
        return false;
    }
    std::sort(files.begin(), files.end());

    const std::size_t before = problems.size();
    std::vector<Synergy> all;
    std::map<std::string, std::string> idFile;
    std::map<std::vector<std::string>, std::string> axiomsId;
    for (const std::filesystem::path& file : files) {
        std::vector<Synergy> defs;
        if (!loadSynergyContentFile(file, pool, defs, problems)) {
            continue;
        }
        const std::string name = file.filename().generic_string();
        for (Synergy& s : defs) {
            if (const auto seen = idFile.emplace(s.id, name); !seen.second) {
                problems.push_back(name + ": " + s.id + ": duplicate id \"" + s.id + "\" (also in " + seen.first->second + ")");
            }
            if (const auto same = axiomsId.emplace(s.axioms, s.id); !same.second) {
                problems.push_back(name + ": " + s.id + ": same axioms as \"" + same.first->second + "\"");
            }
            all.push_back(std::move(s));
        }
    }
    if (problems.size() != before) {
        return false;
    }
    std::sort(all.begin(), all.end(), [](const Synergy& a, const Synergy& b) { return a.id < b.id; });
    out = std::move(all);
    return true;
}

}
