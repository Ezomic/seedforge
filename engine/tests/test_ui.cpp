#include "ichor/ui/ledger.h"
#include "ichor/ui/hud.h"
#include "ichor/ui/recording_renderer.h"
#include "ichor/ui/ui_context.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>

#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/SystemInterface.h>

using namespace ichor;
using Catch::Approx;

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

TEST_CASE("fonts last as long as the contexts do, and a file that is not a font is refused")
{
    const std::filesystem::path fonts = std::filesystem::path(ICHOR_ART_DIR) / "fonts";
    const auto textVertices = [&](bool loadFonts) {
        RecordingUiRenderer renderer;
        UiContext ui(renderer, 640, 360);
        REQUIRE(ui.ok());
        if (loadFonts) {
            REQUIRE(ui.loadFontFace(fonts / "AlegreyaSans-Regular.ttf"));
            REQUIRE_FALSE(ui.loadFontFace(fonts / "AlegreyaSans-OFL.txt"));
        }
        Rml::ElementDocument* document = ui.loadDocumentFromMemory(
            "<rml><head><style>body, div { display: block; } body { width: 640px; height: 360px; }"
            " div { font-family: Alegreya Sans; font-size: 20px; color: white; }</style></head>"
            "<body><div>Hello</div></body></rml>");
        REQUIRE(document != nullptr);
        document->Show();
        frame(ui, renderer, 0.0);
        std::size_t most = 0;
        for (const auto& draw : renderer.draws()) {
            most = std::max(most, draw.vertexCount);
        }
        return most;
    };
    REQUIRE(textVertices(true) >= 20);
    // The first context is gone and took the fonts with it.
    REQUIRE(textVertices(false) < 20);
    REQUIRE(textVertices(true) >= 20);
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

TEST_CASE("the compass names the eight points and wraps")
{
    constexpr float kQuarter = 1.57079632679f;
    REQUIRE(compassLabel(0.0f) == "N");
    REQUIRE(compassLabel(kQuarter) == "E");
    REQUIRE(compassLabel(kQuarter * 2.0f) == "S");
    REQUIRE(compassLabel(kQuarter * 3.0f) == "W");
    REQUIRE(compassLabel(kQuarter * 0.5f) == "NE");
    REQUIRE(compassLabel(-kQuarter * 0.5f) == "NW");
    REQUIRE(compassLabel(kQuarter * 4.0f) == "N");
    REQUIRE(compassLabel(-kQuarter) == "W");
    // The point holds for a stretch either side of it.
    REQUIRE(compassLabel(0.3f) == "N");
    REQUIRE(compassLabel(0.5f) == "NE");
}

TEST_CASE("the heading of a view follows the compass and bad numbers read as north")
{
    constexpr float kQuarter = 1.57079632679f;
    REQUIRE(headingOf(0.0f, -1.0f) == 0.0f);
    REQUIRE(headingOf(1.0f, 0.0f) == Approx(kQuarter));
    REQUIRE(std::abs(headingOf(0.0f, 1.0f)) == Approx(kQuarter * 2.0f));
    REQUIRE(headingOf(-1.0f, 0.0f) == Approx(-kQuarter));
    REQUIRE(compassLabel(headingOf(1.0f, 0.0f)) == "E");
    REQUIRE(compassLabel(headingOf(-1.0f, 0.0f)) == "W");
    REQUIRE(compassLabel(std::nanf("")) == "N");
    REQUIRE(compassLabel(INFINITY) == "N");
    REQUIRE(clockLabel(std::nanf("")) == "00:00");
}

TEST_CASE("wading is named by depth, and over dry land or the ankles it is not named")
{
    REQUIRE(waterLabel(0.0f).empty());
    REQUIRE(waterLabel(0.1f).empty());
    REQUIRE(waterLabel(-2.0f).empty());
    REQUIRE(waterLabel(std::nanf("")).empty());
    REQUIRE(waterLabel(0.1001f) == "Ankle deep");
    // Four equal bands between the first wading depth (0.1 m) and chest height (1.5 m): edges at 0.45, 0.8 and 1.15.
    REQUIRE(waterLabel(0.44f) == "Ankle deep");
    REQUIRE(waterLabel(0.46f) == "Knee deep");
    REQUIRE(waterLabel(0.79f) == "Knee deep");
    REQUIRE(waterLabel(0.81f) == "Waist deep");
    REQUIRE(waterLabel(1.14f) == "Waist deep");
    REQUIRE(waterLabel(1.16f) == "Chest deep");
    REQUIRE(waterLabel(1.5f) == "Chest deep");
    REQUIRE(waterLabel(30.0f) == "Chest deep");
}

TEST_CASE("the clock reads hours and minutes and wraps around the day")
{
    REQUIRE(clockLabel(9.0f) == "09:00");
    REQUIRE(clockLabel(17.5f) == "17:30");
    REQUIRE(clockLabel(0.0f) == "00:00");
    REQUIRE(clockLabel(23.99f) == "23:59");
    REQUIRE(clockLabel(24.0f) == "00:00");
    REQUIRE(clockLabel(-1.0f) == "23:00");
}

TEST_CASE("the HUD draws the compass and clock and only relays out when the text changes")
{
    RecordingUiRenderer renderer;
    UiContext ui(renderer, 1280, 720);
    REQUIRE(ui.ok());
    const std::filesystem::path fonts = std::filesystem::path(ICHOR_ART_DIR) / "fonts";
    REQUIRE(ui.loadFontFace(fonts / "AlegreyaSans-Regular.ttf"));
    REQUIRE(ui.loadFontFace(fonts / "Cinzel.ttf"));
    Hud hud(ui);
    REQUIRE(hud.ok());

    hud.update(0.0f, 9.0f, 0.0f);
    frame(ui, renderer, 0.0);
    const std::size_t firstDraws = renderer.draws().size();
    REQUIRE(firstDraws >= 3); // the plate and its border, then text

    // Nothing changed: the same picture.
    hud.update(0.0f, 9.0f, 0.0f);
    frame(ui, renderer, 0.1);
    REQUIRE(renderer.draws().size() == firstDraws);

    hud.update(0.0f, 9.0f, 0.9f);
    frame(ui, renderer, 0.15);
    REQUIRE(renderer.draws().size() > firstDraws);
    hud.update(0.0f, 9.0f, 0.0f);
    frame(ui, renderer, 0.17);
    REQUIRE(renderer.draws().size() == firstDraws);

    // A turn to the south-west and a later hour: still a picture, with different text in it.
    hud.update(3.9f, 17.5f, 0.0f);
    frame(ui, renderer, 0.2);
    REQUIRE(renderer.draws().size() >= 3);
}

TEST_CASE("the ledger escapes markup so an entry can never act as a document")
{
    REQUIRE(escapeRml("plain text") == "plain text");
    REQUIRE(escapeRml("a < b && c > \"d\"") == "a &lt; b &amp;&amp; c &gt; &quot;d&quot;");
    REQUIRE(escapeRml("<div id=\"list\">x</div>") == "&lt;div id=&quot;list&quot;&gt;x&lt;/div&gt;");
}

TEST_CASE("the ledger cleans control characters and bad UTF-8, and entities cannot be smuggled in")
{
    REQUIRE(escapeRml("it's fine") == "it's fine");
    REQUIRE(escapeRml("a & b") == "a &amp; b");
    REQUIRE(escapeRml("&amp;") == "&amp;amp;");
    REQUIRE(escapeRml("&#60;div&#62;") == "&amp;#60;div&amp;#62;");
    REQUIRE(escapeRml(std::string("a\0b", 3)) == "a b");
    REQUIRE(escapeRml("tab\there\r\n\x01\x7f") == "tab here    ");
    REQUIRE(escapeRml("caf\xC3\xA9") == "caf\xC3\xA9");           // a valid two-byte character stays
    REQUIRE(escapeRml("\xE2\x82\xAC") == "\xE2\x82\xAC");           // and a three-byte one
    REQUIRE(escapeRml("bad\xFF" "byte") == "bad?byte");                // a byte that cannot start a character
    REQUIRE(escapeRml("cut\xE2\x82") == "cut??");                  // a sequence cut short
    REQUIRE(escapeRml("\xC0\x80") == "??");                        // an overlong form
}

TEST_CASE("the ledger draws nothing until shown, then its card and rows, and only relays out when the rows change")
{
    RecordingUiRenderer renderer;
    UiContext ui(renderer, 1280, 720);
    REQUIRE(ui.ok());
    const std::filesystem::path fonts = std::filesystem::path(ICHOR_ART_DIR) / "fonts";
    REQUIRE(ui.loadFontFace(fonts / "AlegreyaSans-Regular.ttf"));
    REQUIRE(ui.loadFontFace(fonts / "Cinzel.ttf"));
    Ledger ledger(ui);
    REQUIRE(ledger.ok());
    REQUIRE_FALSE(ledger.visible());

    ledger.setRows({{"0:01:05", "quest", "Took the ash to the ferryman."}});
    frame(ui, renderer, 0.0);
    REQUIRE(renderer.draws().empty());

    ledger.setVisible(true);
    frame(ui, renderer, 0.1);
    const std::size_t shown = renderer.draws().size();
    REQUIRE(shown >= 3);

    ledger.setRows({{"0:01:05", "quest", "Took the ash to the ferryman."}});
    frame(ui, renderer, 0.2);
    REQUIRE(renderer.draws().size() == shown);

    // A hostile entry is text on the card, not a second card.
    ledger.setRows({{"0:02:00", "note", "<div style=\"background-color: #ff0000; width: 900px; height: 900px\">x</div>"}});
    frame(ui, renderer, 0.3);
    REQUIRE_FALSE(drewColour(renderer, 255, 0, 0));

    ledger.setVisible(false);
    frame(ui, renderer, 0.4);
    REQUIRE(renderer.draws().empty());
}

TEST_CASE("an empty ledger says so, and rows beyond the card's room are left off")
{
    RecordingUiRenderer renderer;
    UiContext ui(renderer, 1280, 720);
    REQUIRE(ui.ok());
    const std::filesystem::path fonts = std::filesystem::path(ICHOR_ART_DIR) / "fonts";
    REQUIRE(ui.loadFontFace(fonts / "AlegreyaSans-Regular.ttf"));
    REQUIRE(ui.loadFontFace(fonts / "Cinzel.ttf"));
    Ledger ledger(ui);
    ledger.setVisible(true);
    ledger.setRows({});
    frame(ui, renderer, 0.0);
    const std::size_t empty = renderer.draws().size();
    REQUIRE(empty >= 2);

    std::vector<LedgerRow> many(40, LedgerRow{"0:00:01", "note", "a line"});
    ledger.setRows(many);
    frame(ui, renderer, 0.1);
    const std::size_t full = renderer.draws().size();
    ledger.setRows(std::vector<LedgerRow>(Ledger::kMaxRows, LedgerRow{"0:00:01", "note", "a line"}));
    frame(ui, renderer, 0.2);
    REQUIRE(renderer.draws().size() == full); // the same twelve rows
    REQUIRE(full > empty);
}

TEST_CASE("a long unbroken entry stays inside the card")
{
    RecordingUiRenderer renderer;
    UiContext ui(renderer, 1280, 720);
    REQUIRE(ui.ok());
    const std::filesystem::path fonts = std::filesystem::path(ICHOR_ART_DIR) / "fonts";
    REQUIRE(ui.loadFontFace(fonts / "AlegreyaSans-Regular.ttf"));
    REQUIRE(ui.loadFontFace(fonts / "Cinzel.ttf"));
    Ledger ledger(ui);
    ledger.setVisible(true);
    ledger.setRows(std::vector<LedgerRow>(Ledger::kMaxRows, LedgerRow{"9:59:59", "note", std::string(500, 'w')}));
    frame(ui, renderer, 0.0);
    // Nothing is drawn below the card's bottom edge (88% of the window), scissor or not.
    for (const auto& draw : renderer.draws()) {
        if (draw.scissored) {
            REQUIRE(draw.scissor.Bottom() <= 720 * 88 / 100 + 2);
        }
    }
    REQUIRE_FALSE(renderer.draws().empty());
}

TEST_CASE("clicking the ledger's close button asks for it to be closed, and clicking elsewhere does not")
{
    RecordingUiRenderer renderer;
    UiContext ui(renderer, 1280, 720);
    REQUIRE(ui.ok());
    const std::filesystem::path fonts = std::filesystem::path(ICHOR_ART_DIR) / "fonts";
    REQUIRE(ui.loadFontFace(fonts / "AlegreyaSans-Regular.ttf"));
    REQUIRE(ui.loadFontFace(fonts / "Cinzel.ttf"));
    Ledger ledger(ui);
    REQUIRE(ledger.ok());
    REQUIRE_FALSE(ledger.closeButtonCentre().has_value()); // hidden: nowhere to click

    // A click while it is hidden reaches nothing.
    ui.mouseMove(640, 100);
    ui.mouseButton(0, true);
    ui.mouseButton(0, false);
    frame(ui, renderer, 0.0);
    REQUIRE_FALSE(ledger.takeCloseRequest());

    ledger.setVisible(true);
    frame(ui, renderer, 0.1);
    const auto centre = ledger.closeButtonCentre();
    REQUIRE(centre.has_value());
    REQUIRE(centre->first > 640.0f); // top right of the card
    REQUIRE(centre->second < 360.0f);

    // Pressing on the card's body is no request.
    ui.mouseMove(640, 400);
    frame(ui, renderer, 0.2);
    ui.mouseButton(0, true);
    ui.mouseButton(0, false);
    frame(ui, renderer, 0.3);
    REQUIRE_FALSE(ledger.takeCloseRequest());

    // Pressing and releasing on the button is.
    ui.mouseMove(static_cast<int>(centre->first), static_cast<int>(centre->second));
    frame(ui, renderer, 0.4);
    ui.mouseButton(0, true);
    frame(ui, renderer, 0.5);
    ui.mouseButton(0, false);
    frame(ui, renderer, 0.6);
    REQUIRE(ledger.takeCloseRequest());
    REQUIRE_FALSE(ledger.takeCloseRequest()); // once

    // Pressing on the button and sliding off before releasing does not count.
    ui.mouseMove(static_cast<int>(centre->first), static_cast<int>(centre->second));
    frame(ui, renderer, 0.7);
    ui.mouseButton(0, true);
    frame(ui, renderer, 0.8);
    ui.mouseMove(640, 400);
    frame(ui, renderer, 0.9);
    ui.mouseButton(0, false);
    frame(ui, renderer, 1.0);
    REQUIRE_FALSE(ledger.takeCloseRequest());
}

TEST_CASE("a pointer that leaves the window unhovers the button, and a ledger can go while the context still lives")
{
    RecordingUiRenderer renderer;
    UiContext ui(renderer, 1280, 720);
    REQUIRE(ui.ok());
    const std::filesystem::path fonts = std::filesystem::path(ICHOR_ART_DIR) / "fonts";
    REQUIRE(ui.loadFontFace(fonts / "AlegreyaSans-Regular.ttf"));
    REQUIRE(ui.loadFontFace(fonts / "Cinzel.ttf"));
    {
        Ledger ledger(ui);
        ledger.setVisible(true);
        frame(ui, renderer, 0.0);
        const auto centre = ledger.closeButtonCentre();
        REQUIRE(centre.has_value());
        ui.mouseMove(static_cast<int>(centre->first), static_cast<int>(centre->second));
        frame(ui, renderer, 0.1);
        ui.mouseLeave();
        frame(ui, renderer, 0.2);
        ui.mouseButton(0, true);
        ui.mouseButton(0, false);
        frame(ui, renderer, 0.3);
        REQUIRE_FALSE(ledger.takeCloseRequest());
    }
    // The ledger is gone but its elements may be pending; the context carries on and clicks reach nothing.
    ui.mouseMove(640, 100);
    frame(ui, renderer, 0.4);
    ui.mouseButton(0, true);
    ui.mouseButton(0, false);
    frame(ui, renderer, 0.5);
    SUCCEED("no listener called after the ledger went");
}
