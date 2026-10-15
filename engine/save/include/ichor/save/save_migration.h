#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ichor {

// What a slot file holds apart from its header: the scene snapshot and the chronicle text.
// A migration step rewrites these from one format to the next; the header fields that name
// the world (seed, generator version, data digest, tick) are not its to change.
struct SlotBody {
    std::vector<std::uint8_t> snapshot;
    std::string chronicle;
};

// Takes a body written in format `from` to one in format `from + 1`. False when the body
// cannot be carried over.
struct MigrationStep {
    std::uint32_t from = 0;
    bool (*apply)(SlotBody&) = nullptr;
};

// The slot format this build writes, and the steps that carry older ones up to it. Steps
// are applied in order, one format at a time. A format with no unbroken chain of steps to
// the current one is unsupported, as is any format newer than the current one.
class MigrationRegistry {
public:
    MigrationRegistry(std::uint32_t current, std::vector<MigrationStep> steps);

    // The registry this build ships: the real format and the real steps.
    static const MigrationRegistry& standard();

    std::uint32_t current() const { return current_; }

    bool isNewer(std::uint32_t format) const { return format > current_; }
    bool canMigrate(std::uint32_t format) const;
    bool migrate(std::uint32_t from, SlotBody& body) const;

private:
    std::uint32_t current_;
    std::vector<MigrationStep> steps_; // sorted by `from`
};

}
