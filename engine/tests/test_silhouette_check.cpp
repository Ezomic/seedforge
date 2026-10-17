#include "ichor/gfx/silhouette_check.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace ichor;

namespace {

SilhouetteMask blank(int w, int h)
{
    SilhouetteMask m;
    m.width = w;
    m.height = h;
    m.cells.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    return m;
}

void fillRect(SilhouetteMask& m, int x0, int y0, int x1, int y1)
{
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            m.cells[static_cast<std::size_t>(y) * static_cast<std::size_t>(m.width) + static_cast<std::size_t>(x)] = 1;
        }
    }
}

SilhouetteMask enemyBlob()
{
    SilhouetteMask m = blank(64, 64);
    fillRect(m, 24, 12, 40, 52);
    return m;
}

}

TEST_CASE("a dark enemy against a clear backdrop reads")
{
    const SilhouetteResult r = checkSilhouette(enemyBlob(), blank(64, 64), ClutterDepth::Behind, {});
    REQUIRE(r.status == SilhouetteStatus::Ok);
    CHECK(r.passes);
    CHECK(r.legibleEdges > 0.99f);
    CHECK(r.occlusion == 0.0f);
}

TEST_CASE("an enemy toned like the foliage behind it is flagged, and a contrasting one is not")
{
    const SilhouetteMask clutter = denseFoliageMask(64, 64, 0.9f, 7);
    SilhouetteParams matched;
    matched.enemyLuma = matched.clutterLuma;
    SilhouetteParams distinct;
    distinct.enemyLuma = 0.05f;
    const SilhouetteResult bad = checkSilhouette(enemyBlob(), clutter, ClutterDepth::Behind, matched);
    const SilhouetteResult good = checkSilhouette(enemyBlob(), clutter, ClutterDepth::Behind, distinct);
    CHECK_FALSE(bad.passes);
    CHECK(good.passes);
    CHECK(good.legibleEdges - bad.legibleEdges > 0.4f);
    CHECK(good.meanContrast > bad.meanContrast);
}

TEST_CASE("foliage in front lowers legibility as it gets denser")
{
    const SilhouetteMask enemy = enemyBlob();
    float previous = 2.0f;
    for (const float density : {0.0f, 0.3f, 0.6f, 0.95f}) {
        const SilhouetteResult r = checkSilhouette(enemy, denseFoliageMask(64, 64, density, 3), ClutterDepth::InFront, {});
        REQUIRE(r.status == SilhouetteStatus::Ok);
        CHECK(r.legibleEdges <= previous);
        previous = r.legibleEdges;
    }
    const SilhouetteResult sparse = checkSilhouette(enemy, denseFoliageMask(64, 64, 0.1f, 3), ClutterDepth::InFront, {});
    const SilhouetteResult dense = checkSilhouette(enemy, denseFoliageMask(64, 64, 0.95f, 3), ClutterDepth::InFront, {});
    CHECK(sparse.passes);
    CHECK_FALSE(dense.passes);
    CHECK(dense.occlusion > 0.8f);
    CHECK(sparse.occlusion < 0.2f);
}

TEST_CASE("full cover hides the whole outline")
{
    const SilhouetteResult r = checkSilhouette(enemyBlob(), denseFoliageMask(64, 64, 1.0f, 1), ClutterDepth::InFront, {});
    CHECK(r.occlusion == 1.0f);
    CHECK(r.legibleEdges == 0.0f);
    CHECK(r.meanContrast == 0.0f);
    CHECK_FALSE(r.passes);
}

TEST_CASE("a thin shape reads worse than it would with a contrasting tone under the same clutter")
{
    SilhouetteMask thin = blank(64, 64);
    fillRect(thin, 31, 4, 33, 60);
    SilhouetteParams p;
    p.enemyLuma = p.clutterLuma;
    const SilhouetteMask clutter = denseFoliageMask(64, 64, 0.7f, 11);
    const SilhouetteResult t = checkSilhouette(thin, clutter, ClutterDepth::Behind, p);
    p.enemyLuma = 0.05f;
    const SilhouetteResult c = checkSilhouette(thin, clutter, ClutterDepth::Behind, p);
    CHECK(c.legibleEdges > t.legibleEdges);
}

TEST_CASE("the batch checker reports every shape in order and fails them all under dense cover")
{
    SilhouetteMask wall = blank(64, 64);
    fillRect(wall, 8, 8, 56, 56);
    const std::vector<NamedSilhouette> shapes = {{"blob", enemyBlob()}, {"wall", wall}};
    SilhouetteParams p;
    p.enemyLuma = 0.05f;

    const auto sparse = checkSilhouettes(shapes, denseFoliageMask(64, 64, 0.1f, 5), p);
    REQUIRE(sparse.size() == 2);
    CHECK(sparse[0].id == "blob");
    CHECK(sparse[1].id == "wall");
    CHECK(sparse[0].passes());
    CHECK(sparse[1].passes());

    const auto dense = checkSilhouettes(shapes, denseFoliageMask(64, 64, 0.95f, 5), p);
    REQUIRE(dense.size() == 2);
    CHECK_FALSE(dense[0].passes());
    CHECK_FALSE(dense[1].passes());
    CHECK(checkSilhouettes({}, denseFoliageMask(64, 64, 0.5f, 5), p).empty());
}

TEST_CASE("hostile input is rejected without crashing")
{
    const SilhouetteMask ok = enemyBlob();
    SilhouetteMask wrongCells = ok;
    wrongCells.cells.pop_back();
    SilhouetteMask huge;
    huge.width = 1 << 30;
    huge.height = 1 << 30;
    SilhouetteMask negative = ok;
    negative.width = -4;

    CHECK(checkSilhouette(wrongCells, ok, ClutterDepth::Behind, {}).status == SilhouetteStatus::BadMask);
    CHECK(checkSilhouette(huge, ok, ClutterDepth::Behind, {}).status == SilhouetteStatus::BadMask);
    CHECK(checkSilhouette(ok, negative, ClutterDepth::Behind, {}).status == SilhouetteStatus::BadMask);
    CHECK(checkSilhouette(SilhouetteMask{}, SilhouetteMask{}, ClutterDepth::Behind, {}).status == SilhouetteStatus::BadMask);
    CHECK(checkSilhouette(ok, blank(32, 32), ClutterDepth::Behind, {}).status == SilhouetteStatus::SizeMismatch);
    CHECK(checkSilhouette(blank(64, 64), ok, ClutterDepth::Behind, {}).status == SilhouetteStatus::EmptyEnemy);

    SilhouetteParams nan;
    nan.enemyLuma = std::numeric_limits<float>::quiet_NaN();
    CHECK(checkSilhouette(ok, ok, ClutterDepth::Behind, nan).status == SilhouetteStatus::BadParams);
    SilhouetteParams inf;
    inf.minEdgeContrast = std::numeric_limits<float>::infinity();
    CHECK(checkSilhouette(ok, ok, ClutterDepth::Behind, inf).status == SilhouetteStatus::BadParams);
    SilhouetteParams out;
    out.backdropLuma = -0.1f;
    CHECK(checkSilhouette(ok, ok, ClutterDepth::Behind, out).status == SilhouetteStatus::BadParams);
}

TEST_CASE("the foliage generator is deterministic and rejects bad sizes")
{
    CHECK(denseFoliageMask(32, 32, 0.5f, 9).cells == denseFoliageMask(32, 32, 0.5f, 9).cells);
    CHECK(denseFoliageMask(32, 32, 0.5f, 9).cells != denseFoliageMask(32, 32, 0.5f, 10).cells);
    CHECK_FALSE(denseFoliageMask(0, 5, 0.5f, 1).valid());
    CHECK_FALSE(denseFoliageMask(kSilhouetteMaxDim + 1, 5, 0.5f, 1).valid());
    CHECK(denseFoliageMask(8, 8, std::numeric_limits<float>::quiet_NaN(), 1).count() == 0);
    CHECK(denseFoliageMask(8, 8, 1.0f, 1).count() == 64);
    const std::size_t half = denseFoliageMask(128, 128, 0.5f, 2).count();
    CHECK(half > 7000);
    CHECK(half < 9400);
}
