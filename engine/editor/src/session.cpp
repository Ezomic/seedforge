#include "ichor/editor/session.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>

namespace ichor {

EditorSession::EditorSession(Scene& scene, std::size_t maxHistory)
    : scene_(scene)
    , maxHistory_(std::max<std::size_t>(maxHistory, 1))
    , pendingTick_(scene.tickCount())
{
}

void EditorSession::forgetPendingIfTicked() const
{
    // A tick applies every command asked for before it, so once the clock has moved the
    // registry is the truth again.
    if (scene_.tickCount() != pendingTick_) {
        pending_.clear();
        pendingTick_ = scene_.tickCount();
    }
}

std::optional<EditorSession::EntityState> EditorSession::stateOf(StableId id) const
{
    forgetPendingIfTicked();
    if (const auto found = pending_.find(id); found != pending_.end()) {
        return found->second;
    }
    const entt::entity entity = scene_.find(id);
    if (entity == entt::null) {
        return std::nullopt;
    }
    const entt::registry& registry = scene_.registry();
    EntityState state;
    state.name = registry.get<Name>(entity).value;
    state.transform = registry.get<Transform>(entity);
    if (const auto* mesh = registry.try_get<MeshRef>(entity)) state.mesh = *mesh;
    if (const auto* spin = registry.try_get<Spin>(entity)) state.spin = *spin;
    if (const auto* box = registry.try_get<StaticBox>(entity)) state.box = *box;
    return state;
}

bool EditorSession::editable(StableId id) const
{
    const entt::entity entity = scene_.find(id);
    if (entity == entt::null) {
        return true; // not applied yet, or not there: stateOf decides
    }
    // An allow-list: the session can rebuild exactly what it captures, so an entity with any
    // other component, now or one added later, is the game's and is left alone.
    const entt::registry& registry = scene_.registry();
    std::size_t present = 0;
    for (const auto&& storage : registry.storage()) {
        present += storage.second.contains(entity) ? 1 : 0;
    }
    const std::size_t known = 3 + (registry.all_of<PreviousTransform>(entity) ? 1 : 0) + (registry.all_of<MeshRef>(entity) ? 1 : 0)
        + (registry.all_of<Spin>(entity) ? 1 : 0) + (registry.all_of<StaticBox>(entity) ? 1 : 0); // StableId, Name, Transform
    return present == known;
}

std::optional<StableId> EditorSession::selection() const
{
    return selection_ && exists(*selection_) ? selection_ : std::nullopt;
}

void EditorSession::reset()
{
    pending_.clear();
    pendingTick_ = scene_.tickCount();
    undo_.clear();
    redo_.clear();
    transaction_.clear();
    depth_ = 0;
    selection_.reset();
}

// Whether putting a step back would spawn something whose id is taken again, which
// the scene would silently ignore.
bool EditorSession::conflicts(const Step& step, bool undoing) const
{
    for (const Edit& edit : step) {
        const std::optional<EntityState>& from = undoing ? edit.after : edit.before;
        const std::optional<EntityState>& to = undoing ? edit.before : edit.after;
        if (!from && to && exists(edit.id)) {
            return true;
        }
    }
    return false;
}

bool EditorSession::exists(StableId id) const
{
    return stateOf(id).has_value();
}

std::optional<Transform> EditorSession::transformOf(StableId id) const
{
    const auto state = stateOf(id);
    return state ? std::optional<Transform>(state->transform) : std::nullopt;
}

std::vector<OutlinerEntry> EditorSession::outliner() const
{
    forgetPendingIfTicked();
    std::vector<OutlinerEntry> entries;
    for (auto [entity, id, name] : scene_.registry().view<const StableId, const Name>().each()) {
        const auto found = pending_.find(id);
        if (found != pending_.end() && !found->second) {
            continue; // destroyed this tick
        }
        // What was asked for this tick wins over what the registry still holds.
        entries.push_back({id, found != pending_.end() ? found->second->name : name.value, selection_ && *selection_ == id});
    }
    for (const auto& [id, state] : pending_) {
        if (state && scene_.find(id) == entt::null) {
            entries.push_back({id, state->name, selection_ && *selection_ == id});
        }
    }
    std::sort(entries.begin(), entries.end(), [](const OutlinerEntry& a, const OutlinerEntry& b) {
        return a.name != b.name ? a.name < b.name : a.id < b.id;
    });
    return entries;
}

void EditorSession::select(std::optional<StableId> id)
{
    selection_ = id && exists(*id) ? id : std::nullopt;
}

void EditorSession::apply(StableId id, const std::optional<EntityState>& from, const std::optional<EntityState>& to)
{
    forgetPendingIfTicked();
    if (from && !to) {
        scene_.enqueue(DestroyCommand{id});
        if (selection_ && *selection_ == id) {
            selection_.reset();
        }
    } else if (!from && to) {
        SpawnCommand spawn;
        spawn.id = id;
        spawn.name.value = to->name;
        spawn.transform = to->transform;
        spawn.mesh = to->mesh;
        spawn.spin = to->spin;
        spawn.staticBox = to->box;
        scene_.enqueue(spawn);
    } else if (from && to) {
        Transform placed = to->transform;
        // A spinning entity turns by itself; undoing a move must not snap it back to an old angle.
        if (to->spin) {
            if (const auto live = stateOf(id)) {
                placed.rotation = live->transform.rotation;
            }
        }
        scene_.enqueue(SetTransformCommand{id, placed});
        pending_[id] = EntityState{to->name, placed, to->mesh, to->spin, to->box};
        return;
    }
    pending_[id] = to;
}

void EditorSession::record(Edit edit)
{
    redo_.clear();
    if (depth_ > 0) {
        transaction_.push_back(std::move(edit));
        return;
    }
    undo_.push_back({std::move(edit)});
    if (undo_.size() > maxHistory_) {
        undo_.erase(undo_.begin());
    }
}

std::optional<StableId> EditorSession::spawn(std::string_view name, glm::dvec3 position, std::optional<MeshRef> mesh)
{
    if (name.empty()) {
        return std::nullopt;
    }
    const StableId id = StableId::of(name);
    if (exists(id)) {
        return std::nullopt;
    }
    EntityState state;
    state.name = std::string(name);
    state.transform.position = position;
    state.mesh = mesh;
    apply(id, std::nullopt, state);
    record({id, std::nullopt, state});
    return id;
}

bool EditorSession::destroy(StableId id)
{
    const auto before = stateOf(id);
    if (!before || !editable(id)) {
        return false;
    }
    apply(id, before, std::nullopt);
    record({id, before, std::nullopt});
    return true;
}

bool EditorSession::setTransform(StableId id, const Transform& transform)
{
    const auto before = stateOf(id);
    if (!before || !editable(id)) {
        return false;
    }
    // A gizmo that accumulates rotations drifts off the unit sphere; one that breaks yields nonsense.
    const auto finite = [](glm::dvec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    const float length = glm::length(transform.rotation);
    if (!finite(transform.position) || !finite(glm::dvec3(transform.scale)) || !std::isfinite(length) || length < 1e-6f) {
        return false;
    }
    EntityState after = *before;
    after.transform = transform;
    after.transform.rotation = transform.rotation / length;
    apply(id, before, after);
    record({id, before, after});
    return true;
}

void EditorSession::beginTransaction()
{
    if (depth_++ == 0) {
        transaction_.clear();
    }
}

void EditorSession::endTransaction()
{
    if (depth_ == 0 || --depth_ > 0) {
        return;
    }
    if (transaction_.empty()) {
        return;
    }
    undo_.push_back(std::move(transaction_));
    transaction_.clear();
    if (undo_.size() > maxHistory_) {
        undo_.erase(undo_.begin());
    }
}

bool EditorSession::undo()
{
    if (depth_ > 0 || undo_.empty()) {
        return false;
    }
    Step step = std::move(undo_.back());
    undo_.pop_back();
    if (conflicts(step, true)) {
        return false; // the step is dropped: it can no longer be applied
    }
    for (auto edit = step.rbegin(); edit != step.rend(); ++edit) {
        apply(edit->id, edit->after, edit->before);
    }
    redo_.push_back(std::move(step));
    return true;
}

bool EditorSession::redo()
{
    if (depth_ > 0 || redo_.empty()) {
        return false;
    }
    Step step = std::move(redo_.back());
    redo_.pop_back();
    if (conflicts(step, false)) {
        return false;
    }
    for (const Edit& edit : step) {
        apply(edit.id, edit.before, edit.after);
    }
    undo_.push_back(std::move(step));
    return true;
}

}
