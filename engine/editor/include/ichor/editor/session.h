#pragma once

#include "ichor/sim/scene.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// What the outliner shows for one entity.
struct OutlinerEntry {
    StableId id;
    std::string name;
    bool selected = false;
};

// The logic of the editor, without the ImGui: a selection, an outliner, and
// edits that go through scene commands (so they enter the command log like
// any other change) with undo and redo. Commands apply at the start of the
// next tick, so the session keeps its own record of what it has asked for in
// the current tick and answers queries from that.
//
// Only entities made of a name, a transform and optionally a mesh, a spin or
// a static box can be edited; players, characters and animated entities
// belong to the game's systems and are left alone.
class EditorSession {
public:
    explicit EditorSession(Scene& scene, std::size_t maxHistory = 256);

    std::vector<OutlinerEntry> outliner() const;
    // The selected entity, or nothing once it has gone, whoever removed it.
    std::optional<StableId> selection() const;
    void select(std::optional<StableId> id);
    bool exists(StableId id) const;
    std::optional<Transform> transformOf(StableId id) const;
    std::optional<std::string> nameOf(StableId id) const;

    // Each is one undo step, or part of the open transaction. spawn returns the
    // new entity's id, or nothing when the name is empty or already taken.
    std::optional<StableId> spawn(std::string_view name, glm::dvec3 position, std::optional<MeshRef> mesh = std::nullopt);
    bool destroy(StableId id);
    bool setTransform(StableId id, const Transform& transform);
    // Moves the entity by an offset from where the session last put it, so two nudges in one tick add up.
    // Nothing changes for an entity that is gone or an offset that is not finite.
    bool nudge(StableId id, glm::dvec3 offset);

    // Everything between begin and end is one undo step: a drag sends many
    // transforms and should take one undo to take back.
    void beginTransaction();
    void endTransaction();

    bool canUndo() const { return depth_ == 0 && !undo_.empty(); }
    bool canRedo() const { return depth_ == 0 && !redo_.empty(); }
    bool undo();
    bool redo();
    std::size_t historySize() const { return undo_.size(); }

    // Forgets everything the session knows: the history, the selection and what it had queued.
    // A restore of the scene (loading a save) invalidates all of it, so the loader calls this.
    void reset();

private:
    struct EntityState {
        std::string name;
        Transform transform;
        std::optional<MeshRef> mesh;
        std::optional<Spin> spin;
        std::optional<StaticBox> box;
    };
    // One change and its inverse as states: a missing before is a spawn, a missing after a destroy.
    struct Edit {
        StableId id;
        std::optional<EntityState> before;
        std::optional<EntityState> after;
    };
    using Step = std::vector<Edit>;

    std::optional<EntityState> stateOf(StableId id) const;
    bool editable(StableId id) const;
    bool conflicts(const Step& step, bool undoing) const;
    void apply(StableId id, const std::optional<EntityState>& from, const std::optional<EntityState>& to);
    void record(Edit edit);
    void forgetPendingIfTicked() const;

    Scene& scene_;
    std::size_t maxHistory_;
    std::optional<StableId> selection_;
    std::vector<Step> undo_;
    std::vector<Step> redo_;
    Step transaction_;
    int depth_ = 0; // open transactions; they nest, and only the outermost closes the step

    // What the session has asked for this tick and the scene has not applied yet.
    mutable std::map<StableId, std::optional<EntityState>> pending_;
    mutable std::uint64_t pendingTick_ = 0;
};

}
