#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ichor {

constexpr std::int64_t kMaxDialogueWeight = 1'000'000;

// "key=value" holds when the context has that tag, "key" when it has the key
// with any value. A negated condition ("key!=value", "!key") holds when the
// plain one does not, so a missing tag fails the plain form and passes the negated one.
struct DialogueCondition {
    std::string key;
    std::string value;
    bool anyValue = false;
    bool negated = false;
};

struct DialogueLine {
    std::string id;
    std::string text;
    std::vector<DialogueCondition> conditions;
    std::int64_t weight = 1;    // 0 keeps a line in the pool but never picks it
    std::uint64_t cooldown = 0; // ticks after it is spoken before it may be picked again
};

// What the selector knows of the NPC and the world: abstract key/value tags
// ("trait"="bitter", "schedule"="work", "town"="short_of_grain"). A key may carry several values.
struct DialogueContext {
    std::uint64_t npcId = 0;
    std::vector<std::pair<std::string, std::string>> tags;
};

struct DialogueHistory {
    std::uint64_t now = 0;     // current tick, set by the caller
    std::uint64_t counter = 0; // lines this NPC has said; seeds the next pick
    std::string lastId;
    std::map<std::string, std::uint64_t, std::less<>> lastSpoken;

    void record(const std::string& lineId);
};

// A pool only exists if its ids are unique and non-empty, every weight is in
// [0, kMaxDialogueWeight] and every line has text. Lines are kept sorted by
// id, so nothing downstream depends on authoring order.
class DialoguePool {
public:
    static std::optional<DialoguePool> fromLines(std::vector<DialogueLine> lines, std::string& error);

    // A JSON array of line objects: {id, text, weight, cooldown?, conditions?}.
    // Every problem found is reported, each with its file and path.
    static std::optional<DialoguePool> load(const std::filesystem::path& file, std::vector<std::string>& problems);

    const std::vector<DialogueLine>& lines() const { return lines_; }

private:
    std::vector<DialogueLine> lines_;
};

bool parseCondition(const std::string& text, DialogueCondition& out, std::string& error);
bool conditionHolds(const DialogueCondition& condition, const DialogueContext& context);

// Pure and deterministic. Among lines whose conditions all hold and that are
// not in cooldown, picks by weight with a generator seeded from
// (seed, npcId, history.counter). The line just spoken is skipped unless it is
// the only candidate. Nothing qualifying gives nullptr. Does not record the pick.
const DialogueLine* selectLine(const DialoguePool& pool, const DialogueContext& context, const DialogueHistory& history, std::uint64_t seed);

}
