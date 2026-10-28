#include "ichor/ui/prompt_screen.h"

namespace ichor {

PromptScreen::PromptScreen(UiContext& ui)
    : ui_(ui)
{
}

PromptScreen::~PromptScreen() = default;

void PromptScreen::bind(const std::optional<std::string>&)
{
}

bool PromptScreen::visible() const
{
    return false;
}

std::string PromptScreen::markup() const
{
    return {};
}

}
