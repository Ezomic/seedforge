#include "ichor/editor/panels.h"

#include <glm/gtc/quaternion.hpp>

#include <string>

namespace ichor {

namespace {

constexpr float kRadiansToDegrees = 57.2957795f;

}

EditorPanels::EditorPanels(EditorSession& session, EditorEnvironment& environment)
    : session_(session)
    , environment_(environment)
{
}

std::optional<ImVec2> EditorPanels::centreOf(const std::string& label) const
{
    const auto found = centres_.find(label);
    return found == centres_.end() ? std::nullopt : std::optional<ImVec2>(found->second);
}

void EditorPanels::remember(const std::string& label)
{
    const ImVec2 low = ImGui::GetItemRectMin();
    const ImVec2 high = ImGui::GetItemRectMax();
    centres_[label] = {(low.x + high.x) * 0.5f, (low.y + high.y) * 0.5f};
}

void EditorPanels::draw()
{
    centres_.clear();
    ImGui::SetNextWindowPos({12.0f, 140.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({340.0f, 560.0f}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Editor")) {
        toolbar();
        ImGui::Separator();
        outliner();
        ImGui::Separator();
        inspector();
        ImGui::Separator();
        environmentControls();
    }
    ImGui::End();
}

void EditorPanels::toolbar()
{
    ImGui::BeginDisabled(!session_.canUndo());
    if (ImGui::Button("Undo")) {
        session_.undo();
    }
    ImGui::EndDisabled();
    remember("Undo");
    ImGui::SameLine();

    ImGui::BeginDisabled(!session_.canRedo());
    if (ImGui::Button("Redo")) {
        session_.redo();
    }
    ImGui::EndDisabled();
    remember("Redo");
    ImGui::SameLine();

    if (ImGui::Button("Spawn")) {
        // The first name nobody has taken.
        std::string name;
        do {
            name = "object " + std::to_string(++spawned_);
        } while (session_.exists(StableId::of(name)));
        if (const auto id = session_.spawn(name, environment_.spawnAt)) {
            session_.select(id);
        }
    }
    remember("Spawn");
    ImGui::SameLine();

    ImGui::BeginDisabled(!session_.selection().has_value());
    if (ImGui::Button("Delete")) {
        if (const auto selected = session_.selection()) {
            session_.destroy(*selected);
        }
    }
    ImGui::EndDisabled();
    remember("Delete");
}

void EditorPanels::outliner()
{
    ImGui::TextUnformatted("Outliner");
    if (ImGui::BeginChild("outliner", {0.0f, 140.0f}, ImGuiChildFlags_Borders)) {
        for (const OutlinerEntry& entry : session_.outliner()) {
            // The id keeps two entities of one name apart.
            const std::string label = entry.name + "##" + std::to_string(entry.id.value);
            if (ImGui::Selectable(label.c_str(), entry.selected)) {
                session_.select(entry.id);
            }
            remember(label);
            if (!centres_.contains(entry.name)) {
                centres_[entry.name] = centres_[label];
            }
        }
    }
    ImGui::EndChild();
}

void EditorPanels::inspector()
{
    ImGui::TextUnformatted("Inspector");
    const auto selected = session_.selection();
    const auto current = selected ? session_.transformOf(*selected) : std::nullopt;
    if (!selected || !current) {
        // A drag that lost its entity mid-way would leave the step open for ever.
        if (dragged_) {
            session_.endTransaction();
            dragged_.reset();
        }
        ImGui::TextDisabled("nothing selected");
        return;
    }
    if (dragged_ && *dragged_ != *selected) {
        session_.endTransaction();
        dragged_.reset();
    }

    // Dragging a field sends a transform per frame; the drag as a whole is one undo step.
    Transform edited = *current;
    const auto field = [&](const char* label, auto&& widget) {
        const bool modified = widget();
        remember(label);
        if (ImGui::IsItemActivated()) {
            session_.beginTransaction();
            dragged_ = selected;
        }
        if (modified) {
            session_.setTransform(*selected, edited);
        }
        if (ImGui::IsItemDeactivated()) {
            session_.endTransaction();
            dragged_.reset();
        }
    };

    double position[3] = {edited.position.x, edited.position.y, edited.position.z};
    field("position", [&] {
        const bool modified = ImGui::DragScalarN("position", ImGuiDataType_Double, position, 3, 0.05f);
        edited.position = {position[0], position[1], position[2]};
        return modified;
    });

    const glm::vec3 euler = glm::eulerAngles(edited.rotation) * kRadiansToDegrees;
    float degrees[3] = {euler.x, euler.y, euler.z};
    field("rotation", [&] {
        const bool modified = ImGui::DragFloat3("rotation", degrees, 0.5f, -180.0f, 180.0f, "%.1f");
        if (modified) {
            edited.rotation = glm::quat(glm::vec3(degrees[0], degrees[1], degrees[2]) / kRadiansToDegrees);
        }
        return modified;
    });

    float scale[3] = {edited.scale.x, edited.scale.y, edited.scale.z};
    field("scale", [&] {
        const bool modified = ImGui::DragFloat3("scale", scale, 0.01f, 0.01f, 100.0f, "%.2f");
        edited.scale = {scale[0], scale[1], scale[2]};
        return modified;
    });
}

void EditorPanels::environmentControls()
{
    ImGui::TextUnformatted("Environment");
    ImGui::Checkbox("Override time", &environment_.overrideTime);
    remember("Override time");
    ImGui::BeginDisabled(!environment_.overrideTime);
    ImGui::SliderFloat("Hour", &environment_.hour, 0.0f, 24.0f, "%.1f");
    remember("Hour");
    ImGui::EndDisabled();

    ImGui::Checkbox("Override weather", &environment_.overrideWeather);
    remember("Override weather");
    ImGui::BeginDisabled(!environment_.overrideWeather);
    ImGui::SliderFloat("Overcast", &environment_.weather.overcast, 0.0f, 1.0f, "%.2f");
    remember("Overcast");
    ImGui::SliderFloat("Rain", &environment_.weather.rain, 0.0f, 1.0f, "%.2f");
    remember("Rain");
    ImGui::EndDisabled();
}

}
