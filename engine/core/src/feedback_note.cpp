#include "ichor/core/feedback_note.h"

namespace ichor {

bool FeedbackPrompt::pressF8(bool keyDown, bool available)
{
    (void)keyDown;
    (void)available;
    return false;
}

NoteOutcome FeedbackPrompt::feed(std::string_view typed)
{
    (void)typed;
    return NoteOutcome::Typing;
}

void InputTail::push(const InputFrame& frame)
{
    (void)frame;
}

InputStream InputTail::stream() const
{
    return {};
}

bool writeFeedbackNote(const std::filesystem::path& folder, const FeedbackNote& note, const InputStream& tail)
{
    (void)folder;
    (void)note;
    (void)tail;
    return false;
}

}
