#include "ichor/save/chronicle.h"

#include <algorithm>
#include <charconv>
#include <format>

namespace ichor {

namespace {

constexpr std::string_view kHeader = "ichor-chronicle 1";

bool plainCategory(std::string_view category)
{
    return !category.empty() && category.size() <= 32
        && std::all_of(category.begin(), category.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'; });
}

bool parseNumber(std::string_view text, std::uint32_t& out)
{
    const auto [rest, error] = std::from_chars(text.data(), text.data() + text.size(), out);
    return !text.empty() && error == std::errc{} && rest == text.data() + text.size();
}

// "npc,npc|place|weight"
bool parseTags(std::string_view column, ChronicleTags& tags)
{
    const std::size_t bar = column.find('|');
    const std::size_t bar2 = bar == std::string_view::npos ? bar : column.find('|', bar + 1);
    if (bar2 == std::string_view::npos) {
        return false;
    }
    std::string_view list = column.substr(0, bar);
    while (!list.empty()) {
        const std::size_t comma = list.find(',');
        std::uint32_t npc = 0;
        if (!parseNumber(list.substr(0, comma), npc)) {
            return false;
        }
        tags.npcs.push_back(npc);
        list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
        if (comma != std::string_view::npos && list.empty()) {
            return false;
        }
    }
    std::uint32_t weight = 0;
    if (!parseNumber(column.substr(bar + 1, bar2 - bar - 1), tags.place) || !parseNumber(column.substr(bar2 + 1), weight) || weight > 255) {
        return false;
    }
    tags.weight = static_cast<std::uint8_t>(weight);
    return true;
}

}

bool Chronicle::add(std::uint64_t tick, std::string_view category, std::string_view text)
{
    if (!plainCategory(category) || entries_.size() >= kMaxEntries) {
        return false;
    }
    ChronicleEntry entry;
    entry.tick = tick;
    entry.category = std::string(category);
    // Cut on a character boundary, so a multi-byte character is never split.
    std::size_t length = std::min(text.size(), kMaxTextBytes);
    if (length < text.size()) {
        while (length > 0 && (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80) {
            --length;
        }
    }
    entry.text = std::string(text.substr(0, length));
    for (char& c : entry.text) {
        if (c == '\t' || c == '\n' || c == '\r') {
            c = ' ';
        }
    }
    entries_.push_back(std::move(entry));
    return true;
}

bool Chronicle::add(std::uint64_t tick, std::string_view category, std::string_view text, const ChronicleTags& tags)
{
    std::vector<std::uint32_t> npcs = tags.npcs;
    std::sort(npcs.begin(), npcs.end());
    npcs.erase(std::unique(npcs.begin(), npcs.end()), npcs.end());
    if (npcs.size() > kMaxTaggedNpcs || !add(tick, category, text)) {
        return false;
    }
    ChronicleEntry& entry = entries_.back();
    entry.npcs = std::move(npcs);
    entry.place = tags.place;
    entry.weight = tags.weight;
    return true;
}

std::string Chronicle::serialize() const
{
    std::string out(kHeader);
    out += '\n';
    for (const ChronicleEntry& entry : entries_) {
        out += std::to_string(entry.tick);
        out += '\t';
        out += entry.category;
        out += '\t';
        out += entry.text;
        if (!entry.npcs.empty() || entry.place != 0 || entry.weight != 0) {
            out += '\t';
            for (std::size_t i = 0; i < entry.npcs.size(); ++i) {
                out += (i == 0 ? "" : ",") + std::to_string(entry.npcs[i]);
            }
            out += '|' + std::to_string(entry.place) + '|' + std::to_string(entry.weight);
        }
        out += '\n';
    }
    return out;
}

bool Chronicle::parse(std::string_view bytes, Chronicle& out)
{
    Chronicle parsed;
    std::size_t position = bytes.find('\n');
    if (position == std::string_view::npos) {
        return false;
    }
    // A file saved with Windows line endings is read the same.
    const auto withoutReturn = [](std::string_view line) { return !line.empty() && line.back() == '\r' ? line.substr(0, line.size() - 1) : line; };
    if (withoutReturn(bytes.substr(0, position)) != kHeader) {
        return false;
    }
    ++position;
    while (position < bytes.size()) {
        std::size_t end = bytes.find('\n', position);
        if (end == std::string_view::npos) {
            return false; // a final line without its break is a truncated file
        }
        const std::string_view line = withoutReturn(bytes.substr(position, end - position));
        position = end + 1;
        const std::size_t first = line.find('\t');
        const std::size_t second = first == std::string_view::npos ? first : line.find('\t', first + 1);
        if (second == std::string_view::npos) {
            return false;
        }
        std::uint64_t tick = 0;
        const std::string_view tickText = line.substr(0, first);
        const auto [rest, error] = std::from_chars(tickText.data(), tickText.data() + tickText.size(), tick);
        if (error != std::errc{} || rest != tickText.data() + tickText.size()) {
            return false;
        }
        const std::size_t third = line.find('\t', second + 1);
        const std::string_view category = line.substr(first + 1, second - first - 1);
        if (third == std::string_view::npos) {
            if (!parsed.add(tick, category, line.substr(second + 1))) {
                return false;
            }
            continue;
        }
        ChronicleTags tags;
        if (!parseTags(line.substr(third + 1), tags)
            || !parsed.add(tick, category, line.substr(second + 1, third - second - 1), tags)) {
            return false;
        }
    }
    out = std::move(parsed);
    return true;
}

namespace {

std::string playTime(std::uint64_t tick, double secondsPerTick)
{
    // NaN, a negative rate and absurd ticks all fall to zero instead of an undefined conversion.
    const double elapsed = static_cast<double>(tick) * secondsPerTick;
    const std::uint64_t seconds = elapsed >= 0.0 && elapsed < 1.8e19 ? static_cast<std::uint64_t>(elapsed) : 0;
    return std::format("{}:{:02}:{:02}", seconds / 3600, seconds / 60 % 60, seconds % 60);
}

}

std::vector<ChronicleLine> Chronicle::recent(double secondsPerTick, std::size_t count) const
{
    std::vector<ChronicleLine> lines;
    const std::size_t take = std::min(count, entries_.size());
    lines.reserve(take);
    for (std::size_t i = 0; i < take; ++i) {
        const ChronicleEntry& entry = entries_[entries_.size() - 1 - i];
        lines.push_back({playTime(entry.tick, secondsPerTick), entry.category, entry.text});
    }
    return lines;
}

std::vector<ChronicleLine> Chronicle::recentIn(std::string_view category, double secondsPerTick, std::size_t count) const
{
    std::vector<ChronicleLine> lines;
    for (auto it = entries_.rbegin(); it != entries_.rend() && lines.size() < count; ++it) {
        if (it->category == category) {
            lines.push_back({playTime(it->tick, secondsPerTick), it->category, it->text});
        }
    }
    return lines;
}

std::string Chronicle::exportText(double secondsPerTick) const
{
    std::string out;
    for (const ChronicleEntry& entry : entries_) {
        out += std::format("[{}] {}: {}\n", playTime(entry.tick, secondsPerTick), entry.category, entry.text);
    }
    return out;
}

}
