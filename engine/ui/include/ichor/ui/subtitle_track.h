#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

struct SubtitleSettings {
    std::size_t maxCharsPerChunk = 42;      // in code points; a longer chunk is split at a space
    float minSecondsPerChar = 1.0f / 17.0f; // reading speed: a chunk shows at least this long per character
    float minChunkSeconds = 1.0f;           // floor for any chunk, so a short one is not a flash
};

// Times are seconds from the start of the voice line. [start, end) is when the text is shown.
struct SubtitleCue {
    std::string text;
    float start = 0.0f;
    float end = 0.0f;
};

struct SubtitleTrack {
    std::string speaker;
    std::string text; // the source line, as given
    std::vector<SubtitleCue> cues;
};

// Splits `text` into chunks at word boundaries (a single word longer than the limit is cut at a
// code point). Runs of whitespace collapse to one space; no other character is dropped.
std::vector<std::string> splitSubtitleText(std::string_view text, const SubtitleSettings& settings);

// Lays the chunks out back to back across `voiceSeconds`, in proportion to their length. A chunk
// the reading-speed rule says needs longer gets longer, so the track may run past the voice line.
// A NaN, negative or infinite voice length counts as zero.
SubtitleTrack buildSubtitleTrack(std::string speaker, std::string_view text, float voiceSeconds,
                                 const SubtitleSettings& settings = {});

// When cues overlap the one that started last wins. Null before the first cue, at or after a
// cue's end, for a NaN time, and for a negative time.
const SubtitleCue* activeSubtitle(const SubtitleTrack& track, float seconds);

} // namespace ichor
