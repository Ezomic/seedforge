#include "ichor/stage0/telegraph_geometry.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

using namespace ichor;
using namespace ichor::stage0;

namespace {

TelegraphDraw draw(float progress = 1.0f)
{
    TelegraphDraw d;
    d.origin = {0.0, 0.9, 0.0};
    d.forward = {0.0f, 0.0f, -1.0f};
    d.reach = 1.6f;
    d.halfWidth = 0.6f;
    d.progress = progress;
    d.color = glm::vec3(0.7f);
    return d;
}

std::vector<LineSegment> windup(bool heavy, float progress = 1.0f)
{
    std::vector<LineSegment> out;
    appendWindupTelegraph(heavy, draw(progress), nullptr, 0.4f, out);
    return out;
}

bool sameGeometry(const std::vector<LineSegment>& a, const std::vector<LineSegment>& b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].a != b[i].a || a[i].b != b[i].b) return false;
    }
    return true;
}

double totalLength(const std::vector<LineSegment>& segs)
{
    double sum = 0.0;
    for (const LineSegment& s : segs) sum += glm::length(s.b - s.a);
    return sum;
}

bool finiteAll(const std::vector<LineSegment>& segs)
{
    for (const LineSegment& s : segs) {
        for (const glm::dvec3& p : {s.a, s.b}) {
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("light and heavy wind-ups differ in outline with the colour held equal", "[telegraph_geometry]")
{
    const auto light = windup(false);
    const auto heavy = windup(true);
    REQUIRE_FALSE(light.empty());
    REQUIRE_FALSE(heavy.empty());
    CHECK(light.front().color == heavy.front().color);
    CHECK(light.size() != heavy.size());
    CHECK_FALSE(sameGeometry(light, heavy));
    CHECK(totalLength(heavy) > totalLength(light) * 1.5);
}

TEST_CASE("every shipped telegraph draws something and stays finite", "[telegraph_geometry]")
{
    for (const VisualTelegraph& cue : visualTelegraphTable()) {
        std::vector<LineSegment> out;
        appendTelegraphLines(cue, draw(), out);
        CHECK_FALSE(out.empty());
        CHECK(finiteAll(out));
    }
}

TEST_CASE("each pattern changes the stroke set of one shape", "[telegraph_geometry]")
{
    std::vector<std::vector<LineSegment>> seen;
    for (const TelegraphPattern p : {TelegraphPattern::Solid, TelegraphPattern::Dashed, TelegraphPattern::Dotted, TelegraphPattern::Hatched,
                                     TelegraphPattern::Chevrons}) {
        VisualTelegraph cue;
        cue.shape = TelegraphShape::Cone;
        cue.pattern = p;
        std::vector<LineSegment> out;
        appendTelegraphLines(cue, draw(), out);
        for (const auto& earlier : seen) CHECK_FALSE(sameGeometry(earlier, out));
        seen.push_back(out);
    }
}

TEST_CASE("the wind-up cue grows with progress", "[telegraph_geometry]")
{
    CHECK(totalLength(windup(true, 0.25f)) < totalLength(windup(true, 0.75f)));
    CHECK(windup(false, 0.0f).size() == windup(false, 1.0f).size());
}

TEST_CASE("hostile draws append nothing or stay finite", "[telegraph_geometry]")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const VisualTelegraph* cue = findVisualTelegraph(visualTelegraphTable(), TelegraphRole::Skirmisher, "heavy_windup");
    REQUIRE(cue != nullptr);

    for (const float bad : {nan, inf, -inf}) {
        TelegraphDraw d = draw();
        d.progress = bad;
        std::vector<LineSegment> out;
        appendTelegraphLines(*cue, d, out);
        CHECK(out.empty());
        d = draw();
        d.reach = bad;
        appendTelegraphLines(*cue, d, out);
        CHECK(out.empty());
        d = draw();
        d.forward = {bad, 0.0f, 0.0f};
        appendTelegraphLines(*cue, d, out);
        CHECK(out.empty());
        d = draw();
        d.origin = {bad, 0.0, 0.0};
        appendTelegraphLines(*cue, d, out);
        CHECK(out.empty());
    }

    TelegraphDraw zero = draw();
    zero.forward = glm::vec3(0.0f);
    std::vector<LineSegment> out;
    appendTelegraphLines(*cue, zero, out);
    CHECK(out.empty());

    TelegraphDraw extreme = draw(5.0f);
    extreme.reach = 1e30f;
    extreme.halfWidth = -3.0f;
    appendTelegraphLines(*cue, extreme, out);
    CHECK(finiteAll(out));

    std::vector<LineSegment> none;
    appendTelegraphLines(VisualTelegraph{}, draw(), none);
    CHECK(none.empty());
}

TEST_CASE("the off-screen arrow appears only when the foe is out of view", "[telegraph_geometry]")
{
    const glm::dvec3 player{0.0, 0.0, 3.0};
    const glm::dvec3 foe{0.0, 0.0, -2.0};
    const CameraPose camera = followCamera(player, foe);

    std::vector<LineSegment> visible;
    CHECK_FALSE(appendOffscreenArrow(camera, foe, 0.4f, glm::vec3(1.0f), visible));
    CHECK(visible.empty());

    std::vector<LineSegment> hidden;
    const glm::dvec3 behind = camera.position + glm::dvec3(camera.rotation * glm::vec3(0.0f, 0.0f, 5.0f));
    CHECK(appendOffscreenArrow(camera, behind, 0.4f, glm::vec3(1.0f), hidden));
    CHECK(hidden.size() == 3);
    CHECK(finiteAll(hidden));
}

TEST_CASE("the arrow points toward the side the foe left the screen on", "[telegraph_geometry]")
{
    const CameraPose camera = followCamera({0.0, 0.0, 3.0}, {0.0, 0.0, -2.0});
    const auto arrowDx = [&](glm::vec3 viewOffset) {
        std::vector<LineSegment> out;
        const glm::dvec3 target = camera.position + glm::dvec3(camera.rotation * viewOffset);
        REQUIRE(appendOffscreenArrow(camera, target, 0.4f, glm::vec3(1.0f), out));
        const glm::vec3 tip = glm::conjugate(camera.rotation) * glm::vec3(out.front().b - camera.position);
        const glm::vec3 tail = glm::conjugate(camera.rotation) * glm::vec3(out.front().a - camera.position);
        return tip.x - tail.x;
    };
    CHECK(arrowDx({-30.0f, 0.0f, -5.0f}) < 0.0f);
    CHECK(arrowDx({30.0f, 0.0f, -5.0f}) > 0.0f);
}

TEST_CASE("the arrow rejects non-finite targets", "[telegraph_geometry]")
{
    const CameraPose camera = followCamera({0.0, 0.0, 3.0}, {0.0, 0.0, -2.0});
    std::vector<LineSegment> out;
    CHECK_FALSE(appendOffscreenArrow(camera, {std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}, 0.4f, glm::vec3(1.0f), out));
    CHECK_FALSE(appendOffscreenArrow(camera, {std::numeric_limits<double>::infinity(), 0.0, 0.0}, 0.4f, glm::vec3(1.0f), out));
    CHECK(out.empty());
}

TEST_CASE("only the foe wind-up carries the arrow", "[telegraph_geometry]")
{
    const CameraPose camera = followCamera({0.0, 0.0, 3.0}, {0.0, 0.0, -2.0});
    TelegraphDraw d = draw();
    d.origin = camera.position + glm::dvec3(camera.rotation * glm::vec3(0.0f, 0.0f, 5.0f));
    std::vector<LineSegment> withCamera;
    std::vector<LineSegment> without;
    appendWindupTelegraph(false, d, &camera, 0.4f, withCamera);
    appendWindupTelegraph(false, d, nullptr, 0.4f, without);
    CHECK(withCamera.size() == without.size() + 3);
}

TEST_CASE("a foe off screen by depth alone points the arrow down, and a non-finite radius draws nothing", "[telegraph_geometry]")
{
    const CameraPose camera = followCamera({0.0, 0.0, 3.0}, {0.0, 0.0, -2.0});
    const glm::dvec3 behind = camera.position + glm::dvec3(camera.rotation * glm::vec3(0.0f, 0.0f, 5.0f));
    std::vector<LineSegment> out;
    REQUIRE(appendOffscreenArrow(camera, behind, 0.4f, glm::vec3(1.0f), out));
    const glm::vec3 tip = glm::conjugate(camera.rotation) * glm::vec3(out.front().b - camera.position);
    CHECK(tip.y < 0.0f);
    CHECK(std::abs(tip.x) < 1e-3f);

    std::vector<LineSegment> none;
    CHECK_FALSE(appendOffscreenArrow(camera, behind, std::numeric_limits<float>::quiet_NaN(), glm::vec3(1.0f), none));
    CHECK(none.empty());
}
