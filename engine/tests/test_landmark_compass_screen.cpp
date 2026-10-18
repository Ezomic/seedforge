#include "ichor/ui/landmark_compass_screen.h"
#include "ichor/ui/recording_renderer.h"
#include "ichor/ui/ui_context.h"

#include <catch2/catch_test_macros.hpp>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <filesystem>
#include <string>
#include <vector>

using namespace ichor;

namespace {

const std::filesystem::path kUiDirectory = std::filesystem::path(ICHOR_REPO_DATA) / "ui";

struct Fixture {
    RecordingUiRenderer renderer;
    UiContext ui{renderer, 800, 600};

    Fixture()
    {
        const std::filesystem::path fonts = std::filesystem::path(ICHOR_ART_DIR) / "fonts";
        REQUIRE(ui.ok());
        REQUIRE(ui.loadFontFace(fonts / "AlegreyaSans-Regular.ttf"));
    }

    std::size_t drawnVertices()
    {
        renderer.clearDraws();
        ui.update(0.0);
        ui.render();
        std::size_t vertices = 0;
        for (const auto& draw : renderer.draws()) {
            vertices += draw.vertexCount;
        }
        return vertices;
    }
};

std::vector<NavLandmark> tower()
{
    return {{"tower", "the broken tower", {0.0f, 100.0f}}};
}

std::string landmarksText(Fixture& f)
{
    std::string text;
    for (int i = 0; i < f.ui.context()->GetNumDocuments(); ++i) {
        if (Rml::Element* list = f.ui.context()->GetDocument(i)->GetElementById("landmarks")) {
            text = list->GetInnerRML();
        }
    }
    return text;
}

}

TEST_CASE("the landmark compass loads its RML and CSS from data/ui")
{
    Fixture f;
    LandmarkCompassScreen screen(f.ui, kUiDirectory);
    REQUIRE(screen.ok());
    LandmarkCompassScreen missing(f.ui, kUiDirectory / "nowhere");
    REQUIRE_FALSE(missing.ok());
}

TEST_CASE("binding a reading draws the landmark sentence and unbinding it draws less")
{
    Fixture f;
    LandmarkCompassScreen screen(f.ui, kUiDirectory);
    REQUIRE(screen.ok());
    const std::size_t empty = f.drawnVertices();

    bindLandmarkCompass(screen, {0.0f, 0.0f}, {0.0f, 1.0f}, tower());
    const std::size_t withLandmark = f.drawnVertices();

    const std::string text = landmarksText(f);
    CHECK(text.find("The broken tower lies north") != std::string::npos);
    CHECK(withLandmark > empty);
    CHECK(withLandmark - empty >= 20 * 4); // a quad per glyph, not just a box

    bindLandmarkCompass(screen, {0.0f, 0.0f}, {0.0f, 1.0f}, {});
    CHECK(f.drawnVertices() == empty);
}

TEST_CASE("the compass lists landmarks nearest first and follows the player")
{
    Fixture f;
    LandmarkCompassScreen screen(f.ui, kUiDirectory);
    REQUIRE(screen.ok());
    std::vector<NavLandmark> marks = tower();
    marks.push_back({"salt", "the salt flats", {-500.0f, 0.0f}});

    bindLandmarkCompass(screen, {0.0f, 0.0f}, {0.0f, 1.0f}, marks);
    std::string text = landmarksText(f);
    REQUIRE(text.find("broken tower") != std::string::npos);
    REQUIRE(text.find("salt flats") != std::string::npos);
    CHECK(text.find("broken tower") < text.find("salt flats"));

    bindLandmarkCompass(screen, {-500.0f, 0.0f}, {0.0f, 1.0f}, marks);
    text = landmarksText(f);
    CHECK(text.find("salt flats") < text.find("broken tower"));
}

TEST_CASE("a bad reading shows nothing and landmark names cannot inject markup")
{
    Fixture f;
    LandmarkCompassScreen screen(f.ui, kUiDirectory);
    REQUIRE(screen.ok());

    bindLandmarkCompass(screen, {0.0f, 0.0f}, {0.0f, 0.0f}, tower());
    CHECK(landmarksText(f).empty());

    bindLandmarkCompass(screen, {0.0f, 0.0f}, {0.0f, 1.0f}, {{"x", "<div id=\"evil\">&", {0.0f, 50.0f}}});
    f.drawnVertices();
    CHECK(f.ui.context()->GetDocument(0)->GetElementById("evil") == nullptr);
}
