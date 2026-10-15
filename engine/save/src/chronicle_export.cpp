#include "ichor/save/chronicle_export.h"

#include <cstdint>
#include <format>

namespace ichor {

namespace {

constexpr std::string_view kReplacement = "\xEF\xBF\xBD";

// Decodes one code point at the start of `bytes`; returns its length, or 0 when the bytes are not valid UTF-8.
std::size_t decode(std::string_view bytes, std::uint32_t& point)
{
    const auto at = [&](std::size_t i) { return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i])); };
    const std::uint32_t lead = at(0);
    std::size_t length = 0;
    std::uint32_t minimum = 0;
    if (lead < 0x80) {
        point = lead;
        return 1;
    } else if (lead >= 0xC2 && lead <= 0xDF) {
        length = 2;
        point = lead & 0x1F;
        minimum = 0x80;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        length = 3;
        point = lead & 0x0F;
        minimum = 0x800;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        length = 4;
        point = lead & 0x07;
        minimum = 0x10000;
    } else {
        return 0;
    }
    if (bytes.size() < length) {
        return 0;
    }
    for (std::size_t i = 1; i < length; ++i) {
        if ((at(i) & 0xC0) != 0x80) {
            return 0;
        }
        point = (point << 6) | (at(i) & 0x3F);
    }
    if (point < minimum || point > 0x10FFFF || (point >= 0xD800 && point <= 0xDFFF)) {
        return 0;
    }
    return length;
}

bool unsafe(std::uint32_t point)
{
    return point < 0x20 || (point >= 0x7F && point <= 0x9F) || point == 0x2028 || point == 0x2029
        || (point >= 0x202A && point <= 0x202E) || (point >= 0x2066 && point <= 0x2069)
        || point == 0x200E || point == 0x200F || point == 0x061C || point == 0xAD || point == 0xFEFF
        || (point >= 0x200B && point <= 0x200D) || (point >= 0x2060 && point <= 0x2064)
        || (point >= 0xFFF9 && point <= 0xFFFB) || (point >= 0xE0000 && point <= 0xE007F);
}

void appendSafe(std::string& out, std::string_view text)
{
    while (!text.empty()) {
        std::uint32_t point = 0;
        const std::size_t length = decode(text, point);
        if (length == 0) {
            out += kReplacement;
            text.remove_prefix(1);
            continue;
        }
        if (unsafe(point)) {
            out += ' ';
        } else {
            out.append(text.substr(0, length));
        }
        text.remove_prefix(length);
    }
}

constexpr std::uint64_t kMaxSeconds = 1'000'000'000ull * 3600;

std::string playTime(std::uint64_t tick, double secondsPerTick)
{
    // NaN and a negative rate fall to zero, absurd ticks clamp, so the conversion is never undefined.
    const double elapsed = static_cast<double>(tick) * secondsPerTick;
    const std::uint64_t seconds = elapsed >= 1.8e19 ? kMaxSeconds : elapsed >= 0.0 ? static_cast<std::uint64_t>(elapsed) : 0;
    return std::format("{}:{:02}:{:02}", seconds / 3600, seconds / 60 % 60, seconds % 60);
}

}

bool isValidUtf8(std::string_view bytes)
{
    while (!bytes.empty()) {
        std::uint32_t point = 0;
        const std::size_t length = decode(bytes, point);
        if (length == 0) {
            return false;
        }
        bytes.remove_prefix(length);
    }
    return true;
}

std::string exportChronicleText(const Chronicle& chronicle, double secondsPerTick)
{
    const std::size_t count = chronicle.entries().size();
    std::string out = std::format("Chronicle\n{} {}\n", count, count == 1 ? "entry" : "entries");
    if (count == 0) {
        out += "\nNothing has happened yet.\n";
        return out;
    }
    out += '\n';
    for (const ChronicleEntry& entry : chronicle.entries()) {
        out += '[';
        out += playTime(entry.tick, secondsPerTick);
        out += "] ";
        appendSafe(out, entry.category);
        out += ": ";
        appendSafe(out, entry.text);
        if (entry.weight > 0) {
            out += std::format(" (weight {})", static_cast<unsigned>(entry.weight));
        }
        out += '\n';
    }
    return out;
}

}
