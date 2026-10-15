#include "ichor/save/save_migration.h"

#include <algorithm>

namespace ichor {

namespace {

// Format 2 is the first format with a migration path, so there are no steps yet. The next
// format change adds its step here and raises the number.
constexpr std::uint32_t kStandardFormat = 2;

}

MigrationRegistry::MigrationRegistry(std::uint32_t current, std::vector<MigrationStep> steps)
    : current_(current)
    , steps_(std::move(steps))
{
    std::sort(steps_.begin(), steps_.end(), [](const MigrationStep& a, const MigrationStep& b) { return a.from < b.from; });
}

const MigrationRegistry& MigrationRegistry::standard()
{
    static const MigrationRegistry registry(kStandardFormat, {});
    return registry;
}

bool MigrationRegistry::canMigrate(std::uint32_t format) const
{
    if (format == 0 || format > current_) {
        return false;
    }
    for (std::uint32_t at = format; at < current_; ++at) {
        const auto step = std::find_if(steps_.begin(), steps_.end(), [at](const MigrationStep& s) { return s.from == at && s.apply != nullptr; });
        if (step == steps_.end()) {
            return false;
        }
    }
    return true;
}

bool MigrationRegistry::migrate(std::uint32_t from, SlotBody& body) const
{
    if (!canMigrate(from)) {
        return false;
    }
    for (std::uint32_t at = from; at < current_; ++at) {
        const auto step = std::find_if(steps_.begin(), steps_.end(), [at](const MigrationStep& s) { return s.from == at; });
        if (!step->apply(body)) {
            return false;
        }
    }
    return true;
}

}
