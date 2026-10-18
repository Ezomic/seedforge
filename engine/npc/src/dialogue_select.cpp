#include "ichor/npc/dialogue_select.h"

#include "ichor/core/file_io.h"
#include "ichor/core/hash.h"
#include "ichor/core/rng.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <limits>
#include <set>

namespace ichor {

namespace {

using Json = nlohmann::json;

constexpr std::uintmax_t kMaxFileBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaxText = 400;
constexpr std::size_t kMaxId = 64;
constexpr std::size_t kMaxProblems = 100;
constexpr std::size_t kMaxConditionPart = 64;
constexpr std::string_view kEmDash = "\xE2\x80\x94";

bool isKeyChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
}

bool isKey(const std::string& text)
{
    return !text.empty() && text.size() <= kMaxConditionPart && text[0] >= 'a' && text[0] <= 'z' && std::all_of(text.begin(), text.end(), isKeyChar);
}

bool isValue(const std::string& text)
{
    return !text.empty() && text.size() <= kMaxConditionPart && std::all_of(text.begin(), text.end(), isKeyChar);
}

bool hasTag(const DialogueContext& context, const DialogueCondition& condition)
{
    return std::any_of(context.tags.begin(), context.tags.end(), [&](const auto& tag) {
        return tag.first == condition.key && (condition.anyValue || tag.second == condition.value);
    });
}

bool inCooldown(const DialogueLine& line, const DialogueHistory& history)
{
    if (line.cooldown == 0) {
        return false;
    }
    const auto it = history.lastSpoken.find(line.id);
    if (it == history.lastSpoken.end()) {
        return false;
    }
    const std::uint64_t elapsed = history.now >= it->second ? history.now - it->second : 0;
    return elapsed < line.cooldown;
}

std::uint64_t totalWeight(const std::vector<const DialogueLine*>& lines)
{
    std::uint64_t total = 0;
    for (const auto* line : lines) {
        total += static_cast<std::uint64_t>(line->weight);
    }
    return total;
}

std::string at(const std::filesystem::path& file, const std::string& path, const std::string& message)
{
    return file.generic_string() + ": " + path + ": " + message;
}

}

void DialogueHistory::record(const std::string& lineId)
{
    lastSpoken[lineId] = now;
    lastId = lineId;
    ++counter;
}

bool parseCondition(const std::string& text, DialogueCondition& out, std::string& error)
{
    DialogueCondition condition;
    std::string rest = text;
    if (!rest.empty() && rest[0] == '!') {
        condition.negated = true;
        rest.erase(0, 1);
        if (rest.find('=') != std::string::npos) {
            error = "\"!key\" takes no value, got \"" + text + "\"";
            return false;
        }
    }
    const auto eq = rest.find('=');
    if (eq == std::string::npos) {
        condition.key = rest;
        condition.anyValue = true;
    } else {
        std::string key = rest.substr(0, eq);
        if (!key.empty() && key.back() == '!') {
            if (condition.negated) {
                error = "condition \"" + text + "\" negates twice";
                return false;
            }
            condition.negated = true;
            key.pop_back();
        }
        condition.key = key;
        condition.value = rest.substr(eq + 1);
        if (!isValue(condition.value)) {
            error = "condition \"" + text + "\" needs a lowercase value (letters, digits, '_', '.') after '='";
            return false;
        }
    }
    if (!isKey(condition.key)) {
        error = "condition \"" + text + "\" needs a lowercase key (letters, digits, '_', '.') starting with a letter";
        return false;
    }
    out = condition;
    return true;
}

bool conditionHolds(const DialogueCondition& condition, const DialogueContext& context)
{
    return hasTag(context, condition) != condition.negated;
}

std::optional<DialoguePool> DialoguePool::fromLines(std::vector<DialogueLine> lines, std::string& error)
{
    std::set<std::string> seen;
    for (const auto& line : lines) {
        if (line.id.empty()) {
            error = "a dialogue line has an empty id";
            return std::nullopt;
        }
        if (!seen.insert(line.id).second) {
            error = "duplicate dialogue line id '" + line.id + "'";
            return std::nullopt;
        }
        if (line.text.find_first_not_of(" \t\r\n") == std::string::npos) {
            error = "line '" + line.id + "' has no text";
            return std::nullopt;
        }
        if (line.weight < 0 || line.weight > kMaxDialogueWeight) {
            error = "line '" + line.id + "': weight " + std::to_string(line.weight) + " is outside 0 to " + std::to_string(kMaxDialogueWeight);
            return std::nullopt;
        }
    }
    std::sort(lines.begin(), lines.end(), [](const DialogueLine& a, const DialogueLine& b) { return a.id < b.id; });
    DialoguePool pool;
    pool.lines_ = std::move(lines);
    return pool;
}

std::optional<DialoguePool> DialoguePool::load(const std::filesystem::path& file, std::vector<std::string>& problems)
{
    std::vector<std::uint8_t> bytes;
    if (!readFileBytes(file, bytes, kMaxFileBytes)) {
        problems.push_back(file.generic_string() + ": cannot read the file (missing, not a file, or over " + std::to_string(kMaxFileBytes) + " bytes)");
        return std::nullopt;
    }
    const Json json = Json::parse(bytes.begin(), bytes.end(), nullptr, false);
    if (json.is_discarded()) {
        problems.push_back(file.generic_string() + ": not valid JSON");
        return std::nullopt;
    }
    if (!json.is_array()) {
        problems.push_back(at(file, "$", "expected an array of dialogue lines"));
        return std::nullopt;
    }

    const std::size_t before = problems.size();
    std::vector<DialogueLine> lines;
    std::set<std::string> seenIds;
    for (std::size_t i = 0; i < json.size() && problems.size() - before < kMaxProblems; ++i) {
        const std::string base = "[" + std::to_string(i) + "]";
        const Json& item = json[i];
        if (!item.is_object()) {
            problems.push_back(at(file, base, "expected an object"));
            continue;
        }
        DialogueLine line;

        const auto id = item.find("id");
        if (id == item.end() || !id->is_string() || id->get_ref<const std::string&>().empty() || id->get_ref<const std::string&>().size() > kMaxId) {
            problems.push_back(at(file, base + ".id", "expected a non-empty string of at most " + std::to_string(kMaxId) + " bytes"));
        } else if (!seenIds.insert(id->get<std::string>()).second) {
            problems.push_back(at(file, base + ".id", "duplicate id '" + id->get<std::string>() + "'"));
        } else {
            line.id = id->get<std::string>();
        }

        const auto text = item.find("text");
        if (text == item.end() || !text->is_string() || text->get_ref<const std::string&>().find_first_not_of(" \t\r\n") == std::string::npos) {
            problems.push_back(at(file, base + ".text", "expected a non-empty string"));
        } else if (text->get_ref<const std::string&>().size() > kMaxText) {
            problems.push_back(at(file, base + ".text", "longer than " + std::to_string(kMaxText) + " bytes"));
        } else if (text->get_ref<const std::string&>().find(kEmDash) != std::string::npos) {
            problems.push_back(at(file, base + ".text", "must not contain an em-dash"));
        } else {
            line.text = text->get<std::string>();
        }

        const auto weight = item.find("weight");
        if (weight == item.end() || !(weight->is_number_integer() || weight->is_number_unsigned())) {
            problems.push_back(at(file, base + ".weight", "expected an integer"));
        } else if (weight->is_number_unsigned() ? weight->get<std::uint64_t>() > static_cast<std::uint64_t>(kMaxDialogueWeight) : (weight->get<std::int64_t>() < 0 || weight->get<std::int64_t>() > kMaxDialogueWeight)) {
            problems.push_back(at(file, base + ".weight", "outside 0 to " + std::to_string(kMaxDialogueWeight)));
        } else {
            line.weight = weight->get<std::int64_t>();
        }

        const auto cooldown = item.find("cooldown");
        if (cooldown != item.end()) {
            if (!cooldown->is_number_unsigned()) {
                problems.push_back(at(file, base + ".cooldown", "expected a non-negative integer"));
            } else {
                line.cooldown = cooldown->get<std::uint64_t>();
            }
        }

        const auto conditions = item.find("conditions");
        if (conditions != item.end()) {
            if (!conditions->is_array()) {
                problems.push_back(at(file, base + ".conditions", "expected an array of strings"));
            } else {
                for (std::size_t c = 0; c < conditions->size(); ++c) {
                    const std::string path = base + ".conditions[" + std::to_string(c) + "]";
                    DialogueCondition condition;
                    std::string error;
                    if (!(*conditions)[c].is_string()) {
                        problems.push_back(at(file, path, "expected a string"));
                    } else if (!parseCondition((*conditions)[c].get<std::string>(), condition, error)) {
                        problems.push_back(at(file, path, error));
                    } else {
                        line.conditions.push_back(condition);
                    }
                }
            }
        }
        lines.push_back(std::move(line));
    }
    if (problems.size() != before) {
        return std::nullopt;
    }

    std::string error;
    auto pool = fromLines(std::move(lines), error);
    if (!pool) {
        problems.push_back(file.generic_string() + ": " + error);
    }
    return pool;
}

const DialogueLine* selectLine(const DialoguePool& pool, const DialogueContext& context, const DialogueHistory& history, std::uint64_t seed)
{
    std::vector<const DialogueLine*> candidates;
    for (const auto& line : pool.lines()) {
        if (line.weight <= 0 || inCooldown(line, history)) {
            continue;
        }
        if (std::all_of(line.conditions.begin(), line.conditions.end(), [&](const DialogueCondition& c) { return conditionHolds(c, context); })) {
            candidates.push_back(&line);
        }
    }

    std::vector<const DialogueLine*> fresh;
    for (const auto* line : candidates) {
        if (line->id != history.lastId) {
            fresh.push_back(line);
        }
    }
    const auto& choices = fresh.empty() ? candidates : fresh;
    if (choices.empty()) {
        return nullptr;
    }

    const std::uint64_t total = totalWeight(choices);
    Rng rng(hashCombine(hashCombine(seed, context.npcId), history.counter));
    const std::uint64_t limit = std::numeric_limits<std::uint64_t>::max() - std::numeric_limits<std::uint64_t>::max() % total;
    std::uint64_t draw = rng.nextU64();
    while (draw >= limit) {
        draw = rng.nextU64();
    }
    std::uint64_t pick = draw % total;
    for (const auto* line : choices) {
        const auto weight = static_cast<std::uint64_t>(line->weight);
        if (pick < weight) {
            return line;
        }
        pick -= weight;
    }
    return choices.back();
}

}
