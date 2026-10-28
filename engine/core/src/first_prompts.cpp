#include "ichor/core/first_prompts.h"

namespace ichor {

std::string firstPromptText(FirstPrompt)
{
    return {};
}

FirstPrompts stepFirstPrompts(FirstPrompts state, const FirstPromptsFrame&, double)
{
    return state;
}

std::optional<std::string> firstPromptLine(const FirstPrompts&, bool)
{
    return std::nullopt;
}

bool firstPromptsShown(const FirstPromptsRun&)
{
    return true;
}

}
