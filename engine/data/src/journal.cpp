#include "ichor/data/journal.h"

#include "ichor/core/hash.h"
#include "ichor/core/rng.h"

#include <algorithm>
#include <array>

namespace ichor {

namespace {

enum class Slot { Who, What, Where, Decided };

constexpr std::string_view kEmDash = "\xE2\x80\x94";

bool blank(std::string_view text)
{
    return std::all_of(text.begin(), text.end(), [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; });
}

std::optional<Slot> slotNamed(std::string_view name)
{
    if (name == "who") return Slot::Who;
    if (name == "what") return Slot::What;
    if (name == "where") return Slot::Where;
    if (name == "decided") return Slot::Decided;
    return std::nullopt;
}

// Walks a template and calls onSlot for each {placeholder}. Returns an error or nothing.
template <typename OnSlot>
std::string walk(std::string_view text, OnSlot onSlot)
{
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t open = text.find_first_of("{}", at);
        if (open == std::string_view::npos) {
            break;
        }
        if (text[open] == '}') {
            return "an unmatched '}'";
        }
        const std::size_t close = text.find_first_of("{}", open + 1);
        if (close == std::string_view::npos || text[close] == '{') {
            return "an unclosed '{'";
        }
        const std::string_view name = text.substr(open + 1, close - open - 1);
        const std::optional<Slot> slot = slotNamed(name);
        if (!slot) {
            return "the unknown placeholder {" + std::string(name) + "}";
        }
        onSlot(*slot, open);
        at = close + 1;
    }
    return {};
}

std::string checkText(std::string_view text, std::initializer_list<Slot> allowed)
{
    if (text.empty() || blank(text)) {
        return "an empty text";
    }
    if (text.find(kEmDash) != std::string_view::npos || text.find("--") != std::string_view::npos) {
        return "a dash used as punctuation (use a comma, colon or full stop)";
    }
    std::string problem;
    const std::string error = walk(text, [&](Slot slot, std::size_t) {
        if (std::find(allowed.begin(), allowed.end(), slot) == allowed.end() && problem.empty()) {
            problem = "a placeholder that this sentence cannot fill";
        }
    });
    return error.empty() ? problem : error;
}

bool readVariants(const DataRecord& record, std::initializer_list<Slot> allowed, std::vector<std::string>& out, std::string& error)
{
    const auto it = record.fields.find("variants");
    const auto* list = it == record.fields.end() ? nullptr : std::get_if<std::vector<std::string>>(&it->second);
    if (list == nullptr || list->empty()) {
        error = "journal '" + record.id + "': \"variants\" must be a list of at least one string";
        return false;
    }
    for (const std::string& variant : *list) {
        const std::string problem = checkText(variant, allowed);
        if (!problem.empty()) {
            error = "journal '" + record.id + "': \"" + variant + "\" has " + problem;
            return false;
        }
    }
    out = *list;
    return true;
}

bool readWords(const DataRecord& record, const char* name, std::string& out, std::string& error)
{
    const auto it = record.fields.find(name);
    const auto* value = it == record.fields.end() ? nullptr : std::get_if<std::string>(&it->second);
    if (value == nullptr) {
        error = std::string("journal 'fallback': \"") + name + "\" must be a string";
        return false;
    }
    if (blank(*value) || value->find(kEmDash) != value->npos || value->find("--") != value->npos || value->find_first_of("{}") != value->npos) {
        error = std::string("journal 'fallback': \"") + name + "\" must be plain words";
        return false;
    }
    out = *value;
    return true;
}

}

std::optional<JournalTemplates> JournalTemplates::fromTable(const DataTable& table, std::string& error)
{
    for (const auto& [id, record] : table.records) {
        if (id != "asked" && id != "decided" && id != "undecided" && id != "fallback") {
            error = "journal: unknown record '" + id + "'";
            return std::nullopt;
        }
    }
    const auto find = [&](const char* id) -> const DataRecord* {
        const auto it = table.records.find(id);
        if (it == table.records.end()) {
            error = std::string("journal: the record '") + id + "' is missing";
            return nullptr;
        }
        return &it->second;
    };
    const DataRecord* asked = find("asked");
    const DataRecord* decided = asked ? find("decided") : nullptr;
    const DataRecord* undecided = decided ? find("undecided") : nullptr;
    const DataRecord* fallback = undecided ? find("fallback") : nullptr;
    if (fallback == nullptr) {
        return std::nullopt;
    }

    JournalTemplates templates;
    const bool ok = readVariants(*asked, {Slot::Who, Slot::What, Slot::Where}, templates.asked_, error)
        && readVariants(*decided, {Slot::Who, Slot::What, Slot::Where, Slot::Decided}, templates.decided_, error)
        && readVariants(*undecided, {Slot::Who, Slot::What, Slot::Where}, templates.undecided_, error)
        && readWords(*fallback, "who", templates.fallbackWho_, error)
        && readWords(*fallback, "what", templates.fallbackWhat_, error)
        && readWords(*fallback, "where", templates.fallbackWhere_, error);
    if (!ok) {
        return std::nullopt;
    }
    return templates;
}

namespace {

std::uint64_t seedOf(const JournalEntry& entry)
{
    std::uint64_t seed = entry.tick;
    for (const std::string* text : {&entry.who, &entry.what, &entry.where, &entry.decided}) {
        // The length goes in too, so the split between fields matters.
        seed = hashCombine(hashCombine(seed, text->size()), hash64(*text));
    }
    return seed;
}

// Fills a template in one pass, so a name that looks like a placeholder stays as written.
// Fallback words that open a sentence are capitalised; nothing the player's data wrote is touched.
std::string fill(std::string_view variant, const JournalEntry& entry, const JournalTemplates& templates)
{
    std::string out;
    const auto sentenceStart = [&](std::size_t position) {
        return position == 0 || (position >= 2 && variant[position - 1] == ' ' && variant[position - 2] == '.');
    };
    std::size_t copied = 0;
    walk(variant, [&](Slot slot, std::size_t open) {
        out.append(variant.substr(copied, open - copied));
        const std::size_t close = variant.find('}', open);
        copied = close + 1;
        const std::string* text = nullptr;
        const std::string* fallback = nullptr;
        switch (slot) {
        case Slot::Who: text = &entry.who; fallback = &templates.fallbackWho(); break;
        case Slot::What: text = &entry.what; fallback = &templates.fallbackWhat(); break;
        case Slot::Where: text = &entry.where; fallback = &templates.fallbackWhere(); break;
        case Slot::Decided: text = &entry.decided; break;
        }
        if (!blank(*text) || fallback == nullptr) {
            out.append(*text);
            return;
        }
        const std::size_t first = out.size();
        out.append(*fallback);
        if (sentenceStart(open) && out[first] >= 'a' && out[first] <= 'z') {
            out[first] = static_cast<char>(out[first] - 'a' + 'A');
        }
    });
    out.append(variant.substr(copied));
    return out;
}

}

std::string composeJournal(const JournalEntry& entry, const JournalTemplates& templates)
{
    Rng rng = Rng(seedOf(entry)).derive("journal.prose");
    const auto pick = [&](const std::vector<std::string>& variants) -> const std::string& {
        return variants[rng.range(0, static_cast<std::uint32_t>(variants.size() - 1))];
    };
    std::string prose = fill(pick(templates.asked()), entry, templates);
    const bool decided = !blank(entry.decided);
    prose += ' ';
    prose += fill(pick(decided ? templates.decided() : templates.undecided()), entry, templates);
    return prose;
}

}
