#include "ichor/core/feedback_note.h"

#include "ichor/core/session_summary.h"

#include <format>
#include <fstream>

namespace ichor {

namespace {

// The length of the well-formed UTF-8 character at the start of text, or 0 for a byte that does not start one.
std::size_t sequenceLength(std::string_view text)
{
    const auto lead = static_cast<unsigned char>(text[0]);
    std::size_t length = 0;
    if (lead < 0x80) return 1;
    if (lead >= 0xC2 && lead <= 0xDF) length = 2;
    if (lead >= 0xE0 && lead <= 0xEF) length = 3;
    if (lead >= 0xF0 && lead <= 0xF4) length = 4;
    if (length == 0 || length > text.size()) {
        return 0;
    }
    for (std::size_t i = 1; i < length; ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) {
            return 0;
        }
    }
    return length;
}

std::size_t characterCount(std::string_view text)
{
    std::size_t count = 0;
    for (const char c : text) {
        count += (static_cast<unsigned char>(c) & 0xC0) != 0x80 ? 1 : 0;
    }
    return count;
}

}

bool FeedbackPrompt::pressF8(bool keyDown, bool available)
{
    const bool pressed = keyDown && !held_ && available && !open_;
    held_ = keyDown;
    if (pressed) {
        open_ = true;
        text_.clear();
    }
    return pressed;
}

NoteOutcome FeedbackPrompt::feed(std::string_view typed)
{
    if (!open_) {
        return NoteOutcome::Typing;
    }
    std::size_t characters = characterCount(text_);
    std::size_t i = 0;
    while (i < typed.size()) {
        const char c = typed[i];
        if (c == '\r' || c == '\x1b') {
            open_ = false;
            return c == '\r' ? NoteOutcome::Save : NoteOutcome::Cancel;
        }
        if (c == '\b') {
            while (!text_.empty() && (static_cast<unsigned char>(text_.back()) & 0xC0) == 0x80) {
                text_.pop_back();
            }
            if (!text_.empty()) {
                text_.pop_back();
                --characters;
            }
            ++i;
            continue;
        }
        const std::size_t length = sequenceLength(typed.substr(i));
        if (length == 0) {
            ++i;
            continue;
        }
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F && characters < kNoteMaxCharacters) {
            text_.append(typed.substr(i, length));
            ++characters;
        }
        i += length;
    }
    return NoteOutcome::Typing;
}

void FeedbackPrompt::cancel()
{
    open_ = false;
}

bool writeFeedbackNote(const std::filesystem::path& folder, const FeedbackNote& note, const InputStream& tail)
{
    const std::uint64_t tailTicks = tail.frames.size();
    const std::uint64_t firstTick = note.tick > tailTicks ? note.tick - tailTicks : 0;
    std::string text = std::format("build: {}\ntime: {}\nseed: {}\ntick: {}\nreplay tail: {} ticks, from tick {}\nnote: {}\n", note.buildVersion,
        utcTimestamp(note.time), note.seed, note.tick, tailTicks, firstTick, note.text);
    {
        std::ofstream out(folder / "note.txt", std::ios::binary | std::ios::trunc);
        out << text;
        if (!out) {
            return false;
        }
    }
    return tail.save(folder / "replay-tail.iinp");
}

}
