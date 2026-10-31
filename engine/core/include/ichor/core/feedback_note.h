#pragma once

#include "ichor/core/input.h"
#include "ichor/core/version.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace ichor {

// A tester's F8 note (SEED-697): one line, so a long one stops here.
inline constexpr std::size_t kNoteMaxCharacters = 280;
// How much input the note keeps from before F8: 30 seconds of 60 Hz ticks.
inline constexpr std::size_t kNoteTailTicks = 1800;

enum class NoteOutcome {
    Typing,
    Save,
    Cancel,
};

// The F8 box's state. F8 opens it once per press and does nothing while it is open; typed text arrives as the
// platform reports it: UTF-8, '\b' for Backspace, '\r' for Enter (save) and '\x1b' for Esc (cancel).
class FeedbackPrompt {
public:
    // True when this press opened the box. Unavailable (headless, the editor) ignores the key.
    bool pressF8(bool keyDown, bool available);
    bool open() const { return open_; }
    // Enter or Esc closes the box, and anything after it in the same text is dropped.
    NoteOutcome feed(std::string_view typed);
    // Closes the box without a note, as Esc does.
    void cancel();
    const std::string& text() const { return text_; }

private:
    bool open_ = false;
    bool held_ = false;
    std::string text_;
};

// F8 needs the game UI to draw the box, and the keyboard to itself: never headless, in a --screenshot run or under the editor.
constexpr bool feedbackNoteAvailable(bool gameUi, bool screenshot, bool editing)
{
    return gameUi && !screenshot && !editing;
}

struct FeedbackNote {
    std::string buildVersion = std::string(versionStamp());
    std::chrono::system_clock::time_point time;
    std::uint64_t seed = 0;
    std::uint64_t tick = 0;
    std::string text;
};

// note.txt (the text as typed, with the build, time, seed and tick) and replay-tail.iinp into folder.
bool writeFeedbackNote(const std::filesystem::path& folder, const FeedbackNote& note, const InputStream& tail);

}
