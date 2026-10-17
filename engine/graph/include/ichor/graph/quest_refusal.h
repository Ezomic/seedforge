#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ichor::graph::questrefusal {

enum class Choice : std::uint8_t {
    Accept,        // carry on with the quest
    Decline,       // turn the offer down; only valid at the start step
    WalkAway,      // abandon a quest already taken
    TellOtherSide, // warn or inform the party the quest works against
};

// One way out of a step. `next` is the step it leads to, 0 ends the quest.
// `consequence` is the world fact a refusal writes to the ledger; a refusal
// that leaves nothing behind is not a real path.
struct Option {
    Choice choice = Choice::Accept;
    std::uint32_t next = 0;
    std::string consequence;
};

struct Step {
    std::uint32_t id = 0;        // nonzero, unique within the quest
    std::uint32_t otherSide = 0; // the party this step works against, 0 when none
    std::vector<Option> options;
};

// A generated quest as plain data, so a hostile description can be judged
// without a graph class that makes bad states impossible.
struct Quest {
    std::uint32_t start = 0;
    std::vector<Step> steps;
};

inline constexpr std::size_t kMaxSteps = 512;
inline constexpr std::size_t kMaxOptionsPerStep = 8;
inline constexpr std::size_t kMaxConsequenceLength = 128;

enum class Code {
    Empty,
    TooLarge,
    DuplicateId,
    BadId,
    DanglingNext,
    NoStart,
    Unreachable,
    NoDecline,
    NoWalkAway,
    NoOtherSide,
    NoTellOtherSide,
    TellWithoutOtherSide,
    RefusalContinues,
    RefusalNoConsequence,
    DeclineAfterStart,
    DuplicateChoice,
};

struct Finding {
    Code code = Code::Empty;
    std::uint32_t step = 0; // the step concerned, 0 when none
    std::string message;
};

// An empty result means every step reachable from the start can be refused:
// the start step offers Decline, every later step offers WalkAway, every step
// with an other side offers TellOtherSide, and the quest has at least one
// other side. Decline and WalkAway end the quest; every refusal writes a
// consequence. Never throws; structural problems stop the run early.
std::vector<Finding> validate(const Quest& quest);

struct Result {
    Quest quest;
    std::vector<Finding> findings; // what is still wrong after the repair
};

// Adds every refusal option the quest lacks, with consequences derived from
// the ids ("declined:1", "abandoned:3", "told:7"), then validates. A quest
// that is structurally broken is returned unchanged with its findings. A
// quest with no other side anywhere cannot be repaired: telling the other
// side needs a party. Deterministic.
Result ensureRefusals(const Quest& quest);

} // namespace ichor::graph::questrefusal
