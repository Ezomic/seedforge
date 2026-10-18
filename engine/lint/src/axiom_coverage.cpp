#include "ichor/lint/axiom_coverage.h"

#include "ichor/data/synergy_content.h"

#include <algorithm>
#include <map>
#include <utility>

namespace ichor::lint {

namespace {

enum class PairState { None, Flagship, TextOnly, Excluded };

using Pair = std::pair<std::string, std::string>;

Pair orderedPair(const std::string& a, const std::string& b)
{
    return a < b ? Pair{a, b} : Pair{b, a};
}

bool excluded(const Axiom& a, const Axiom& b)
{
    if (a.slot == b.slot) return true;
    return std::find(a.conflicts.begin(), a.conflicts.end(), b.id) != a.conflicts.end()
        || std::find(b.conflicts.begin(), b.conflicts.end(), a.id) != b.conflicts.end();
}

char symbol(PairState state)
{
    switch (state) {
    case PairState::Flagship: return 'F';
    case PairState::TextOnly: return 'T';
    case PairState::Excluded: return 'x';
    case PairState::None: break;
    }
    return '.';
}

std::string code(const std::string& text)
{
    return "`" + text + "`";
}

}

std::string renderAxiomCoverage(const AxiomSet& axioms, const std::vector<Synergy>& synergies)
{
    std::vector<const Axiom*> order;
    for (const auto& [id, axiom] : axioms.axioms()) order.push_back(&axiom);
    std::stable_sort(order.begin(), order.end(), [](const Axiom* a, const Axiom* b) { return a->slot < b->slot; });

    std::map<Pair, const Synergy*> pairSynergies;
    std::vector<const Synergy*> triples;
    for (const Synergy& synergy : synergies) {
        if (synergy.axioms.size() == 2) pairSynergies[orderedPair(synergy.axioms[0], synergy.axioms[1])] = &synergy;
        else if (synergy.axioms.size() == 3) triples.push_back(&synergy);
    }
    std::sort(triples.begin(), triples.end(), [](const Synergy* a, const Synergy* b) { return a->id < b->id; });

    const std::size_t count = order.size();
    std::vector<std::vector<PairState>> grid(count, std::vector<PairState>(count, PairState::None));
    std::size_t flagship = 0;
    std::size_t textOnly = 0;
    std::size_t none = 0;
    std::size_t blocked = 0;
    std::vector<std::size_t> rowFlagship(count, 0);
    std::vector<std::size_t> rowText(count, 0);
    std::vector<std::size_t> rowNone(count, 0);
    std::vector<std::size_t> rowBlocked(count, 0);
    std::vector<Pair> uncovered;
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t j = i + 1; j < count; ++j) {
            const auto found = pairSynergies.find(orderedPair(order[i]->id, order[j]->id));
            PairState state = PairState::None;
            if (found != pairSynergies.end()) state = found->second->contentTier == ContentTier::Flagship ? PairState::Flagship : PairState::TextOnly;
            else if (excluded(*order[i], *order[j])) state = PairState::Excluded;
            grid[i][j] = grid[j][i] = state;
            for (const std::size_t row : {i, j}) {
                if (state == PairState::Flagship) ++rowFlagship[row];
                else if (state == PairState::TextOnly) ++rowText[row];
                else if (state == PairState::Excluded) ++rowBlocked[row];
                else ++rowNone[row];
            }
            if (state == PairState::Flagship) ++flagship;
            else if (state == PairState::TextOnly) ++textOnly;
            else if (state == PairState::Excluded) ++blocked;
            else {
                ++none;
                uncovered.push_back(orderedPair(order[i]->id, order[j]->id));
            }
        }
    }
    std::sort(uncovered.begin(), uncovered.end());

    std::string out;
    out += "# Axiom coverage matrix\n\n";
    out += "Generated from `data/axioms` and `data/synergies`. Do not edit by hand: regenerate with\n";
    out += "`ichor_lint --coverage-out docs/axiom-coverage.md` (a test fails when this file is out of date).\n\n";

    out += "## Summary\n\n";
    out += "| | Pairs |\n|---|---|\n";
    out += "| Flagship synergy (content tier 1) | " + std::to_string(flagship) + " |\n";
    out += "| Text-only synergy (content tier 2) | " + std::to_string(textOnly) + " |\n";
    out += "| No synergy, can roll together | " + std::to_string(none) + " |\n";
    out += "| Excluded (same slot or declared conflict) | " + std::to_string(blocked) + " |\n";
    out += "| Total | " + std::to_string(flagship + textOnly + none + blocked) + " |\n\n";
    out += "Axioms: " + std::to_string(count) + ". Triple synergies: " + std::to_string(triples.size()) + ".\n\n";

    out += "## Per axiom\n\n";
    out += "| # | Axiom | Slot | Flagship | Text only | None | Excluded |\n|---|---|---|---|---|---|---|\n";
    for (std::size_t i = 0; i < count; ++i) {
        out += "| " + std::to_string(i + 1) + " | " + code(order[i]->id) + " | " + std::string(axiomSlotName(order[i]->slot)) + " | " + std::to_string(rowFlagship[i]) + " | "
            + std::to_string(rowText[i]) + " | " + std::to_string(rowNone[i]) + " | " + std::to_string(rowBlocked[i]) + " |\n";
    }

    out += "\n## Pair matrix\n\n";
    out += "`F` flagship, `T` text only, `.` none, `x` excluded, `-` the axiom itself. Numbers are the # column above.\n\n";
    out += "| # |";
    for (std::size_t j = 0; j < count; ++j) out += " " + std::to_string(j + 1) + " |";
    out += "\n|---|";
    for (std::size_t j = 0; j < count; ++j) out += "---|";
    out += "\n";
    for (std::size_t i = 0; i < count; ++i) {
        out += "| " + std::to_string(i + 1) + " |";
        for (std::size_t j = 0; j < count; ++j) out += std::string(" ") + (i == j ? '-' : symbol(grid[i][j])) + " |";
        out += "\n";
    }

    out += "\n## Pairs to author next\n\n";
    if (uncovered.empty()) out += "Every pair that can roll together has a synergy.\n";
    for (const Pair& pair : uncovered) out += "- " + code(pair.first) + " + " + code(pair.second) + "\n";

    out += "\n## Triples\n\n";
    if (triples.empty()) out += "No triple synergies yet.\n";
    for (const Synergy* triple : triples) {
        out += "- " + code(triple->id) + " (" + (triple->contentTier == ContentTier::Flagship ? "flagship" : "text only") + "): ";
        for (std::size_t k = 0; k < triple->axioms.size(); ++k) out += (k ? " + " : "") + code(triple->axioms[k]);
        out += "\n";
    }
    return out;
}

bool generateAxiomCoverage(const std::filesystem::path& dataRoot, std::string& markdown, std::vector<std::string>& problems)
{
    AxiomSet axioms;
    if (!axioms.loadDirectory(dataRoot)) {
        problems.insert(problems.end(), axioms.problems().begin(), axioms.problems().end());
        return false;
    }
    std::vector<Synergy> synergies;
    if (!loadSynergyContentDirectory(dataRoot / "synergies", axiomPool(axioms), synergies, problems)) return false;
    markdown = renderAxiomCoverage(axioms, synergies);
    return true;
}

}
