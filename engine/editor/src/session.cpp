#include "ichor/editor/session.h"

#include <algorithm>

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
    const entt::registry& registry = scene_.registry();
    return !registry.all_of<Player>(entity) && !registry.all_of<CharacterBody>(entity) && !registry.all_of<Animator>(entity);
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
        if (const auto found = pending_.find(id); found != pending_.end() && !found->second) {
            continue; // destroyed this tick
        }
        entries.push_back({id, name.value, selection_ && *selection_ == id});
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
        scene_.enqueue(SetTransformCommand{id, to->transform});
    }
    pending_[id] = to;
}

void EditorSession::record(Edit edit)
{
    redo_.clear();
    if (inTransaction_) {
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
    EntityState after = *before;
    after.transform = transform;
    apply(id, before, after);
    record({id, before, after});
    return true;
}

void EditorSession::beginTransaction()
{
    if (!inTransaction_) {
        inTransaction_ = true;
        transaction_.clear();
    }
}

void EditorSession::endTransaction()
{
    if (!inTransaction_) {
        return;
    }
    inTransaction_ = false;
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
    if (inTransaction_ || undo_.empty()) {
        return false;
    }
    Step step = std::move(undo_.back());
    undo_.pop_back();
    for (auto edit = step.rbegin(); edit != step.rend(); ++edit) {
        apply(edit->id, edit->after, edit->before);
    }
    redo_.push_back(std::move(step));
    return true;
}

bool EditorSession::redo()
{
    if (inTransaction_ || redo_.empty()) {
        return false;
    }
    Step step = std::move(redo_.back());
    redo_.pop_back();
    for (const Edit& edit : step) {
        apply(edit.id, edit.before, edit.after);
    }
    undo_.push_back(std::move(step));
    return true;
}

}
