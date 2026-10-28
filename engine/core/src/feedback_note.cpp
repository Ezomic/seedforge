#include "ichor/core/feedback_note.h"

#include "ichor/core/session_summary.h"

#include <format>
#include <fstream>

namespace ichor {

namespace {

std::size_t sequenceLength(unsigned char lead)
{
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 0; // a stray continuation byte or not UTF-8 at all
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
        const std::size_t length = sequenceLength(static_cast<unsigned char>(c));
        if (length == 0 || i + length > typed.size()) {
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

void InputTail::push(const InputFrame& frame)
{
    if (capacity_ == 0) {
        return;
    }
    if (frames_.size() < capacity_) {
        frames_.push_back(frame);
    } else {
        frames_[next_] = frame;
    }
    next_ = (next_ + 1) % capacity_;
}

InputStream InputTail::stream() const
{
    InputStream out;
    out.frames.reserve(frames_.size());
    const std::size_t start = frames_.size() < capacity_ ? 0 : next_;
    for (std::size_t i = 0; i < frames_.size(); ++i) {
        out.frames.push_back(frames_[(start + i) % frames_.size()]);
    }
    return out;
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
