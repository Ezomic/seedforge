#include "ichor/assets/bundle.h"
#include "ichor/stage0/character_pose.h"
#include "ichor/stage0/grey_box_mesh.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace ichor::stage0;

namespace {

PoseInput at(CombatState state, AttackKind attack, float time)
{
    PoseInput in;
    in.state = state;
    in.attack = attack;
    in.stateTime = time;
    in.windup = attack == AttackKind::Heavy ? 0.7f : 0.35f;
    in.active = attack == AttackKind::Heavy ? 0.2f : 0.15f;
    in.recovery = attack == AttackKind::Heavy ? 0.8f : 0.4f;
    return in;
}

glm::vec3 tip(const PartPose& spear)
{
    return spear.position + spear.axis * 1.4f;
}

float distance(const CharacterPose& a, const CharacterPose& b)
{
    return glm::distance(tip(a.spear), tip(b.spear)) + glm::distance(a.spear.position, b.spear.position) + std::abs(a.lean - b.lean);
}

}

TEST_CASE("the same input always gives the same pose", "[pose]")
{
    const CharacterPose a = characterPose(at(CombatState::Windup, AttackKind::Heavy, 0.31f));
    const CharacterPose b = characterPose(at(CombatState::Windup, AttackKind::Heavy, 0.31f));
    CHECK(a.lean == b.lean);
    CHECK(a.spear.position == b.spear.position);
    CHECK(a.spear.axis == b.spear.axis);
    CHECK(a.shield.position == b.shield.position);
}

TEST_CASE("every combat state reads differently from idle", "[pose]")
{
    const CharacterPose idle = characterPose(at(CombatState::Idle, AttackKind::Light, 0.0f));
    for (const CombatState state : {CombatState::Windup, CombatState::Active, CombatState::Recovery, CombatState::Dodging, CombatState::Staggered, CombatState::Dead}) {
        CAPTURE(name(state));
        const CharacterPose pose = characterPose(at(state, AttackKind::Light, 0.1f));
        CHECK(distance(idle, pose) > 0.05f);
    }
}

TEST_CASE("the wind-up is a visible, growing pose rather than a snap", "[pose]")
{
    for (const AttackKind kind : {AttackKind::Light, AttackKind::Heavy}) {
        const float length = kind == AttackKind::Heavy ? 0.7f : 0.35f;
        const CharacterPose idle = characterPose(at(CombatState::Idle, kind, 0.0f));
        const CharacterPose early = characterPose(at(CombatState::Windup, kind, length * 0.25f));
        const CharacterPose late = characterPose(at(CombatState::Windup, kind, length * 0.9f));
        CAPTURE(static_cast<int>(kind));
        CHECK(distance(idle, early) > 0.02f);
        CHECK(distance(idle, late) > distance(idle, early));
    }
}

TEST_CASE("light and heavy wind-ups are different shapes, not just different lengths", "[pose]")
{
    const CharacterPose light = characterPose(at(CombatState::Windup, AttackKind::Light, 0.35f));
    const CharacterPose heavy = characterPose(at(CombatState::Windup, AttackKind::Heavy, 0.7f));
    // Light draws the spear back level; heavy lifts it overhead.
    CHECK(tip(heavy.spear).y - tip(light.spear).y > 1.0f);
    CHECK(heavy.spear.axis.y > 0.7f);
    CHECK(std::abs(light.spear.axis.y) < 0.3f);
    CHECK(heavy.lean < light.lean);
    CHECK(distance(light, heavy) > 1.5f);
}

TEST_CASE("the strike for each attack ends where its reach is", "[pose]")
{
    const CharacterPose light = characterPose(at(CombatState::Active, AttackKind::Light, 0.15f));
    const CharacterPose heavy = characterPose(at(CombatState::Active, AttackKind::Heavy, 0.2f));
    CHECK(tip(light.spear).z < -1.5f);
    CHECK(tip(heavy.spear).z < -1.5f);
    CHECK(tip(light.spear).y > tip(heavy.spear).y); // heavy comes down, light stays level
}

TEST_CASE("recovery returns to the idle pose", "[pose]")
{
    for (const AttackKind kind : {AttackKind::Light, AttackKind::Heavy}) {
        const CharacterPose idle = characterPose(at(CombatState::Idle, kind, 0.0f));
        const CharacterPose end = characterPose(at(CombatState::Recovery, kind, 10.0f));
        CHECK(distance(idle, end) < 1e-4f);
    }
}

TEST_CASE("blocking raises the shield in front of the body", "[pose]")
{
    PoseInput in = at(CombatState::Idle, AttackKind::Light, 0.0f);
    const CharacterPose down = characterPose(in);
    in.blocking = true;
    const CharacterPose up = characterPose(in);
    CHECK(glm::distance(down.shield.position, up.shield.position) > 0.2f);
    CHECK(std::abs(up.shield.position.x) < std::abs(down.shield.position.x));
}

TEST_CASE("the dead lie down and the living stand", "[pose]")
{
    const CharacterPose standing = characterPose(at(CombatState::Idle, AttackKind::Light, 0.0f));
    const CharacterPose fallen = characterPose(at(CombatState::Dead, AttackKind::Light, 5.0f));
    CHECK(standing.fall == 0.0f);
    CHECK(fallen.fall == 1.0f);
    const CharacterTransforms t = characterTransforms(fallen, glm::dvec3(0.0), 0.0f);
    // The body's long axis, local +Y, ends up horizontal.
    CHECK(std::abs((t.body.basis * glm::vec3(0.0f, 1.0f, 0.0f)).y) < 1e-4f);
}

TEST_CASE("hostile numbers never produce a non-finite pose", "[pose]")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (const CombatState state : {CombatState::Idle, CombatState::Windup, CombatState::Active, CombatState::Recovery, CombatState::Dodging, CombatState::Staggered, CombatState::Dead}) {
        for (const float v : {nan, inf, -inf, -1.0f, 1e30f}) {
            PoseInput in = at(state, AttackKind::Heavy, v);
            in.windup = v;
            in.active = v;
            in.recovery = v;
            in.dodgeDuration = v;
            const CharacterTransforms t = characterTransforms(characterPose(in), glm::dvec3(1.0, 0.0, 2.0), v);
            for (const PartTransform* part : {&t.body, &t.spear, &t.shield}) {
                CHECK(std::isfinite(part->position.x));
                CHECK(std::isfinite(part->position.y));
                CHECK(std::isfinite(part->position.z));
                for (int c = 0; c < 3; ++c) {
                    for (int r = 0; r < 3; ++r) CHECK(std::isfinite(part->basis[c][r]));
                }
            }
        }
    }
}

TEST_CASE("basisAlong carries +Y onto the axis, including the awkward ones", "[pose]")
{
    for (const glm::vec3 axis : {glm::vec3(0, 1, 0), glm::vec3(0, -1, 0), glm::vec3(1, 0, 0), glm::vec3(0, 0.3f, -0.9f), glm::vec3(0, -0.99999f, 0.001f)}) {
        const glm::vec3 n = glm::normalize(axis);
        const glm::vec3 got = basisAlong(n) * glm::vec3(0.0f, 1.0f, 0.0f);
        CHECK(glm::distance(got, n) < 5e-3f);
    }
    CHECK(basisAlong(glm::vec3(0.0f)) == glm::mat3(1.0f));
    CHECK(basisAlong(glm::vec3(std::numeric_limits<float>::quiet_NaN())) == glm::mat3(1.0f));
}

TEST_CASE("the pose turns with the character's yaw", "[pose]")
{
    const CharacterPose pose = characterPose(at(CombatState::Active, AttackKind::Light, 0.15f));
    const CharacterTransforms north = characterTransforms(pose, glm::dvec3(0.0), 0.0f);
    const CharacterTransforms east = characterTransforms(pose, glm::dvec3(0.0), 1.5707963f);
    const glm::vec3 northTip = glm::vec3(north.spear.position) + north.spear.basis * glm::vec3(0.0f, 1.4f, 0.0f);
    const glm::vec3 eastTip = glm::vec3(east.spear.position) + east.spear.basis * glm::vec3(0.0f, 1.4f, 0.0f);
    CHECK(northTip.z < -1.5f);
    CHECK(eastTip.x > 1.5f); // yaw 90 degrees faces +X, as forwardFromYaw does
}

namespace {

void checkWindingMatchesNormals(const ichor::Mesh& mesh)
{
    REQUIRE(!mesh.indices.empty());
    REQUIRE(mesh.indices.size() % 3 == 0);
    int checked = 0;
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
        REQUIRE(mesh.indices[i] < mesh.vertices.size());
        const auto& a = mesh.vertices[mesh.indices[i]];
        const auto& b = mesh.vertices[mesh.indices[i + 1]];
        const auto& c = mesh.vertices[mesh.indices[i + 2]];
        const glm::vec3 face = glm::cross(b.position - a.position, c.position - a.position);
        if (glm::length(face) < 1e-7f) continue; // pole triangles collapse to a line
        const glm::vec3 normal = a.normal + b.normal + c.normal;
        CHECK(glm::dot(face, normal) > 0.0f);
        ++checked;
    }
    CHECK(checked > 20);
}

}

TEST_CASE("grey box meshes are well formed and outward facing", "[pose][mesh]")
{
    ichor::SceneBundle bundle;
    const GreyBoxMeshes ids = appendGreyBoxMeshes(bundle);
    REQUIRE(bundle.meshes.size() == 3);
    REQUIRE(bundle.materials.size() == 1);
    CHECK(ids.body != ids.spear);
    CHECK(ids.spear != ids.shield);
    for (const ichor::Mesh& mesh : bundle.meshes) {
        CAPTURE(mesh.name);
        CHECK(mesh.submeshes.front().material == 0);
        for (const auto& v : mesh.vertices) {
            CHECK(std::isfinite(v.position.x + v.position.y + v.position.z));
            CHECK(std::abs(glm::length(v.normal) - 1.0f) < 1e-3f);
        }
        checkWindingMatchesNormals(mesh);
    }
    const ichor::Mesh& body = bundle.meshes[ids.body];
    CHECK_THAT(body.boundsMin.y, Catch::Matchers::WithinAbs(0.0, 1e-4));
    CHECK_THAT(body.boundsMax.y, Catch::Matchers::WithinAbs(1.8, 1e-3));
    const ichor::Mesh& spear = bundle.meshes[ids.spear];
    CHECK_THAT(spear.boundsMax.y, Catch::Matchers::WithinAbs(1.4, 1e-4));
}

TEST_CASE("a finished phase never reads as unstarted, whatever the numbers", "[pose]")
{
    PoseInput dead = at(CombatState::Dead, AttackKind::Light, 1e38f);
    CHECK(characterPose(dead).fall == 1.0f);
    PoseInput recovering = at(CombatState::Recovery, AttackKind::Light, 1e38f);
    recovering.recovery = 1e-5f;
    const CharacterPose idle = characterPose(at(CombatState::Idle, AttackKind::Light, 0.0f));
    CHECK(distance(idle, characterPose(recovering)) < 1e-4f);
}

TEST_CASE("a fallen body rests on the ground rather than sinking into it", "[pose]")
{
    const CharacterPose fallen = characterPose(at(CombatState::Dead, AttackKind::Light, 5.0f));
    const CharacterTransforms t = characterTransforms(fallen, glm::dvec3(0.0), 0.0f);
    const ichor::Mesh body = capsuleMesh(kBodyRadius, 0.6f, 0);
    double lowest = 1e9;
    for (const auto& v : body.vertices) {
        lowest = std::min(lowest, t.body.position.y + static_cast<double>((t.body.basis * v.position).y));
    }
    CHECK(lowest > -1e-3);
}
