#include "ichor/data/synergy.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <system_error>
#include <fstream>
#include <iterator>
#include <set>

namespace ichor {

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaxFileBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaxDefinitions = 4096;
constexpr std::size_t kMaxIdBytes = 64;
constexpr int kMaxDepth = 8;

constexpr std::array<SynergyTier, 6> kTiers = {SynergyTier::Triple, SynergyTier::CataclysmLaw, SynergyTier::CataclysmPowers, SynergyTier::LawPowers, SynergyTier::Pressure, SynergyTier::Anomaly};

std::string_view typeName(const Json& json)
{
    if (json.is_null()) {
        return "null";
    }
    if (json.is_boolean()) {
        return "boolean";
    }
    if (json.is_number()) {
        return "number";
    }
    if (json.is_string()) {
        return "string";
    }
    if (json.is_array()) {
        return "array";
    }
    return "object";
}

std::string shown(std::string_view text)
{
    constexpr std::size_t kLimit = 40;
    std::string out = "\"";
    out.append(text.substr(0, kLimit));
    if (text.size() > kLimit) {
        out += "...";
    }
    return out + "\"";
}

bool isBlank(std::string_view text)
{
    return std::all_of(text.begin(), text.end(), [](unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; });
}

// [a-z][a-z0-9_]*
bool isWord(std::string_view text)
{
    return !text.empty() && text[0] >= 'a' && text[0] <= 'z' && std::all_of(text.begin(), text.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

bool nestsTooDeep(std::string_view text)
{
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (const char c : text) {
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
        } else if (c == '"') {
            inString = true;
        } else if (c == '[' || c == '{') {
            if (++depth > kMaxDepth) {
                return true;
            }
        } else if ((c == ']' || c == '}') && depth > 0) {
            --depth;
        }
    }
    return false;
}

std::string tierChoices()
{
    std::string out;
    for (const SynergyTier tier : kTiers) {
        out += out.empty() ? "" : ", ";
        out += synergyTierName(tier);
    }
    return out;
}

bool has(const std::vector<AxiomSlot>& slots, AxiomSlot slot)
{
    return std::find(slots.begin(), slots.end(), slot) != slots.end();
}

// What the doc's tier names imply about the slots of the axioms. Empty when satisfied.
std::string slotProblem(SynergyTier tier, const std::vector<AxiomSlot>& slots)
{
    const std::string name(synergyTierName(tier));
    const auto needs = [&](bool ok, const std::string& what) { return ok ? std::string() : name + " needs " + what; };
    switch (tier) {
    case SynergyTier::CataclysmLaw:
        return needs(has(slots, AxiomSlot::Cataclysm) && has(slots, AxiomSlot::Law), "one cataclysm and one law axiom");
    case SynergyTier::CataclysmPowers:
        return needs(has(slots, AxiomSlot::Cataclysm) && has(slots, AxiomSlot::Powers), "one cataclysm and one powers axiom");
    case SynergyTier::LawPowers:
        return needs(has(slots, AxiomSlot::Law) && has(slots, AxiomSlot::Powers), "one law and one powers axiom");
    case SynergyTier::Pressure:
        return needs(has(slots, AxiomSlot::Pressure) && !has(slots, AxiomSlot::Anomaly) && std::count(slots.begin(), slots.end(), AxiomSlot::Pressure) == 1, "exactly one pressure axiom and no anomaly axiom");
    case SynergyTier::Anomaly:
        return needs(has(slots, AxiomSlot::Anomaly) && std::count(slots.begin(), slots.end(), AxiomSlot::Anomaly) == 1, "exactly one anomaly axiom");
    case SynergyTier::Triple: {
        std::set<AxiomSlot> distinct(slots.begin(), slots.end());
        return needs(distinct.size() == slots.size(), "three axioms from three different slots");
    }
    }
    return {};
}

}

std::string_view synergyTierName(SynergyTier tier)
{
    switch (tier) {
    case SynergyTier::Triple:
        return "triple";
    case SynergyTier::CataclysmLaw:
        return "cataclysm_law";
    case SynergyTier::CataclysmPowers:
        return "cataclysm_powers";
    case SynergyTier::LawPowers:
        return "law_powers";
    case SynergyTier::Pressure:
        return "pressure";
    case SynergyTier::Anomaly:
        return "anomaly";
    }
    return "";
}

AxiomPool axiomPool(const AxiomSet& axioms)
{
    AxiomPool pool;
    for (const auto& [id, axiom] : axioms.axioms()) {
        pool.emplace(id, axiom.slot);
    }
    return pool;
}

bool parseSynergies(std::string_view label, std::string_view text, const AxiomPool& pool, std::vector<Synergy>& out, std::vector<std::string>& problems)
{
    const std::size_t before = problems.size();
    const auto add = [&](const std::string& path, const std::string& message) {
        problems.push_back(std::string(label) + ": " + (path.empty() ? "" : path + ": ") + message);
    };
    if (text.size() > kMaxFileBytes) {
        add("", "file is larger than " + std::to_string(kMaxFileBytes) + " bytes");
        return false;
    }
    if (isBlank(text)) {
        add("", "empty file");
        return false;
    }
    if (nestsTooDeep(text)) {
        add("", "nested deeper than " + std::to_string(kMaxDepth) + " levels");
        return false;
    }
    const Json json = Json::parse(text.begin(), text.end(), nullptr, false);
    if (json.is_discarded()) {
        add("", "not valid JSON");
        return false;
    }
    if (!json.is_object()) {
        add("", "expected an object, got " + std::string(typeName(json)));
        return false;
    }
    for (const auto& [key, item] : json.items()) {
        if (key != "synergies") {
            add(key.substr(0, 40), "unknown key");
        }
    }
    const auto list = json.find("synergies");
    if (list == json.end()) {
        add("synergies", "missing");
        return false;
    }
    if (!list->is_array()) {
        add("synergies", "expected an array, got " + std::string(typeName(*list)));
        return false;
    }
    if (list->size() > kMaxDefinitions) {
        add("synergies", "has more than " + std::to_string(kMaxDefinitions) + " entries");
        return false;
    }

    std::vector<Synergy> parsed;
    std::set<std::string> seen;
    for (std::size_t i = 0; i < list->size(); ++i) {
        const Json& item = (*list)[i];
        const std::string where = "synergies[" + std::to_string(i) + "]";
        if (!item.is_object()) {
            add(where, "expected an object, got " + std::string(typeName(item)));
            continue;
        }
        Synergy synergy;
        bool tierKnown = false;
        for (const auto& [key, value] : item.items()) {
            if (key != "id" && key != "tier" && key != "axioms") {
                add(where + "." + key.substr(0, 40), "unknown key");
            }
        }

        if (const auto it = item.find("id"); it == item.end()) {
            add(where + ".id", "missing");
        } else if (!it->is_string()) {
            add(where + ".id", "expected a string, got " + std::string(typeName(*it)));
        } else {
            const std::string& id = it->get_ref<const std::string&>();
            if (id.size() > kMaxIdBytes || !isWord(id)) {
                add(where + ".id", "expected a lowercase ascii id (letters, digits, underscores) of at most " + std::to_string(kMaxIdBytes) + " bytes, got " + shown(id));
            } else if (!seen.insert(id).second) {
                add(where + ".id", "duplicate " + shown(id));
            } else {
                synergy.id = id;
            }
        }

        if (const auto it = item.find("tier"); it == item.end()) {
            add(where + ".tier", "missing");
        } else if (!it->is_string()) {
            add(where + ".tier", "expected a string, got " + std::string(typeName(*it)));
        } else {
            const auto tier = std::find_if(kTiers.begin(), kTiers.end(), [&](SynergyTier t) { return synergyTierName(t) == it->get_ref<const std::string&>(); });
            if (tier == kTiers.end()) {
                add(where + ".tier", "expected one of " + tierChoices() + ", got " + shown(it->get_ref<const std::string&>()));
            } else {
                synergy.tier = *tier;
                tierKnown = true;
            }
        }

        bool axiomsOk = false;
        std::vector<AxiomSlot> slots;
        if (const auto it = item.find("axioms"); it == item.end()) {
            add(where + ".axioms", "missing");
        } else if (!it->is_array()) {
            add(where + ".axioms", "expected an array, got " + std::string(typeName(*it)));
        } else if (const std::size_t want = tierKnown && synergy.tier == SynergyTier::Triple ? 3 : 2; tierKnown && it->size() != want) {
            add(where + ".axioms", "expected " + std::to_string(want) + " axioms for tier " + std::string(synergyTierName(synergy.tier)) + ", got " + std::to_string(it->size()));
        } else if (!tierKnown && it->size() > 3) {
            add(where + ".axioms", "expected 2 or 3 axioms, got " + std::to_string(it->size()));
        } else {
            axiomsOk = true;
            std::set<std::string> ids;
            for (std::size_t a = 0; a < it->size(); ++a) {
                const std::string at = where + ".axioms[" + std::to_string(a) + "]";
                const Json& entry = (*it)[a];
                if (!entry.is_string()) {
                    add(at, "expected a string, got " + std::string(typeName(entry)));
                    axiomsOk = false;
                    continue;
                }
                const std::string& id = entry.get_ref<const std::string&>();
                const auto known = pool.find(id);
                if (known == pool.end()) {
                    add(at, "unknown axiom " + shown(id));
                    axiomsOk = false;
                } else if (!ids.insert(id).second) {
                    add(at, "duplicate " + shown(id) + "; a synergy cannot require the same axiom twice");
                    axiomsOk = false;
                } else {
                    slots.push_back(known->second);
                }
            }
            synergy.axioms.assign(ids.begin(), ids.end());
        }
        if (axiomsOk && tierKnown) {
            if (const std::string problem = slotProblem(synergy.tier, slots); !problem.empty()) {
                add(where + ".tier", problem + ", got " + [&] {
                    std::string names;
                    for (const AxiomSlot slot : slots) {
                        names += names.empty() ? "" : " + ";
                        names += axiomSlotName(slot);
                    }
                    return names;
                }());
                axiomsOk = false;
            }
        }
        if (axiomsOk && tierKnown && !synergy.id.empty()) {
            parsed.push_back(std::move(synergy));
        }
    }

    if (problems.size() != before) {
        return false;
    }
    std::sort(parsed.begin(), parsed.end(), [](const Synergy& a, const Synergy& b) { return a.id < b.id; });
    out = std::move(parsed);
    return true;
}

bool loadSynergyFile(const std::filesystem::path& file, const AxiomPool& pool, std::vector<Synergy>& out, std::vector<std::string>& problems)
{
    std::string label;
    try {
        label = file.generic_string();
    } catch (const std::exception&) {
        label = "<unprintable path>";
    }
    std::error_code code;
    const std::uintmax_t size = std::filesystem::file_size(file, code);
    if (code) {
        problems.push_back(label + ": cannot read file: " + code.message());
        return false;
    }
    if (size > kMaxFileBytes) {
        problems.push_back(label + ": file is larger than " + std::to_string(kMaxFileBytes) + " bytes");
        return false;
    }
    std::ifstream stream(file, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return parseSynergies(label, text, pool, out, problems);
}

std::vector<std::string> detectSynergies(const std::vector<std::string>& axiomIds, const std::vector<Synergy>& definitions)
{
    const std::set<std::string, std::less<>> present(axiomIds.begin(), axiomIds.end());
    std::vector<const Synergy*> fired;
    for (const Synergy& synergy : definitions) {
        if (!synergy.axioms.empty() && std::all_of(synergy.axioms.begin(), synergy.axioms.end(), [&](const std::string& id) { return present.count(id) != 0; })) {
            fired.push_back(&synergy);
        }
    }
    std::sort(fired.begin(), fired.end(), [](const Synergy* a, const Synergy* b) {
        return a->tier != b->tier ? a->tier < b->tier : a->id < b->id;
    });
    if (fired.size() > kMaxActiveSynergies) {
        fired.resize(kMaxActiveSynergies);
    }
    std::vector<std::string> ids;
    for (const Synergy* synergy : fired) {
        ids.push_back(synergy->id);
    }
    return ids;
}

}
