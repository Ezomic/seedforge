#include "ichor/editor/panels.h"

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>

using namespace ichor;

namespace {

constexpr double kDt = 1.0 / 60.0;

// ImGui with no window and no renderer: a display size, the default font, and
// events fed through the IO queue, which is how a backend would feed them.
class Gui {
public:
    Gui()
    {
        context_ = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1280.0f, 720.0f};
        io.Fonts->AddFontDefault();
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }
    ~Gui() { ImGui::DestroyContext(context_); }
    Gui(const Gui&) = delete;
    Gui& operator=(const Gui&) = delete;

    void frame(EditorPanels& panels)
    {
        ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        panels.draw();
        ImGui::Render();
    }

    // A press and a release over a point, each in a frame of its own.
    void click(EditorPanels& panels, ImVec2 at)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(at.x, at.y);
        frame(panels);
        io.AddMouseButtonEvent(0, true);
        frame(panels);
        io.AddMouseButtonEvent(0, false);
        frame(panels);
    }

    // Draws once so the controls have places to be clicked.
    void settle(EditorPanels& panels)
    {
        frame(panels);
        frame(panels);
    }

private:
    ImGuiContext* context_ = nullptr;
};

ImVec2 where(const EditorPanels& panels, const char* label)
{
    const auto centre = panels.centreOf(label);
    REQUIRE(centre.has_value());
    return *centre;
}

}

TEST_CASE("the panels draw a window with the controls where they can be clicked")
{
    Scene scene(1);
    EditorSession editor(scene);
    EditorEnvironment environment;
    EditorPanels panels(editor, environment);
    Gui gui;
    gui.settle(panels);

    for (const char* label : {"Undo", "Redo", "Spawn", "Delete", "Override time", "Hour", "Override weather", "Overcast", "Rain"}) {
        INFO(label);
        const auto centre = panels.centreOf(label);
        REQUIRE(centre.has_value());
        REQUIRE(centre->x > 0.0f);
        REQUIRE(centre->x < 1280.0f);
        REQUIRE(centre->y > 0.0f);
        REQUIRE(centre->y < 720.0f);
    }
    REQUIRE(ImGui::GetDrawData()->CmdListsCount > 0);
    REQUIRE_FALSE(panels.centreOf("position").has_value()); // nothing selected, so no inspector fields
}

TEST_CASE("clicking Spawn makes an entity, selects it, and the outliner lists it")
{
    Scene scene(1);
    EditorSession editor(scene);
    EditorEnvironment environment;
    environment.spawnAt = {3.0, 1.0, -2.0};
    EditorPanels panels(editor, environment);
    Gui gui;
    gui.settle(panels);

    gui.click(panels, where(panels, "Spawn"));
    scene.tick(kDt);
    gui.settle(panels);

    REQUIRE(editor.outliner().size() == 1);
    REQUIRE(editor.outliner()[0].name == "object 1");
    REQUIRE(editor.selection().has_value());
    REQUIRE(scene.registry().get<Transform>(scene.find(*editor.selection())).position == glm::dvec3(3.0, 1.0, -2.0));
    REQUIRE(panels.centreOf("object 1").has_value());
    REQUIRE(panels.centreOf("position").has_value()); // the inspector has something to show

    gui.click(panels, where(panels, "Spawn"));
    scene.tick(kDt);
    REQUIRE(editor.outliner().size() == 2);
    REQUIRE(editor.outliner()[1].name == "object 2");
}

TEST_CASE("clicking an outliner row selects that entity")
{
    Scene scene(1);
    EditorSession editor(scene);
    EditorEnvironment environment;
    EditorPanels panels(editor, environment);
    const StableId first = *editor.spawn("alpha", {0, 0, 0});
    const StableId second = *editor.spawn("beta", {1, 0, 0});
    scene.tick(kDt);
    Gui gui;
    gui.settle(panels);

    gui.click(panels, where(panels, "alpha"));
    REQUIRE(editor.selection() == first);
    gui.settle(panels);
    gui.click(panels, where(panels, "beta"));
    REQUIRE(editor.selection() == second);
}

TEST_CASE("Delete and Undo take an entity away and bring it back")
{
    Scene scene(1);
    EditorSession editor(scene);
    EditorEnvironment environment;
    EditorPanels panels(editor, environment);
    const StableId id = *editor.spawn("altar", {5, 0, 0});
    scene.tick(kDt);
    editor.select(id);
    Gui gui;
    gui.settle(panels);

    gui.click(panels, where(panels, "Delete"));
    scene.tick(kDt);
    REQUIRE_FALSE(editor.exists(id));
    REQUIRE_FALSE(editor.selection().has_value());
    gui.settle(panels);

    gui.click(panels, where(panels, "Undo"));
    scene.tick(kDt);
    REQUIRE(editor.exists(id));
    gui.settle(panels);

    gui.click(panels, where(panels, "Redo"));
    scene.tick(kDt);
    REQUIRE_FALSE(editor.exists(id));
}

TEST_CASE("buttons that cannot act are disabled and ignore clicks")
{
    Scene scene(1);
    EditorSession editor(scene);
    EditorEnvironment environment;
    EditorPanels panels(editor, environment);
    Gui gui;
    gui.settle(panels);

    gui.click(panels, where(panels, "Undo"));
    gui.click(panels, where(panels, "Redo"));
    gui.click(panels, where(panels, "Delete"));
    REQUIRE(editor.historySize() == 0);
    REQUIRE_FALSE(editor.canRedo());
}

TEST_CASE("the environment checkboxes switch the overrides on")
{
    Scene scene(1);
    EditorSession editor(scene);
    EditorEnvironment environment;
    EditorPanels panels(editor, environment);
    Gui gui;
    gui.settle(panels);
    REQUIRE_FALSE(environment.overrideTime);
    REQUIRE_FALSE(environment.overrideWeather);

    gui.click(panels, where(panels, "Override time"));
    REQUIRE(environment.overrideTime);
    gui.click(panels, where(panels, "Override weather"));
    REQUIRE(environment.overrideWeather);
    gui.click(panels, where(panels, "Override time"));
    REQUIRE_FALSE(environment.overrideTime);
}
