#include "ichor/sim/physics.h"

#include "ichor/core/log.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <cmath>
#include <map>

namespace ichor {

namespace {

// Jolt asserts, or the float cast overflows, on a NaN, infinite or absurdly large coordinate; none may get past this file.
bool finite(glm::dvec3 v)
{
    constexpr double kLimit = 1.0e9;
    return std::abs(v.x) <= kLimit && std::abs(v.y) <= kLimit && std::abs(v.z) <= kLimit;
}

namespace Layers {
constexpr JPH::ObjectLayer NonMoving = 0;
constexpr JPH::ObjectLayer Moving = 1;
constexpr JPH::ObjectLayer Count = 2;
}

namespace BroadPhaseLayers {
constexpr JPH::BroadPhaseLayer NonMoving(0);
constexpr JPH::BroadPhaseLayer Moving(1);
constexpr JPH::uint Count = 2;
}

class BroadPhaseLayerMap final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::Count; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
    {
        return layer == Layers::NonMoving ? BroadPhaseLayers::NonMoving : BroadPhaseLayers::Moving;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
    {
        return layer == BroadPhaseLayers::NonMoving ? "NonMoving" : "Moving";
    }
#endif
};

class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override
    {
        return layer == Layers::Moving || broadPhase == BroadPhaseLayers::Moving;
    }
};

class ObjectPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
    {
        return a == Layers::Moving || b == Layers::Moving;
    }
};

// Jolt runs in single precision here. Fine for the 20 m arena; the open world
// at E5 adds a floating origin so bodies stay near zero (doc 17, rule 8).
JPH::Vec3 toJolt(glm::vec3 v) { return {v.x, v.y, v.z}; }
JPH::Quat toJolt(glm::quat q) { return {q.x, q.y, q.z, q.w}; }
glm::vec3 fromJolt(JPH::Vec3Arg v) { return {v.GetX(), v.GetY(), v.GetZ()}; }

void registerJoltOnce()
{
    static bool done = false;
    if (done) {
        return;
    }
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    done = true;
}

constexpr float kGravity = -9.81f;

}

struct PhysicsWorld::Impl {
    // First member on purpose: the allocator and the physics system below
    // allocate through Jolt, which needs its global allocator registered first.
    struct Registrar {
        Registrar() { registerJoltOnce(); }
    } registrar;
    BroadPhaseLayerMap broadPhaseMap;
    ObjectVsBroadPhaseFilter objectVsBroadPhase;
    ObjectPairFilter objectPair;
    JPH::TempAllocatorImpl temp{8 * 1024 * 1024};
    JPH::JobSystemSingleThreaded jobs{JPH::cMaxPhysicsJobs};
    JPH::PhysicsSystem system;

    struct StaticEntry {
        JPH::BodyID body;
        JPH::ShapeRefC shape;
        glm::dvec3 worldPosition{0.0};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 halfExtents{0.0f}; // boxes only, for debug lines
        bool heightField = false;
        std::uint32_t sampleCount = 0;
        float spacing = 0.0f;
    };
    struct CharacterEntry {
        JPH::Ref<JPH::CharacterVirtual> character;
        float radius = 0.0f;
        float halfHeight = 0.0f;
        glm::vec3 intent{0.0f};
        float verticalVelocity = 0.0f;
        bool onGround = false;
    };
    // Ordered by id so iteration order, and therefore the step, is deterministic.
    std::map<std::uint64_t, StaticEntry> statics;
    std::map<std::uint64_t, CharacterEntry> characters;
    glm::dvec3 origin{0.0};

    JPH::RVec3 toLocal(glm::dvec3 world) const
    {
        const glm::dvec3 local = world - origin;
        return {static_cast<float>(local.x), static_cast<float>(local.y), static_cast<float>(local.z)};
    }
    glm::dvec3 toWorld(JPH::RVec3Arg local) const
    {
        return origin + glm::dvec3(local.GetX(), local.GetY(), local.GetZ());
    }

    void createStatic(StaticEntry& entry)
    {
        JPH::BodyInterface& bodies = system.GetBodyInterface();
        JPH::BodyCreationSettings settings(entry.shape, toLocal(entry.worldPosition), toJolt(entry.rotation), JPH::EMotionType::Static, Layers::NonMoving);
        entry.body = bodies.CreateAndAddBody(settings, JPH::EActivation::DontActivate);
    }

    Impl()
    {
        system.Init(1024, 0, 1024, 1024, broadPhaseMap, objectVsBroadPhase, objectPair);
        system.SetGravity(JPH::Vec3(0.0f, kGravity, 0.0f));
    }

    void removeStatic(StaticEntry& entry)
    {
        if (entry.body.IsInvalid()) {
            return;
        }
        JPH::BodyInterface& bodies = system.GetBodyInterface();
        bodies.RemoveBody(entry.body);
        bodies.DestroyBody(entry.body);
        entry.body = JPH::BodyID();
    }
};

PhysicsWorld::PhysicsWorld()
    : impl_(std::make_unique<Impl>())
{
}

PhysicsWorld::~PhysicsWorld()
{
    clear();
}

void PhysicsWorld::addStaticBox(StableId id, const StaticBox& box, const Transform& transform)
{
    remove(id);
    if (!finite(transform.position)) {
        log(LogLevel::Error, "physics: non-finite position, static box dropped");
        return;
    }
    Impl::StaticEntry entry;
    entry.shape = new JPH::BoxShape(toJolt(box.halfExtents));
    entry.worldPosition = transform.position;
    entry.rotation = transform.rotation;
    entry.halfExtents = box.halfExtents;
    impl_->createStatic(entry);
    if (entry.body.IsInvalid()) {
        log(LogLevel::Error, "physics: body limit reached, static box dropped");
        return;
    }
    impl_->statics.emplace(id.value, std::move(entry));
}

void PhysicsWorld::addHeightField(StableId id, const float* heights, std::uint32_t sampleCount, glm::dvec3 origin, float spacing)
{
    if (!finite(origin)) {
        log(LogLevel::Error, "physics: non-finite origin, height field dropped");
        return;
    }
    remove(id);
    JPH::HeightFieldShapeSettings settings(heights, JPH::Vec3::sZero(), JPH::Vec3(spacing, 1.0f, spacing), sampleCount);
    settings.mBlockSize = 4;
    settings.mBitsPerSample = 16;
    auto shape = settings.Create();
    if (shape.HasError()) {
        logf(LogLevel::Error, "physics: height field rejected: {}", shape.GetError().c_str());
        return;
    }
    Impl::StaticEntry entry;
    entry.shape = shape.Get();
    entry.worldPosition = origin;
    entry.heightField = true;
    entry.sampleCount = sampleCount;
    entry.spacing = spacing;
    impl_->createStatic(entry);
    if (entry.body.IsInvalid()) {
        log(LogLevel::Error, "physics: body limit reached, height field dropped");
        return;
    }
    impl_->statics.emplace(id.value, std::move(entry));
}

void PhysicsWorld::addCharacter(StableId id, const CharacterBody& body, const Transform& transform)
{
    if (!finite(transform.position)) {
        log(LogLevel::Error, "physics: non-finite position, character dropped");
        return;
    }
    remove(id);
    JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
    settings->mMaxSlopeAngle = JPH::DegreesToRadians(45.0f);
    settings->mShape = JPH::RotatedTranslatedShapeSettings(JPH::Vec3(0.0f, body.halfHeight + body.radius, 0.0f), JPH::Quat::sIdentity(),
        new JPH::CapsuleShape(body.halfHeight, body.radius)).Create().Get();
    settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -body.radius);

    Impl::CharacterEntry entry;
    entry.character = new JPH::CharacterVirtual(settings, impl_->toLocal(transform.position), JPH::Quat::sIdentity(), 0, &impl_->system);
    entry.radius = body.radius;
    entry.halfHeight = body.halfHeight;
    entry.intent = body.intent;
    entry.verticalVelocity = body.fallSpeed;
    entry.onGround = body.onGround;
    entry.character->SetLinearVelocity(toJolt(body.velocity));
    impl_->characters.emplace(id.value, std::move(entry));
    refreshCharacters();
}

void PhysicsWorld::setOrigin(glm::dvec3 origin)
{
    if (!finite(origin)) {
        return;
    }
    if (origin == impl_->origin) {
        return;
    }
    // Characters keep their world position; their local one moves with the origin.
    std::map<std::uint64_t, glm::dvec3> worldPositions;
    for (auto& [id, entry] : impl_->characters) {
        worldPositions[id] = impl_->toWorld(entry.character->GetPosition());
    }
    impl_->origin = origin;
    for (auto& [id, entry] : impl_->statics) {
        impl_->removeStatic(entry);
        impl_->createStatic(entry);
    }
    for (auto& [id, entry] : impl_->characters) {
        entry.character->SetPosition(impl_->toLocal(worldPositions[id]));
    }
    refreshCharacters();
}

glm::dvec3 PhysicsWorld::origin() const
{
    return impl_->origin;
}

bool PhysicsWorld::hasBody(StableId id) const
{
    return impl_->statics.contains(id.value) || impl_->characters.contains(id.value);
}

void PhysicsWorld::refreshCharacters()
{
    // A character reports "in air" until it has looked for contacts; without
    // this a restored or teleported character takes one tick of gravity.
    for (auto& [id, entry] : impl_->characters) {
        entry.character->RefreshContacts(impl_->system.GetDefaultBroadPhaseLayerFilter(Layers::Moving),
            impl_->system.GetDefaultLayerFilter(Layers::Moving), {}, {}, impl_->temp);
        entry.onGround = entry.character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
    }
}

void PhysicsWorld::remove(StableId id)
{
    if (auto found = impl_->statics.find(id.value); found != impl_->statics.end()) {
        impl_->removeStatic(found->second);
        impl_->statics.erase(found);
    }
    impl_->characters.erase(id.value);
}

void PhysicsWorld::clear()
{
    for (auto& [id, entry] : impl_->statics) {
        impl_->removeStatic(entry);
    }
    impl_->statics.clear();
    impl_->characters.clear();
}

void PhysicsWorld::setCharacterIntent(StableId id, glm::vec3 horizontalVelocity)
{
    if (auto found = impl_->characters.find(id.value); found != impl_->characters.end()) {
        found->second.intent = glm::vec3(horizontalVelocity.x, 0.0f, horizontalVelocity.z);
    }
}

void PhysicsWorld::setCharacterPosition(StableId id, glm::dvec3 position)
{
    if (!finite(position)) {
        return;
    }
    if (auto found = impl_->characters.find(id.value); found != impl_->characters.end()) {
        found->second.character->SetPosition(impl_->toLocal(position));
        found->second.verticalVelocity = 0.0f;
        refreshCharacters();
    }
}

void PhysicsWorld::step(double dt)
{
    const float fdt = static_cast<float>(dt);
    const JPH::Vec3 gravity(0.0f, kGravity, 0.0f);
    for (auto& [id, entry] : impl_->characters) {
        JPH::CharacterVirtual& character = *entry.character;
        entry.onGround = character.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
        entry.verticalVelocity = entry.onGround ? 0.0f : entry.verticalVelocity + kGravity * fdt;
        character.SetLinearVelocity(JPH::Vec3(entry.intent.x, entry.verticalVelocity, entry.intent.z));

        JPH::CharacterVirtual::ExtendedUpdateSettings settings;
        character.ExtendedUpdate(fdt, gravity, settings,
            impl_->system.GetDefaultBroadPhaseLayerFilter(Layers::Moving),
            impl_->system.GetDefaultLayerFilter(Layers::Moving), {}, {}, impl_->temp);
    }
    impl_->system.Update(fdt, 1, &impl_->temp, &impl_->jobs);
}

bool PhysicsWorld::character(StableId id, CharacterState& out) const
{
    const auto found = impl_->characters.find(id.value);
    if (found == impl_->characters.end()) {
        return false;
    }
    const Impl::CharacterEntry& entry = found->second;
    out.position = impl_->toWorld(entry.character->GetPosition());
    out.velocity = fromJolt(entry.character->GetLinearVelocity());
    out.fallSpeed = entry.verticalVelocity;
    out.onGround = entry.character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
    return true;
}

void PhysicsWorld::debugLines(std::vector<LineSegment>& out) const
{
    for (const auto& [id, entry] : impl_->statics) {
        if (entry.heightField) {
            // The cell outline at its base; the terrain mesh shows the rest.
            const double side = static_cast<double>(entry.sampleCount - 1) * entry.spacing;
            const glm::dvec3 o = entry.worldPosition;
            const glm::vec3 color(0.2f, 0.7f, 0.9f);
            out.push_back({o, o + glm::dvec3(side, 0.0, 0.0), color});
            out.push_back({o, o + glm::dvec3(0.0, 0.0, side), color});
            out.push_back({o + glm::dvec3(side, 0.0, 0.0), o + glm::dvec3(side, 0.0, side), color});
            out.push_back({o + glm::dvec3(0.0, 0.0, side), o + glm::dvec3(side, 0.0, side), color});
            continue;
        }
        const glm::dvec3 center = entry.worldPosition;
        const JPH::Quat rotation = toJolt(entry.rotation);
        const glm::vec3 h = entry.halfExtents;
        glm::dvec3 corners[8];
        for (int i = 0; i < 8; ++i) {
            const JPH::Vec3 local((i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z);
            corners[i] = center + glm::dvec3(fromJolt(rotation * local));
        }
        const glm::vec3 color(0.9f, 0.6f, 0.2f);
        for (int i = 0; i < 8; ++i) {
            for (int bit = 1; bit < 8; bit <<= 1) {
                if ((i & bit) == 0) {
                    out.push_back({corners[i], corners[i | bit], color});
                }
            }
        }
    }
    for (const auto& [id, entry] : impl_->characters) {
        const glm::dvec3 base = impl_->toWorld(entry.character->GetPosition());
        const glm::vec3 color = entry.onGround ? glm::vec3(0.3f, 0.9f, 0.4f) : glm::vec3(0.9f, 0.3f, 0.3f);
        const double r = entry.radius;
        const double top = entry.radius * 2.0 + entry.halfHeight * 2.0;
        constexpr int kSegments = 16;
        for (int i = 0; i < kSegments; ++i) {
            const double a0 = 6.283185307179586 * i / kSegments;
            const double a1 = 6.283185307179586 * (i + 1) / kSegments;
            const glm::dvec3 d0(std::cos(a0) * r, 0.0, std::sin(a0) * r);
            const glm::dvec3 d1(std::cos(a1) * r, 0.0, std::sin(a1) * r);
            out.push_back({base + d0 + glm::dvec3(0.0, r, 0.0), base + d1 + glm::dvec3(0.0, r, 0.0), color});
            out.push_back({base + d0 + glm::dvec3(0.0, top - r, 0.0), base + d1 + glm::dvec3(0.0, top - r, 0.0), color});
            if (i % 4 == 0) {
                out.push_back({base + d0 + glm::dvec3(0.0, r, 0.0), base + d0 + glm::dvec3(0.0, top - r, 0.0), color});
            }
        }
    }
}

}
