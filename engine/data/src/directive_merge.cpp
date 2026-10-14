#include "ichor/data/directive_merge.h"

#include "ichor/core/hash.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <set>
#include <string_view>

namespace ichor {

namespace {

using Words = std::vector<std::string>;
using Weights = std::map<std::string, double, std::less<>>;

struct Opposed {
    std::string_view block;
    std::string_view adds;
    std::string_view removes;
};

constexpr std::array<Opposed, 3> kOpposed = {{
    {"biomes", "inject", "suppress"},
    {"enemies", "roster_add", "roster_remove"},
    {"economy", "abundant", "scarce"},
}};

const Opposed* opposedKey(std::string_view block, std::string_view key)
{
    for (const Opposed& pair : kOpposed) {
        if (pair.block == block && (pair.adds == key || pair.removes == key)) {
            return &pair;
        }
    }
    return nullptr;
}

double clampNumber(double value, double low)
{
    return std::min(kDirectiveMaxMagnitude, std::max(low, value));
}

// Candidates for one field, in priority order.
using Candidates = std::vector<std::pair<AxiomSlot, const AxiomValue*>>;

bool usable(const AxiomValue& value)
{
    const double* number = std::get_if<double>(&value);
    return number == nullptr || std::isfinite(*number);
}

const AxiomValue* findValue(const Axiom& axiom, std::string_view block, std::string_view key)
{
    const auto b = axiom.blocks.find(block);
    if (b == axiom.blocks.end()) {
        return nullptr;
    }
    const auto k = b->second.find(key);
    return k == b->second.end() ? nullptr : &k->second;
}

void addSlot(std::vector<AxiomSlot>& slots, AxiomSlot slot)
{
    if (std::find(slots.begin(), slots.end(), slot) == slots.end()) {
        slots.push_back(slot);
    }
}

std::vector<AxiomSlot> inSlotOrder(const std::set<AxiomSlot>& slots)
{
    return {slots.begin(), slots.end()};
}

bool mergeField(const Candidates& candidates, AxiomValue& out, std::vector<AxiomSlot>& slots)
{
    const Candidates usableOnes = [&] {
        Candidates kept;
        std::copy_if(candidates.begin(), candidates.end(), std::back_inserter(kept), [](const auto& c) { return usable(*c.second); });
        return kept;
    }();
    if (usableOnes.empty()) {
        return false;
    }
    const std::size_t type = usableOnes.front().second->index();
    std::set<AxiomSlot> contributing;
    if (const double* number = std::get_if<double>(usableOnes.front().second)) {
        out = clampNumber(*number, -kDirectiveMaxMagnitude);
        contributing.insert(usableOnes.front().first);
    } else if (const std::string* text = std::get_if<std::string>(usableOnes.front().second)) {
        out = *text;
        contributing.insert(usableOnes.front().first);
    } else if (std::holds_alternative<Words>(*usableOnes.front().second)) {
        std::set<std::string> merged;
        for (const auto& [slot, value] : usableOnes) {
            if (value->index() != type) {
                continue;
            }
            const Words& list = std::get<Words>(*value);
            merged.insert(list.begin(), list.end());
            if (!list.empty()) {
                contributing.insert(slot);
            }
        }
        if (merged.empty()) {
            return false;
        }
        out = Words(merged.begin(), merged.end());
    } else {
        Weights merged;
        for (const auto& [slot, value] : usableOnes) {
            if (value->index() != type) {
                continue;
            }
            for (const auto& [word, weight] : std::get<Weights>(*value)) {
                if (std::isfinite(weight) && merged.emplace(word, clampNumber(weight, 0.0)).second) {
                    contributing.insert(slot);
                }
            }
        }
        if (merged.empty()) {
            return false;
        }
        out = std::move(merged);
    }
    slots = inSlotOrder(contributing);
    return true;
}

}

MergeResult mergeDirectives(std::span<const Axiom> axioms)
{
    MergeResult result;

    std::vector<const Axiom*> ranked;
    for (const Axiom& axiom : axioms) {
        ranked.push_back(&axiom);
    }
    std::sort(ranked.begin(), ranked.end(), [](const Axiom* l, const Axiom* r) { return l->slot != r->slot ? l->slot < r->slot : l->id < r->id; });
    std::vector<const Axiom*> kept;
    for (const Axiom* axiom : ranked) {
        if (!kept.empty() && kept.back()->slot == axiom->slot) {
            result.problems.push_back("slot " + std::string(1, axiomSlotLetter(axiom->slot)) + " already holds " + kept.back()->id + ", skipped " + axiom->id);
        } else {
            kept.push_back(axiom);
        }
    }

    std::map<std::string, std::set<std::string>, std::less<>> fields;
    for (const Axiom* axiom : kept) {
        for (const auto& [block, values] : axiom->blocks) {
            for (const auto& entry : values) {
                fields[block].insert(entry.first);
            }
        }
    }

    for (const auto& [block, keys] : fields) {
        AxiomBlock merged;
        std::vector<FieldSource> sources;
        const auto emit = [&](const std::string& key, AxiomValue value, std::vector<AxiomSlot> slots) {
            merged.emplace(key, std::move(value));
            sources.push_back({block, key, std::move(slots)});
        };

        for (const Opposed& pair : kOpposed) {
            if (pair.block != block) {
                continue;
            }
            std::map<std::string, std::pair<AxiomSlot, bool>, std::less<>> decided;
            for (const Axiom* axiom : kept) {
                const AxiomValue* adds = findValue(*axiom, block, pair.adds);
                const AxiomValue* removes = findValue(*axiom, block, pair.removes);
                const Words none;
                const Words& addList = adds != nullptr && std::holds_alternative<Words>(*adds) ? std::get<Words>(*adds) : none;
                const Words& removeList = removes != nullptr && std::holds_alternative<Words>(*removes) ? std::get<Words>(*removes) : none;
                std::map<std::string, bool, std::less<>> own;
                for (const std::string& word : addList) {
                    own[word] = false;
                }
                for (const std::string& word : removeList) {
                    own[word] = true;
                }
                for (const auto& [word, removing] : own) {
                    decided.emplace(word, std::pair{axiom->slot, removing});
                }
            }
            for (const bool removing : {false, true}) {
                Words words;
                std::set<AxiomSlot> slots;
                for (const auto& [word, verdict] : decided) {
                    if (verdict.second == removing) {
                        words.push_back(word);
                        slots.insert(verdict.first);
                    }
                }
                if (!words.empty()) {
                    emit(std::string(removing ? pair.removes : pair.adds), std::move(words), inSlotOrder(slots));
                }
            }
        }

        for (const std::string& key : keys) {
            if (opposedKey(block, key) != nullptr) {
                continue;
            }
            Candidates candidates;
            for (const Axiom* axiom : kept) {
                if (const AxiomValue* value = findValue(*axiom, block, key)) {
                    candidates.emplace_back(axiom->slot, value);
                }
            }
            AxiomValue value;
            std::vector<AxiomSlot> slots;
            if (mergeField(candidates, value, slots)) {
                emit(key, std::move(value), std::move(slots));
            }
        }

        if (!merged.empty()) {
            result.directives.blocks.emplace(block, std::move(merged));
            std::sort(sources.begin(), sources.end(), [](const FieldSource& l, const FieldSource& r) { return l.key < r.key; });
            for (FieldSource& source : sources) {
                result.provenance.push_back(std::move(source));
            }
        }
    }
    return result;
}

namespace {

void appendQuoted(std::string& out, std::string_view text)
{
    out += '"';
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    out += '"';
}

// Six decimals as integers: the same text on every platform, no float formatting.
void appendFixed(std::string& out, double value)
{
    const std::int64_t micro = static_cast<std::int64_t>(std::llround(value * 1.0e6));
    const std::uint64_t magnitude = micro < 0 ? static_cast<std::uint64_t>(-micro) : static_cast<std::uint64_t>(micro);
    std::string fraction = std::to_string(magnitude % 1000000);
    out += micro < 0 ? "-" : "";
    out += std::to_string(magnitude / 1000000) + "." + std::string(6 - fraction.size(), '0') + fraction;
}

}

std::string mergedCanonicalText(const MergeResult& result)
{
    std::string out = "{";
    bool firstBlock = true;
    for (const auto& [block, values] : result.directives.blocks) {
        out += firstBlock ? "" : ",";
        firstBlock = false;
        appendQuoted(out, block);
        out += ":{";
        bool firstKey = true;
        for (const auto& [key, value] : values) {
            out += firstKey ? "" : ",";
            firstKey = false;
            appendQuoted(out, key);
            out += ':';
            if (const double* number = std::get_if<double>(&value)) {
                appendFixed(out, *number);
            } else if (const std::string* text = std::get_if<std::string>(&value)) {
                appendQuoted(out, *text);
            } else if (const Words* list = std::get_if<Words>(&value)) {
                out += '[';
                for (std::size_t i = 0; i < list->size(); ++i) {
                    out += i == 0 ? "" : ",";
                    appendQuoted(out, (*list)[i]);
                }
                out += ']';
            } else {
                out += '{';
                bool firstWeight = true;
                for (const auto& [word, weight] : std::get<Weights>(value)) {
                    out += firstWeight ? "" : ",";
                    firstWeight = false;
                    appendQuoted(out, word);
                    out += ':';
                    appendFixed(out, weight);
                }
                out += '}';
            }
        }
        out += '}';
    }
    out += "}|";
    for (const FieldSource& source : result.provenance) {
        out += source.block + "." + source.key + ":";
        for (const AxiomSlot slot : source.slots) {
            out += axiomSlotLetter(slot);
        }
        out += ';';
    }
    return out;
}

std::uint64_t mergedDigest(const MergeResult& result)
{
    return hash64(mergedCanonicalText(result));
}

}
