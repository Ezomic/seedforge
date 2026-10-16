#include "ichor/ui/subtitle_track.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ichor {

namespace {

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

bool isContinuation(char c)
{
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

std::size_t codePoints(std::string_view s)
{
    return static_cast<std::size_t>(std::count_if(s.begin(), s.end(), [](char c) { return !isContinuation(c); }));
}

std::size_t bytesOfPrefix(std::string_view s, std::size_t count)
{
    std::size_t seen = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (!isContinuation(s[i])) {
            if (seen == count)
                return i;
            ++seen;
        }
    }
    return s.size();
}

} // namespace

std::vector<std::string> splitSubtitleText(std::string_view text, const SubtitleSettings& settings)
{
    const std::size_t limit = std::max<std::size_t>(settings.maxCharsPerChunk, 1);
    std::vector<std::string> chunks;
    std::string current;
    std::size_t currentLen = 0;

    auto flush = [&] {
        if (!current.empty())
            chunks.push_back(std::move(current));
        current.clear();
        currentLen = 0;
    };

    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && isSpace(text[i]))
            ++i;
        const std::size_t begin = i;
        while (i < text.size() && !isSpace(text[i]))
            ++i;
        std::string_view word = text.substr(begin, i - begin);
        if (word.empty())
            break;

        std::size_t wordLen = codePoints(word);
        if (currentLen > 0 && currentLen + 1 + wordLen <= limit) {
            current += ' ';
            current += word;
            currentLen += 1 + wordLen;
            continue;
        }
        flush();
        while (wordLen > limit) {
            const std::size_t cut = bytesOfPrefix(word, limit);
            chunks.emplace_back(word.substr(0, cut));
            word.remove_prefix(cut);
            wordLen -= limit;
        }
        current = std::string(word);
        currentLen = wordLen;
    }
    flush();
    return chunks;
}

SubtitleTrack buildSubtitleTrack(std::string speaker, std::string_view text, float voiceSeconds,
                                 const SubtitleSettings& settings)
{
    SubtitleTrack track;
    track.speaker = std::move(speaker);
    track.text = std::string(text);

    const std::vector<std::string> chunks = splitSubtitleText(text, settings);
    if (chunks.empty())
        return track;

    const double voice = std::isfinite(voiceSeconds) && voiceSeconds > 0.0f ? voiceSeconds : 0.0;
    std::size_t total = 0;
    for (const std::string& chunk : chunks)
        total += codePoints(chunk);

    double cursor = 0.0;
    for (const std::string& chunk : chunks) {
        const std::size_t len = codePoints(chunk);
        const double share = voice * static_cast<double>(len) / static_cast<double>(total);
        const double reading =
            std::max<double>(settings.minChunkSeconds, settings.minSecondsPerChar * static_cast<double>(len));
        const double shown = std::max(share, reading);
        track.cues.push_back({chunk, static_cast<float>(cursor), static_cast<float>(cursor + shown)});
        cursor += shown;
    }
    return track;
}

const SubtitleCue* activeSubtitle(const SubtitleTrack& track, float seconds)
{
    if (!(seconds >= 0.0f))
        return nullptr;
    const SubtitleCue* best = nullptr;
    for (const SubtitleCue& cue : track.cues) {
        if (seconds >= cue.start && seconds < cue.end && (!best || cue.start >= best->start))
            best = &cue;
    }
    return best;
}

} // namespace ichor
