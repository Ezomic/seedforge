#include "ichor/save/codex_store.h"

#include <algorithm>

namespace ichor {

namespace {

bool validCategory(CodexCategory category)
{
    return category < CodexCategory::Count_;
}

} // namespace

bool isValidCodexId(std::string_view id, std::size_t maxIdBytes)
{
    if (id.empty() || id.size() > maxIdBytes || id.size() > 255) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
    });
}

CodexStore::CodexStore(CodexLimits limits)
    : limits_(limits)
{
}

CodexResult CodexStore::unlock(CodexCategory category, std::string_view id)
{
    if (!validCategory(category)) {
        return CodexResult::InvalidCategory;
    }
    if (!isValidCodexId(id, limits_.maxIdBytes)) {
        return CodexResult::InvalidId;
    }
    if (has(category, id)) {
        return CodexResult::AlreadyKnown;
    }
    if (count(category) >= limits_.maxEntriesPerCategory) {
        return CodexResult::TooMany;
    }
    entries_.push_back({category, std::string(id)});
    return CodexResult::Unlocked;
}

bool CodexStore::has(CodexCategory category, std::string_view id) const
{
    return std::any_of(entries_.begin(), entries_.end(), [&](const CodexEntry& e) { return e.category == category && e.id == id; });
}

std::size_t CodexStore::count(CodexCategory category) const
{
    return static_cast<std::size_t>(
        std::count_if(entries_.begin(), entries_.end(), [&](const CodexEntry& e) { return e.category == category; }));
}

} // namespace ichor
