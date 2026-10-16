#include "ichor/data/loot_ratios.h"

#include <cmath>
#include <cstdio>
#include <set>

namespace ichor {

namespace {

const std::set<std::string> kTiers = {"unique", "notable", "mundane"};

std::string textField(const DataRecord& record, const char* key)
{
    const auto it = record.fields.find(key);
    if (it == record.fields.end()) return {};
    const auto* value = std::get_if<std::string>(&it->second);
    return value ? *value : std::string();
}

}

bool LootRatioReport::ok() const
{
    if (!problems.empty()) return false;
    for (const LootClassShare& share : classes) {
        if (!share.within) return false;
    }
    return true;
}

std::string LootRatioReport::text() const
{
    std::string out;
    char line[160];
    for (const LootClassShare& share : classes) {
        std::snprintf(line, sizeof line, "%s: %d (%.1f%%) target %.1f%% %+.1f points %s\n", share.name.c_str(), share.count,
                      share.actual * 100.0, share.target * 100.0, share.deviation * 100.0, share.within ? "ok" : "OUT");
        out += line;
    }
    for (const std::string& problem : problems) out += problem + "\n";
    return out;
}

LootRatioReport checkLootRatios(const DataTable& catalogue, const LootRatioTargets& targets)
{
    LootRatioReport report;
    std::map<std::string, int> counts;
    for (const auto& [id, item] : catalogue.records) {
        ++report.total;
        const std::string cls = textField(item, "class");
        const std::string tier = textField(item, "tier");
        if (targets.shares.contains(cls)) ++counts[cls];
        else report.problems.push_back(id + ": unknown class '" + cls + "'");
        if (kTiers.contains(tier)) ++report.tierCounts[tier];
        else report.problems.push_back(id + ": unknown tier '" + tier + "'");
    }
    if (report.total == 0) report.problems.push_back("catalogue is empty");

    for (const auto& [name, target] : targets.shares) {
        LootClassShare share;
        share.name = name;
        share.count = counts[name];
        share.target = target;
        share.actual = report.total > 0 ? static_cast<double>(share.count) / report.total : 0.0;
        share.deviation = share.actual - target;
        share.within = std::fabs(share.deviation) <= targets.tolerance + 1e-9;
        report.classes.push_back(share);
    }
    return report;
}

}
