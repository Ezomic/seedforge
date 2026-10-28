#include "ichor/ui/note_box.h"

#include <RmlUi/Core/ElementDocument.h>

namespace ichor {

NoteBox::NoteBox(UiContext& ui)
    : ui_(ui)
{
}

NoteBox::~NoteBox() = default;

void NoteBox::bind(const std::optional<std::string>& text)
{
    (void)text;
}

bool NoteBox::visible() const
{
    return false;
}

std::string NoteBox::markup() const
{
    return {};
}

}
