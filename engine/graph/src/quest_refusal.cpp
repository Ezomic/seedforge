#include "ichor/graph/quest_refusal.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>

namespace ichor::graph::questrefusal {

namespace {

using StepIndex = std::map<std::uint32_t, const Step*>;

bool hasChoice(const Step& s, Choice c)
{
    return std::any_of(s.options.begin(), s.options.end(), [c](const Option& o) { return o.choice == c; });
}

std::string idText(std::uint32_t id)
{
    return std::to_string(id);
}

void add(std::vector<Finding>& out, Code code, std::uint32_t step, std::string message)
{
    out.push_back({code, step, std::move(message)});
}

std::vector<Finding> checkStructure(const Quest& q, StepIndex& byId)
{
    std::vector<Finding> out;
    if (q.steps.empty()) {
        add(out, Code::Empty, 0, "quest has no steps");
        return out;
    }
    if (q.steps.size() > kMaxSteps) {
        add(out, Code::TooLarge, 0, "quest has too many steps");
        return out;
    }
    for (const Step& s : q.steps) {
        if (s.id == 0) {
            add(out, Code::BadId, 0, "step id 0 is reserved");
            continue;
        }
        if (!byId.emplace(s.id, &s).second)
            add(out, Code::DuplicateId, s.id, "duplicate step id " + idText(s.id));
        if (s.options.size() > kMaxOptionsPerStep) {
            add(out, Code::TooLarge, s.id, "step " + idText(s.id) + " has too many options");
            return out;
        }
    }
    if (!out.empty())
        return out;
    if (!byId.contains(q.start))
        add(out, Code::NoStart, 0, "start step " + idText(q.start) + " does not exist");
    for (const Step& s : q.steps) {
        std::set<Choice> seen;
        for (const Option& o : s.options) {
            if (o.next != 0 && !byId.contains(o.next))
                add(out, Code::DanglingNext, s.id, "step " + idText(s.id) + " leads to missing step " + idText(o.next));
            if (!seen.insert(o.choice).second)
                add(out, Code::DuplicateChoice, s.id, "step " + idText(s.id) + " repeats a choice");
            if (o.consequence.size() > kMaxConsequenceLength)
                add(out, Code::TooLarge, s.id, "consequence too long at step " + idText(s.id));
        }
    }
    return out;
}

std::set<std::uint32_t> reachable(const Quest& q, const StepIndex& byId)
{
    std::set<std::uint32_t> seen;
    std::vector<std::uint32_t> work{q.start};
    while (!work.empty()) {
        const std::uint32_t id = work.back();
        work.pop_back();
        if (!seen.insert(id).second)
            continue;
        for (const Option& o : byId.at(id)->options)
            if (o.next != 0)
                work.push_back(o.next);
    }
    return seen;
}

} // namespace

std::vector<Finding> validate(const Quest& quest)
{
    StepIndex byId;
    std::vector<Finding> out = checkStructure(quest, byId);
    if (!out.empty())
        return out;

    const std::set<std::uint32_t> live = reachable(quest, byId);
    bool anyOtherSide = false;
    for (const Step& s : quest.steps) {
        if (!live.contains(s.id)) {
            add(out, Code::Unreachable, s.id, "step " + idText(s.id) + " cannot be reached from the start");
            continue;
        }
        const bool isStart = s.id == quest.start;
        if (isStart && !hasChoice(s, Choice::Decline))
            add(out, Code::NoDecline, s.id, "the offer cannot be declined");
        if (!isStart && !hasChoice(s, Choice::WalkAway))
            add(out, Code::NoWalkAway, s.id, "step " + idText(s.id) + " cannot be walked away from");
        if (s.otherSide != 0) {
            anyOtherSide = true;
            if (!hasChoice(s, Choice::TellOtherSide))
                add(out, Code::NoTellOtherSide, s.id, "step " + idText(s.id) + " cannot tell the other side");
        }
        for (const Option& o : s.options) {
            if (o.choice == Choice::Accept)
                continue;
            if (o.consequence.empty())
                add(out, Code::RefusalNoConsequence, s.id, "a refusal at step " + idText(s.id) + " leaves nothing behind");
            if (o.choice == Choice::Decline) {
                if (!isStart)
                    add(out, Code::DeclineAfterStart, s.id, "decline is only valid at the start step");
                if (o.next != 0)
                    add(out, Code::RefusalContinues, s.id, "declining at step " + idText(s.id) + " does not end the quest");
            }
            if (o.choice == Choice::WalkAway && o.next != 0)
                add(out, Code::RefusalContinues, s.id, "walking away at step " + idText(s.id) + " does not end the quest");
            if (o.choice == Choice::TellOtherSide && s.otherSide == 0)
                add(out, Code::TellWithoutOtherSide, s.id, "step " + idText(s.id) + " has no other side to tell");
        }
    }
    if (!anyOtherSide)
        add(out, Code::NoOtherSide, 0, "quest has no other side to tell");
    return out;
}

Result ensureRefusals(const Quest& quest)
{
    Result r;
    r.quest = quest;
    StepIndex byId;
    std::vector<Finding> broken = checkStructure(quest, byId);
    if (!broken.empty()) {
        r.findings = std::move(broken);
        return r;
    }

    const std::set<std::uint32_t> live = reachable(quest, byId);
    for (Step& s : r.quest.steps) {
        if (!live.contains(s.id))
            continue;
        const bool isStart = s.id == quest.start;
        auto addOption = [&s](Choice c, std::string consequence) {
            if (s.options.size() < kMaxOptionsPerStep)
                s.options.push_back({c, 0, std::move(consequence)});
        };
        if (isStart && !hasChoice(s, Choice::Decline))
            addOption(Choice::Decline, "declined:" + idText(s.id));
        if (!isStart && !hasChoice(s, Choice::WalkAway))
            addOption(Choice::WalkAway, "abandoned:" + idText(s.id));
        if (s.otherSide != 0 && !hasChoice(s, Choice::TellOtherSide))
            addOption(Choice::TellOtherSide, "told:" + idText(s.otherSide));
    }
    r.findings = validate(r.quest);
    return r;
}

} // namespace ichor::graph::questrefusal
