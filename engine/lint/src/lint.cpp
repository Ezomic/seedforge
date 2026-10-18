#include "ichor/lint/lint.h"

#include "ichor/data/axiom.h"
#include "ichor/data/data_set.h"
#include "ichor/data/id_audit.h"
#include "ichor/data/loot_ratios.h"
#include "ichor/data/synergy.h"
#include "ichor/data/synergy_content.h"
#include "ichor/data/xref.h"
#include "ichor/world/enemy_reskin.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
#include <tuple>

namespace ichor::lint {

namespace {

namespace fs = std::filesystem;

Problem make(std::string_view validator, std::string file, std::string id, std::string message)
{
    return Problem{std::string(validator), std::move(file), std::move(id), std::move(message)};
}

// The data layer reports "<label>: <path>: <what is wrong>"; the label is the file.
Problem fromLabelled(std::string_view validator, const std::string& line)
{
    const std::size_t split = line.find(": ");
    if (split == std::string::npos) return make(validator, "", "", line);
    return make(validator, line.substr(0, split), "", line.substr(split + 2));
}

void addLabelled(std::string_view validator, const std::vector<std::string>& lines, std::vector<Problem>& out)
{
    for (const std::string& line : lines) out.push_back(fromLabelled(validator, line));
}

void runIds(const fs::path& root, std::vector<Problem>& out)
{
    for (const IdViolation& v : auditIds(root).violations) out.push_back(make("ids", v.file, v.id, v.path + ": " + v.reason));
}

void runTables(const fs::path& root, std::vector<Problem>& out)
{
    DataSet data;
    if (!data.loadDirectory(root)) out.push_back(fromLabelled("tables", data.lastError()));
}

void runAxioms(const fs::path& root, std::vector<Problem>& out)
{
    AxiomSet axioms;
    if (!axioms.loadDirectory(root)) addLabelled("axioms", axioms.problems(), out);
}

void runXref(const fs::path& root, std::vector<Problem>& out)
{
    DataSet data;
    AxiomSet axioms;
    if (!data.loadDirectory(root) || !axioms.loadDirectory(root)) return; // reported by tables and axioms
    const XrefReport report = checkReferences(data, axioms, root);
    for (const DanglingRef& ref : report.dangling)
        out.push_back(make("xref", ref.file, ref.record, ref.field + ": no " + ref.target + " record \"" + ref.value + "\""));
    for (const std::string& file : report.unreadable) out.push_back(make("xref", file, "", "cannot be read or is not the expected shape"));
}

void runSynergies(const fs::path& root, std::vector<Problem>& out)
{
    AxiomSet axioms;
    if (!axioms.loadDirectory(root)) return; // reported by axioms
    std::vector<Synergy> synergies;
    std::vector<std::string> problems;
    if (!loadSynergyContentDirectory(root / "synergies", axiomPool(axioms), synergies, problems)) addLabelled("synergies", problems, out);
}

void runReskin(const fs::path& root, std::vector<Problem>& out)
{
    const fs::path directory = root / "reskin";
    std::error_code code;
    if (!fs::is_directory(directory, code)) return;
    std::vector<fs::path> files;
    for (fs::directory_iterator it(directory, code), end; !code && it != end; it.increment(code))
        if (it->is_regular_file(code) && it->path().extension() == ".json") files.push_back(it->path());
    std::sort(files.begin(), files.end());
    for (const fs::path& file : files) {
        const std::string label = "reskin/" + file.filename().generic_string();
        std::ifstream in(file, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (!in && !in.eof()) {
            out.push_back(make("reskin", label, "", "cannot read file"));
            continue;
        }
        std::vector<ReskinLayer> layers;
        std::vector<std::string> problems;
        if (!parseReskinLayers(label, text, layers, problems)) addLabelled("reskin", problems, out);
    }
}

void runLootRatios(const fs::path& root, std::vector<Problem>& out)
{
    DataSet data;
    if (!data.loadDirectory(root)) return; // reported by tables
    const DataTable* catalogue = data.table("catalogue");
    if (catalogue == nullptr) return;
    const LootRatioReport report = checkLootRatios(*catalogue);
    // Only structural problems: the shipped catalogue is knowingly off the doc 06 mix (test_loot_ratios.cpp asserts the same).
    for (const std::string& problem : report.problems) out.push_back(make("loot_ratios", "catalogue", "", problem));
}

// Not registered, because the check exists only as test code with its loader inside the test:
//   quests (ledger/quest_validation.h, refusal, visible number): tests/test_faction_questlines.cpp, test_setpiece_quests.cpp
//   spine: tests/test_main_spine.cpp (parseSpine is local to the test)
//   dungeon skeletons: tests/test_dungeon_skeletons.cpp (loadText is local to the test)
//   notable items: tests/test_notable_items.cpp (problems() is local to the test)
// Weapon classes have no validator anywhere yet. Each becomes one entry below once its loader is a library function.
const std::vector<Validator> kValidators = {
    {"ids", "stable id rules and uniqueness across every JSON file", runIds},
    {"tables", "every data/<table>/*.json loads as records with unique ids", runTables},
    {"axioms", "every data/axioms file parses and validates", runAxioms},
    {"xref", "every reference to an axiom or table record resolves", runXref},
    {"synergies", "synergy files parse and meet the content tier rules", runSynergies},
    {"reskin", "enemy reskin layers parse within their limits", runReskin},
    {"loot_ratios", "catalogue records have a known class and tier", runLootRatios},
};

bool lessProblem(const Problem& a, const Problem& b)
{
    return std::tie(a.file, a.id, a.message) < std::tie(b.file, b.id, b.message);
}

}

std::string describe(const Problem& problem)
{
    std::string line = "[" + problem.validator + "] ";
    if (!problem.file.empty()) line += problem.file + ": ";
    if (!problem.id.empty()) line += problem.id + ": ";
    return line + problem.message;
}

const std::vector<Validator>& validators()
{
    return kValidators;
}

Report runLint(const fs::path& dataRoot, const std::vector<std::string>& only)
{
    Report report;
    const std::set<std::string> wanted(only.begin(), only.end());
    for (const std::string& name : wanted) {
        const bool known = std::any_of(kValidators.begin(), kValidators.end(), [&](const Validator& v) { return v.name == name; });
        if (!known) {
            report.error = "unknown validator \"" + name + "\"";
            return report;
        }
    }
    std::error_code code;
    if (!fs::is_directory(dataRoot, code)) {
        report.error = "no data directory at " + dataRoot.string();
        return report;
    }
    for (const Validator& validator : kValidators) {
        if (!wanted.empty() && !wanted.contains(std::string(validator.name))) continue;
        std::vector<Problem> found;
        validator.run(dataRoot, found);
        std::stable_sort(found.begin(), found.end(), lessProblem);
        report.results.push_back({std::string(validator.name), found.size()});
        report.problems.insert(report.problems.end(), std::make_move_iterator(found.begin()), std::make_move_iterator(found.end()));
    }
    return report;
}

}
