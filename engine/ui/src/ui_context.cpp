#include "ichor/ui/ui_context.h"

#include "ichor/core/log.h"
#include "ichor/core/profile.h"

#include <RmlUi/Core.h>

#include <string>

namespace ichor {

namespace {

// The system interface reads the clock of the context being updated or
// rendered, so animations follow simulated time and two contexts on different
// clocks do not disturb each other. Everything here is single-threaded.
double g_uiClock = 0.0;

class UiSystem final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override { return g_uiClock; }

    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override
    {
        const LogLevel level = type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT ? LogLevel::Error
            : type == Rml::Log::LT_WARNING                                                  ? LogLevel::Warn
                                                                                            : LogLevel::Info;
        logf(level, "ui: {}", message);
        return true;
    }
};

UiSystem g_system;
int g_users = 0;
int g_contexts = 0;

}

UiContext::UiContext(Rml::RenderInterface& renderer, std::uint32_t width, std::uint32_t height)
{
    if (g_users == 0) {
        Rml::SetSystemInterface(&g_system);
        if (!Rml::Initialise()) {
            log(LogLevel::Error, "ui: RmlUi failed to initialise");
            return;
        }
    }
    ++g_users;
    name_ = "ichor-" + std::to_string(g_contexts++);
    context_ = Rml::CreateContext(name_, Rml::Vector2i(static_cast<int>(width), static_cast<int>(height)), &renderer);
    if (context_ == nullptr) {
        log(LogLevel::Error, "ui: could not create a context");
    }
}

UiContext::~UiContext()
{
    if (context_ != nullptr) {
        Rml::RemoveContext(name_);
        context_ = nullptr;
    }
    if (g_users > 0 && --g_users == 0) {
        Rml::Shutdown();
    }
}

bool UiContext::loadFontFace(const std::filesystem::path& path, bool fallback)
{
    if (context_ == nullptr) {
        return false;
    }
    return Rml::LoadFontFace(path.generic_string(), fallback);
}

Rml::ElementDocument* UiContext::loadDocumentFromMemory(std::string_view rml, std::string_view sourceUrl)
{
    return context_ == nullptr ? nullptr : context_->LoadDocumentFromMemory(Rml::String(rml), Rml::String(sourceUrl));
}

Rml::ElementDocument* UiContext::loadDocument(const std::filesystem::path& path)
{
    if (context_ == nullptr) {
        return nullptr;
    }
    const auto utf8 = path.generic_u8string();
    return context_->LoadDocument(Rml::String(reinterpret_cast<const char*>(utf8.data()), utf8.size()));
}

void UiContext::unloadDocument(Rml::ElementDocument* document)
{
    if (context_ != nullptr && document != nullptr) {
        context_->UnloadDocument(document);
    }
}

void UiContext::setSize(std::uint32_t width, std::uint32_t height)
{
    if (context_ != nullptr) {
        context_->SetDimensions(Rml::Vector2i(static_cast<int>(width), static_cast<int>(height)));
    }
}

void UiContext::update(double elapsedSeconds)
{
    ICHOR_ZONE_N("ui.update");
    clock_ = elapsedSeconds;
    g_uiClock = clock_;
    if (context_ != nullptr) {
        context_->Update();
    }
}

void UiContext::render()
{
    ICHOR_ZONE_N("ui.render");
    g_uiClock = clock_;
    if (context_ != nullptr) {
        context_->Render();
    }
}

bool UiContext::mouseMove(int x, int y)
{
    return context_ != nullptr && !context_->ProcessMouseMove(x, y, 0);
}

void UiContext::mouseLeave()
{
    if (context_ != nullptr) {
        context_->ProcessMouseLeave();
    }
}

bool UiContext::mouseButton(int button, bool down)
{
    if (context_ == nullptr) {
        return false;
    }
    return down ? !context_->ProcessMouseButtonDown(button, 0) : !context_->ProcessMouseButtonUp(button, 0);
}

}
