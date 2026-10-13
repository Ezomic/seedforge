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
// Use it from one thread. File paths resolve through RmlUi's default file
// interface, so a relative path is relative to the working directory.
class UiContext {
public:
    UiContext(Rml::RenderInterface& renderer, std::uint32_t width, std::uint32_t height);
    ~UiContext();
    UiContext(const UiContext&) = delete;
    UiContext& operator=(const UiContext&) = delete;

    bool ok() const { return context_ != nullptr; }

    // Documents are RML (HTML and CSS). Both return null on a parse error,
    // which is logged. RmlUi has no default stylesheet, so a document sets
    // display itself. sourceUrl is the base for the document's relative links.
    Rml::ElementDocument* loadDocumentFromMemory(std::string_view rml, std::string_view sourceUrl = "[document from memory]");
    Rml::ElementDocument* loadDocument(const std::filesystem::path& path);
    void unloadDocument(Rml::ElementDocument* document);

    // Registers a font file for every context in the process; documents then name it by its
    // family and style, such as font-family: Alegreya Sans. `fallback` makes it the face tried
    // for glyphs the others lack. False when the file cannot be read or is not a font.
    // RmlUi drops its fonts when the last context goes, so load them again for a new one.
    bool loadFontFace(const std::filesystem::path& path, bool fallback = false);

    void setSize(std::uint32_t width, std::uint32_t height);

    // The UI clock is the one the caller advances, not the wall clock, so a
    // replay and a headless run see the same animations.
    void update(double elapsedSeconds);
    void render();

    // Pointer input in pixels. Return true when the UI used the event.
    bool mouseMove(int x, int y);
    bool mouseButton(int button, bool down);
    // The pointer left the window: nothing stays hovered.
    void mouseLeave();

    Rml::Context* context() { return context_; }

private:
    Rml::Context* context_ = nullptr;
    std::string name_;
    double clock_ = 0.0;
};

}
