#include "ichor/save/notable_events.h"

#include <map>
#include <set>
#include <utility>

namespace ichor {

namespace {

constexpr std::size_t kMaxPhraseBytes = 300;
constexpr std::string_view kReplacement = "\xEF\xBF\xBD";

bool plainCategory(std::string_view s)
{
    if (s.empty() || s.size() > 64)
        return false;
    for (const char c : s) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok)
            return false;
    }
    return true;
}

// Length of the well-formed UTF-8 character at s[i], or 0; cp receives its code point.
std::size_t decode(std::string_view s, std::size_t i, std::uint32_t& cp)
{
    const auto b = [&](std::size_t k) { return static_cast<unsigned char>(s[k]); };
    const unsigned char c = b(i);
    const std::size_t left = s.size() - i;
    if (c < 0x80) {
        cp = c;
        return 1;
    }
    const auto cont = [&](std::size_t k) { return (b(k) & 0xC0) == 0x80; };
    if (c >= 0xC2 && c <= 0xDF && left >= 2 && cont(i + 1)) {
        cp = (static_cast<std::uint32_t>(c & 0x1F) << 6) | (b(i + 1) & 0x3F);
        return 2;
    }
    if (c >= 0xE0 && c <= 0xEF && left >= 3 && cont(i + 1) && cont(i + 2)) {
        cp = (static_cast<std::uint32_t>(c & 0x0F) << 12) | (static_cast<std::uint32_t>(b(i + 1) & 0x3F) << 6) | (b(i + 2) & 0x3F);
        if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))
            return 0;
        return 3;
    }
    if (c >= 0xF0 && c <= 0xF4 && left >= 4 && cont(i + 1) && cont(i + 2) && cont(i + 3)) {
        cp = (static_cast<std::uint32_t>(c & 0x07) << 18) | (static_cast<std::uint32_t>(b(i + 1) & 0x3F) << 12)
            | (static_cast<std::uint32_t>(b(i + 2) & 0x3F) << 6) | (b(i + 3) & 0x3F);
        if (cp < 0x10000 || cp > 0x10FFFF)
            return 0;
        return 4;
    }
    return 0;
}

bool spaceLike(std::uint32_t cp)
{
    return cp < 0x20 || (cp >= 0x7F && cp <= 0x9F) || cp == 0x061C || cp == 0x200E || cp == 0x200F || cp == 0x2028
        || cp == 0x2029 || (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2066 && cp <= 0x2069);
}

std::string safeField(std::string_view s)
{
    std::string out;
    std::size_t i = 0;
    while (i < s.size()) {
        std::uint32_t cp = 0;
        const std::size_t len = decode(s, i, cp);
        std::string piece;
        if (len == 0) {
            piece = std::string(kReplacement);
            i += 1;
        } else if (spaceLike(cp)) {
            piece = " ";
            i += len;
        } else {
            piece = std::string(s.substr(i, len));
            i += len;
        }
        if (out.size() + piece.size() > kMaxNotableFieldBytes)
            break;
        out += piece;
    }
    return out;
}

bool parseNumber(std::string_view s, std::uint64_t& out)
{
    if (s.empty() || s.size() > 18)
        return false;
    std::uint64_t v = 0;
    for (const char c : s) {
        if (c < '0' || c > '9')
            return false;
        v = v * 10 + static_cast<std::uint64_t>(c - '0');
    }
    out = v;
    return true;
}

}

bool isValidNotableRule(const NotableRule& rule)
{
    if (rule.kind.empty() || rule.kind.size() > ConsequenceLedger::kMaxKeyBytes || !plainCategory(rule.category))
        return false;
    if (rule.phrase.empty() || rule.phrase.size() > kMaxPhraseBytes)
        return false;
    const std::string_view p = rule.phrase;
    for (std::size_t i = 0; i < p.size(); ++i) {
        if (p[i] == '}')
            return false;
        if (p[i] != '{')
            continue;
        if (p.substr(i, 9) == "{subject}")
            i += 8;
        else if (p.substr(i, 7) == "{value}")
            i += 6;
        else
            return false;
    }
    return true;
}

std::vector<NotableRule> defaultNotableRules()
{
    return {
        {"killed", "combat", "{subject} was killed.", false, 0, 0, 200},
        {"betrayed", "story", "{subject} was betrayed.", false, 0, 0, 220},
        {"quest-resolved", "quest", "The matter of {subject} was settled: {value}.", false, 0, 0, 150},
        {"settlement-founded", "world", "{subject} was founded.", false, 0, 0, 180},
        {"settlement-fell", "world", "{subject} fell.", false, 0, 0, 230},
        {"starving", "world", "{subject} went hungry; {value} had no food.", true, 10, 3600, 120},
        {"tithe", "world", "{subject} paid a tithe of {value}.", true, 100, 3600, 60},
    };
}

std::string composeNotablePhrase(const NotableRule& rule, const Fact& fact)
{
    const std::string subject = safeField(fact.subject);
    const std::string value = safeField(fact.value);
    std::string out;
    const std::string_view p = rule.phrase;
    for (std::size_t i = 0; i < p.size(); ++i) {
        if (p.substr(i, 9) == "{subject}") {
            out += subject;
            i += 8;
        } else if (p.substr(i, 7) == "{value}") {
            out += value;
            i += 6;
        } else {
            out += p[i];
        }
    }
    return out;
}

std::vector<NotableEntry> selectNotableEvents(const ConsequenceLedger& ledger, const std::vector<NotableRule>& rules, std::size_t maxEntries)
{
    std::map<std::string, const NotableRule*, std::less<>> byKind;
    for (std::size_t i = 0; i < rules.size() && i < kMaxNotableRules; ++i) {
        if (isValidNotableRule(rules[i]))
            byKind.emplace(rules[i].kind, &rules[i]);
    }

    std::vector<NotableEntry> out;
    std::set<std::pair<std::string, std::string>> seen;
    std::map<std::pair<std::string, std::string>, std::uint64_t> lastKept;
    for (const Fact& fact : ledger.facts()) {
        if (out.size() >= maxEntries)
            break;
        const auto it = byKind.find(fact.kind);
        if (it == byKind.end())
            continue;
        const NotableRule& rule = *it->second;
        if (rule.numeric) {
            std::uint64_t n = 0;
            if (!parseNumber(fact.value, n) || n < rule.minValue)
                continue;
        }
        const auto key = std::make_pair(fact.kind, fact.subject);
        const auto last = lastKept.find(key);
        if (last != lastKept.end() && rule.cooldownTicks > 0 && fact.tick >= last->second
            && fact.tick - last->second < rule.cooldownTicks)
            continue;
        std::string text = composeNotablePhrase(rule, fact);
        if (!seen.emplace(rule.category, text).second)
            continue;
        lastKept[key] = fact.tick;
        out.push_back(NotableEntry{fact.tick, rule.category, std::move(text), rule.weight});
    }
    return out;
}

std::size_t appendNotableEvents(Chronicle& chronicle, const std::vector<NotableEntry>& entries)
{
    std::size_t added = 0;
    for (const NotableEntry& e : entries) {
        ChronicleTags tags;
        tags.weight = e.weight;
        if (chronicle.add(e.tick, e.category, e.text, tags))
            ++added;
    }
    return added;
}

}
