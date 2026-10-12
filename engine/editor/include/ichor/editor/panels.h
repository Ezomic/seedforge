#pragma once

#include "ichor/editor/session.h"
#include "ichor/world/weather.h"

#include <imgui.h>

#include <map>
#include <optional>
#include <string>

namespace ichor {

// What the editor can override in the world around the scene: the hour, the
// weather, and where a new object appears. The app reads it each frame.
struct EditorEnvironment {
    bool overrideTime = false;
    float hour = 9.0f;
    bool overrideWeather = false;
    Weather weather;
    glm::dvec3 spawnAt{0.0, 0.0, 0.0};
};

// The editor's ImGui windows over an EditorSession: a toolbar of undo, redo,
// spawn and delete, an outliner, an inspector for the selected entity's
// transform, and the environment overrides. draw() builds them between
// ImGui::NewFrame and ImGui::Render.
class EditorPanels {
public:
    EditorPanels(EditorSession& session, EditorEnvironment& environment);

    void draw();

    // Where the last frame put the control with this label, in screen pixels.
    // The tests click there.
    std::optional<ImVec2> centreOf(const std::string& label) const;

private:
    void remember(const std::string& label);
    void toolbar();
    void outliner();
    void inspector();
    void environmentControls();

    EditorSession& session_;
    EditorEnvironment& environment_;
    std::map<std::string, ImVec2> centres_;
    std::uint32_t spawned_ = 0;
};

}
