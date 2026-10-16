#include "ichor/ui/world_screen_model.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string_view>

namespace ichor {

namespace {

bool isContinuation(unsigned char c)
{
    return (c & 0xC0) == 0x80;
}

// Control characters become spaces, runs of spaces collapse, the ends are trimmed, and the cut
// never lands inside a UTF-8 sequence.
std::string clean(std::string_view text)
{
    std::string out;
    out.reserve(std::min(text.size(), kWorldScreenMaxTextBytes + 4));
    for (const char raw : text) {
        const auto c = static_cast<unsigned char>(raw);
        const bool space = c < 0x20 || c == 0x7f || c == ' ';
        if (space) {
            if (!out.empty() && out.back() != ' ') {
                out += ' ';
            }
        } else {
            out += raw;
        }
        if (out.size() > kWorldScreenMaxTextBytes + 4) {
            break;
        }
    }
    if (out.size() > kWorldScreenMaxTextBytes) {
        std::size_t cut = kWorldScreenMaxTextBytes;
        while (cut > 0 && isContinuation(static_cast<unsigned char>(out[cut]))) {
            --cut;
        }
        out.resize(cut);
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

std::string labelOrId(std::string_view label, std::string_view id)
{
    std::string text = clean(label);
    return text.empty() ? clean(id) : text;
}

std::optional<std::int32_t> toPercent(double fraction)
{
    if (!std::isfinite(fraction)) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(std::lround(std::clamp(fraction, 0.0, 1.0) * 100.0));
}

void capLines(WorldScreenSection& section, std::vector<WorldScreenLine>& lines, std::size_t cap)
{
    section.omitted = lines.size() > cap ? lines.size() - cap : 0;
    lines.resize(std::min(lines.size(), cap));
    section.lines = std::move(lines);
}

}

std::vector<WorldScreenSection> buildWorldScreen(const WorldScreenInput& input)
{
    std::vector<WorldScreenSection> view;

    std::vector<const WorldScreenAxiom*> shown;
    std::vector<const WorldScreenAxiom*> pressures;
    for (const auto& axiom : input.axioms) {
        const bool hidden = (axiom.slot == AxiomSlot::Pressure || axiom.slot == AxiomSlot::Anomaly) && !axiom.discovered;
        if (hidden || axiom.id.empty()) {
            continue;
        }
        if (axiom.slot == AxiomSlot::Pressure) {
            pressures.push_back(&axiom);
        } else {
            shown.push_back(&axiom);
        }
    }
    auto byId = [](const WorldScreenAxiom* a, const WorldScreenAxiom* b) {
        return a->slot != b->slot ? a->slot < b->slot : a->id < b->id;
    };
    std::sort(shown.begin(), shown.end(), byId);
    std::sort(pressures.begin(), pressures.end(), byId);

    {
        std::vector<WorldScreenLine> lines;
        std::set<std::string_view> seen;
        for (const auto* axiom : shown) {
            if (seen.insert(axiom->id).second) {
                lines.push_back({labelOrId(axiom->title, axiom->id), std::nullopt});
            }
        }
        if (!lines.empty()) {
            WorldScreenSection section{WorldScreenSectionKind::Axioms, "Axioms", {}, 0};
            capLines(section, lines, kWorldScreenMaxAxiomLines);
            view.push_back(std::move(section));
        }
    }

    {
        std::vector<WorldScreenLine> lines;
        std::set<std::string_view> seen;
        for (const auto& synergy : input.synergies) {
            if (synergy.discovered && !synergy.id.empty() && seen.insert(synergy.id).second) {
                lines.push_back({labelOrId(synergy.worldName, synergy.id), std::nullopt});
            }
        }
        if (!lines.empty()) {
            WorldScreenSection section{WorldScreenSectionKind::Synergies, "Synergies", {}, 0};
            capLines(section, lines, kWorldScreenMaxSynergyLines);
            view.push_back(std::move(section));
        }
    }

    if (!pressures.empty()) {
        const auto* axiom = pressures.front();
        WorldScreenSection section{WorldScreenSectionKind::Pressure, "Pressure", {}, 0};
        section.lines.push_back({labelOrId(axiom->title, axiom->id), input.pressureLevel ? toPercent(*input.pressureLevel) : std::nullopt});
        view.push_back(std::move(section));
    }

    {
        struct Row {
            std::string faction;
            std::int32_t percent;
            double share;
        };
        std::vector<Row> rows;
        for (const auto& entry : input.territory) {
            std::string name = clean(entry.faction);
            if (name.empty() || !std::isfinite(entry.share) || entry.share <= 0.0) {
                continue;
            }
            rows.push_back({std::move(name), *toPercent(entry.share), std::min(entry.share, 1.0)});
        }
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
            return a.share != b.share ? a.share > b.share : a.faction < b.faction;
        });
        std::vector<WorldScreenLine> lines;
        for (auto& row : rows) {
            lines.push_back({std::move(row.faction), row.percent});
        }
        if (!lines.empty()) {
            WorldScreenSection section{WorldScreenSectionKind::Territory, "Territory", {}, 0};
            capLines(section, lines, kWorldScreenMaxTerritoryLines);
            view.push_back(std::move(section));
        }
    }

    return view;
}

}
