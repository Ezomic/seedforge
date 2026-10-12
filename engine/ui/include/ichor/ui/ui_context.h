#pragma once

#include <RmlUi/Core/RenderInterface.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace Rml {
class Context;
class ElementDocument;
}

namespace ichor {

// The game UI (doc 17: RmlUi for the game, ImGui for tools). A context owns
// one UI surface and draws through whichever render interface it is given, so
// headless tests record geometry and the Vulkan renderer draws it. Everything
// RmlUi keeps global is set up once for the process, on the engine's log.
class UiContext {
public:
    UiContext(Rml::RenderInterface& renderer, std::uint32_t width, std::uint32_t height);
    ~UiContext();
    UiContext(const UiContext&) = delete;
    UiContext& operator=(const UiContext&) = delete;

    bool ok() const { return context_ != nullptr; }

    // Documents are RML (HTML and CSS). Both return null on a parse error,
    // which is logged.
    Rml::ElementDocument* loadDocumentFromMemory(std::string_view rml);
    Rml::ElementDocument* loadDocument(const std::filesystem::path& path);
    void unloadDocument(Rml::ElementDocument* document);

    void setSize(std::uint32_t width, std::uint32_t height);

    // The UI clock is the one the caller advances, not the wall clock, so a
    // replay and a headless run see the same animations.
    void update(double elapsedSeconds);
    void render();

    // Pointer input in pixels. Return true when the UI used the event.
    bool mouseMove(int x, int y);
    bool mouseButton(int button, bool down);

    Rml::Context* context() { return context_; }

private:
    Rml::Context* context_ = nullptr;
    std::string name_;
};

}
