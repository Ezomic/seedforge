#include "ichor/ui/recording_renderer.h"
#include "ichor/ui/ui_context.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>

#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/SystemInterface.h>

using namespace ichor;

namespace {

constexpr const char* kBoxes = R"(<rml>
<head>
<style>
    /* RmlUi has no default stylesheet: divs are inline until told otherwise. */
    body, div { display: block; }
    body { width: 640px; height: 360px; background-color: #101010; }
    .box { position: absolute; left: 20px; top: 20px; width: 100px; height: 50px; background-color: #ff0000; }
    .box:hover { background-color: #00ff00; }
    .clip { position: absolute; left: 200px; top: 20px; width: 80px; height: 80px; overflow: hidden; background-color: #0000ff; }
    .inner { width: 200px; height: 200px; background-color: #ffff00; }
</style>
</head>
<body>
    <div class="box"></div>
    <div class="clip"><div class="inner"></div></div>
</body>
</rml>)";

bool drewColour(const RecordingUiRenderer& renderer, int r, int g, int b)
{
    const auto& draws = renderer.draws();
    return std::any_of(draws.begin(), draws.end(), [&](const RecordingUiRenderer::Draw& draw) {
        return draw.firstColour.red == r && draw.firstColour.green == g && draw.firstColour.blue == b;
    });
}

void frame(UiContext& ui, RecordingUiRenderer& renderer, double time)
{
    renderer.clearDraws();
    ui.update(time);
    ui.render();
}

}

TEST_CASE("text draws a quad per glyph once a font is loaded")
{
    RecordingUiRenderer renderer;
    UiContext ui(renderer, 640, 360);
    REQUIRE(ui.ok());
    const std::filesystem::path fonts = std::filesystem::path(ICHOR_ART_DIR) / "fonts";
    REQUIRE(ui.loadFontFace(fonts / "AlegreyaSans-Regular.ttf"));
    REQUIRE(ui.loadFontFace(fonts / "AlegreyaSans-Bold.ttf"));
    REQUIRE(ui.loadFontFace(fonts / "Cinzel.ttf"));
    REQUIRE_FALSE(ui.loadFontFace(fonts / "missing.ttf"));

    Rml::ElementDocument* document = ui.loadDocumentFromMemory(
        "<rml><head><style>body, div { display: block; } body { width: 640px; height: 360px; }"
        " div { font-family: Alegreya Sans; font-size: 20px; color: white; }</style></head>"
        "<body><div>Hello</div></body></rml>");
    REQUIRE(document != nullptr);
    document->Show();
    frame(ui, renderer, 0.0);
    // Five letters, four vertices each.
    const auto& draws = renderer.draws();
    REQUIRE(std::any_of(draws.begin(), draws.end(), [](const RecordingUiRenderer::Draw& draw) { return draw.vertexCount >= 20; }));
}

TEST_CASE("a UI document renders its boxes through the render interface")
{
    RecordingUiRenderer renderer;
    UiContext ui(renderer, 640, 360);
    REQUIRE(ui.ok());
    Rml::ElementDocument* document = ui.loadDocumentFromMemory(kBoxes);
    REQUIRE(document != nullptr);
    document->Show();
    frame(ui, renderer, 0.0);

    REQUIRE_FALSE(renderer.draws().empty());
    REQUIRE(drewColour(renderer, 255, 0, 0));
    REQUIRE(drewColour(renderer, 0, 0, 255));
    for (const auto& draw : renderer.draws()) {
        REQUIRE(draw.indexCount % 3 == 0);
        REQUIRE(draw.vertexCount >= 3);
    }
}

TEST_CASE("pointer input reaches the document and changes what is drawn")
{
    RecordingUiRenderer renderer;
    UiContext ui(renderer, 640, 360);
    REQUIRE(ui.ok());
    ui.loadDocumentFromMemory(kBoxes)->Show();

    frame(ui, renderer, 0.0);
    REQUIRE(drewColour(renderer, 255, 0, 0));
    REQUIRE_FALSE(drewColour(renderer, 0, 255, 0));

    ui.mouseMove(50, 40); // over the red box
    frame(ui, renderer, 0.1);
    REQUIRE(drewColour(renderer, 0, 255, 0));
    REQUIRE_FALSE(drewColour(renderer, 255, 0, 0));

    ui.mouseMove(500, 300); // away from it
    frame(ui, renderer, 0.2);
    REQUIRE(drewColour(renderer, 255, 0, 0));
}

TEST_CASE("clipped content is drawn under a scissor region")
{
    RecordingUiRenderer renderer;
    UiContext ui(renderer, 640, 360);
    REQUIRE(ui.ok());
    ui.loadDocumentFromMemory(kBoxes)->Show();
    frame(ui, renderer, 0.0);

    bool clipped = false;
    for (const auto& draw : renderer.draws()) {
        if (draw.scissored && draw.firstColour.red == 255 && draw.firstColour.green == 255 && draw.firstColour.blue == 0) {
            clipped = true;
            REQUIRE(draw.scissor.Width() <= 80);
            REQUIRE(draw.scissor.Height() <= 80);
        }
    }
    REQUIRE(clipped);
}

TEST_CASE("unloading a document releases its geometry")
{
    RecordingUiRenderer renderer;
    UiContext ui(renderer, 640, 360);
    REQUIRE(ui.ok());
    Rml::ElementDocument* document = ui.loadDocumentFromMemory(kBoxes);
    document->Show();
    frame(ui, renderer, 0.0);
    REQUIRE(renderer.liveGeometry() > 0);

    ui.unloadDocument(document);
    frame(ui, renderer, 0.1);
    REQUIRE(renderer.liveGeometry() == 0);
    REQUIRE(renderer.draws().empty());
}

TEST_CASE("a missing document is a null result, not a crash")
{
    RecordingUiRenderer renderer;
    UiContext ui(renderer, 640, 360);
    REQUIRE(ui.ok());
    REQUIRE(ui.loadDocument("no/such/file.rml") == nullptr);
}

TEST_CASE("contexts come and go, and the process-wide setup follows them")
{
    RecordingUiRenderer first;
    RecordingUiRenderer second;
    {
        auto a = std::make_unique<UiContext>(first, 640, 360);
        auto b = std::make_unique<UiContext>(second, 320, 180);
        REQUIRE(a->ok());
        REQUIRE(b->ok());
        a->loadDocumentFromMemory(kBoxes)->Show();
        b->loadDocumentFromMemory(kBoxes)->Show();
        a.reset();
        frame(*b, second, 0.0);
        REQUIRE_FALSE(second.draws().empty());
        REQUIRE(first.liveGeometry() == 0);
    }
    RecordingUiRenderer third;
    UiContext c(third, 640, 360);
    REQUIRE(c.ok());
    c.loadDocumentFromMemory(kBoxes)->Show();
    frame(c, third, 0.0);
    REQUIRE_FALSE(third.draws().empty());
}

TEST_CASE("each context reads its own clock, not whichever updated last")
{
    RecordingUiRenderer first;
    RecordingUiRenderer second;
    UiContext a(first, 100, 100);
    UiContext b(second, 100, 100);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    a.update(5.0);
    b.update(900.0);
    a.render();
    REQUIRE(Rml::GetSystemInterface()->GetElapsedTime() == 5.0);
    b.render();
    REQUIRE(Rml::GetSystemInterface()->GetElapsedTime() == 900.0);
}
