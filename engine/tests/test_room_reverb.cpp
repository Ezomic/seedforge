#include "ichor/audio/room_reverb.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace ichor;

namespace {

constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kMax = std::numeric_limits<float>::max();

const RoomMaterial kMaterials[] = {RoomMaterial::Stone, RoomMaterial::Brick, RoomMaterial::Metal,
                                   RoomMaterial::Timber, RoomMaterial::Earth, RoomMaterial::Cloth};

bool inRange(const RoomReverb& r)
{
    return r.decaySeconds >= kRoomReverbMinDecay && r.decaySeconds <= kRoomReverbMaxDecay &&
           r.wet >= 0.0f && r.wet <= 1.0f && r.damping >= 0.0f && r.damping <= 1.0f;
}

}  // namespace

TEST_CASE("a mid-size stone room follows the Sabine estimate", "[audio][room_reverb]")
{
    // 10 x 10 x 4 box: volume 400, surface 360, stone absorption 0.03 -> 0.161 * 400 / 10.8.
    const RoomReverb r = roomReverbForBox(10.0f, 10.0f, 4.0f, RoomMaterial::Stone);
    CHECK(r.decaySeconds == Catch::Approx(5.963f).margin(0.01f));
    CHECK(inRange(r));
}

TEST_CASE("every material gives in-range output for every size", "[audio][room_reverb]")
{
    for (const RoomMaterial m : kMaterials) {
        for (const float side : {0.001f, 0.5f, 3.0f, 30.0f, 3000.0f, 1.0e9f}) {
            CHECK(inRange(roomReverbForBox(side, side, side, m)));
        }
        CHECK(roomMaterialAbsorption(m) > 0.0f);
        CHECK(roomMaterialAbsorption(m) <= 1.0f);
    }
}

TEST_CASE("a tiny room is short and nearly dry, a huge one long and wet", "[audio][room_reverb]")
{
    const RoomReverb tiny = roomReverbForBox(0.01f, 0.01f, 0.01f, RoomMaterial::Stone);
    const RoomReverb huge = roomReverbForBox(1.0e6f, 1.0e6f, 1.0e6f, RoomMaterial::Stone);
    CHECK(tiny.decaySeconds == kRoomReverbMinDecay);
    CHECK(huge.decaySeconds == kRoomReverbMaxDecay);
    CHECK(huge.wet > tiny.wet + 0.3f);
}

TEST_CASE("zero, negative, NaN and infinite dimensions give the dry preset", "[audio][room_reverb]")
{
    for (const RoomMaterial m : kMaterials) {
        for (const float x : {0.0f, -1.0f, -0.0f, kNan, kInf, -kInf}) {
            for (const RoomReverb& r : {roomReverb(x, 100.0f, m), roomReverb(100.0f, x, m),
                                        roomReverbForBox(x, 5.0f, 5.0f, m),
                                        roomReverbForBox(5.0f, x, 5.0f, m),
                                        roomReverbForBox(5.0f, 5.0f, x, m)}) {
                CHECK(r.decaySeconds == kRoomReverbMinDecay);
                CHECK(r.wet == 0.0f);
                CHECK(inRange(r));
            }
        }
    }
}

TEST_CASE("float extremes never overflow into NaN or out-of-range output", "[audio][room_reverb]")
{
    for (const RoomMaterial m : kMaterials) {
        CHECK(inRange(roomReverb(kMax, 1.0e-30f, m)));
        CHECK(inRange(roomReverb(1.0e-30f, kMax, m)));
        CHECK(inRange(roomReverb(kMax, kMax, m)));
        CHECK(inRange(roomReverbForBox(kMax, kMax, kMax, m)));
        CHECK(inRange(roomReverbForBox(1.0e-30f, 1.0e-30f, 1.0e-30f, m)));
    }
}

TEST_CASE("a bigger hard room never decays faster", "[audio][room_reverb]")
{
    for (const RoomMaterial m : {RoomMaterial::Stone, RoomMaterial::Brick, RoomMaterial::Metal}) {
        RoomReverb previous = roomReverbForBox(0.5f, 0.5f, 0.5f, m);
        for (float side = 0.6f; side < 2000.0f; side *= 1.3f) {
            const RoomReverb r = roomReverbForBox(side, side, side, m);
            CHECK(r.decaySeconds >= previous.decaySeconds);
            CHECK(r.wet >= previous.wet);
            previous = r;
        }
        CHECK(previous.decaySeconds > kRoomReverbMinDecay);
    }
}

TEST_CASE("decay grows with volume and shrinks with surface area", "[audio][room_reverb]")
{
    for (const RoomMaterial m : kMaterials) {
        CHECK(roomReverb(2000.0f, 400.0f, m).decaySeconds >= roomReverb(1000.0f, 400.0f, m).decaySeconds);
        CHECK(roomReverb(1000.0f, 800.0f, m).decaySeconds <= roomReverb(1000.0f, 400.0f, m).decaySeconds);
    }
}

TEST_CASE("a softer material decays faster and sounds more muffled", "[audio][room_reverb]")
{
    const RoomReverb stone = roomReverbForBox(8.0f, 8.0f, 4.0f, RoomMaterial::Stone);
    const RoomReverb timber = roomReverbForBox(8.0f, 8.0f, 4.0f, RoomMaterial::Timber);
    const RoomReverb cloth = roomReverbForBox(8.0f, 8.0f, 4.0f, RoomMaterial::Cloth);
    CHECK(stone.decaySeconds > timber.decaySeconds);
    CHECK(timber.decaySeconds > cloth.decaySeconds);
    CHECK(stone.damping < timber.damping);
    CHECK(timber.damping < cloth.damping);
}

TEST_CASE("an unknown material value falls back instead of misbehaving", "[audio][room_reverb]")
{
    const RoomMaterial bogus = static_cast<RoomMaterial>(200);
    CHECK(inRange(roomReverbForBox(6.0f, 6.0f, 3.0f, bogus)));
    CHECK(roomMaterialAbsorption(bogus) == roomMaterialAbsorption(RoomMaterial::Stone));
}

TEST_CASE("the preset converts to Reverb settings in range", "[audio][room_reverb]")
{
    for (const RoomMaterial m : kMaterials) {
        for (const float side : {0.0f, 1.0f, 20.0f, 1.0e7f}) {
            const ReverbSettings s = toReverbSettings(roomReverbForBox(side, side, side, m));
            CHECK(s.wet >= 0.0f);
            CHECK(s.wet <= 1.0f);
            CHECK(s.roomSize >= 0.0f);
            CHECK(s.roomSize <= 1.0f);
            CHECK(s.damping >= 0.0f);
            CHECK(s.damping <= 1.0f);
        }
    }
    const ReverbSettings small = toReverbSettings(roomReverbForBox(2.0f, 2.0f, 2.0f, RoomMaterial::Stone));
    const ReverbSettings big = toReverbSettings(roomReverbForBox(40.0f, 40.0f, 20.0f, RoomMaterial::Stone));
    CHECK(big.roomSize > small.roomSize);
}

TEST_CASE("a vast flat slab decays by its volume to area ratio, not the clamp", "[audio][room_reverb]")
{
    // V / A is about 0.5 m, so stone gives 0.161 * 0.5 / 0.03 = 2.68 s.
    const RoomReverb slab = roomReverbForBox(1.0e20f, 1.0e20f, 1.0f, RoomMaterial::Stone);
    CHECK(slab.decaySeconds == Catch::Approx(2.683f).margin(0.01f));
}

TEST_CASE("fixed rooms keep their exact presets", "[audio][room_reverb]")
{
    const RoomReverb stone = roomReverbForBox(10.0f, 10.0f, 4.0f, RoomMaterial::Stone);
    CHECK(stone.decaySeconds == Catch::Approx(5.96296f).margin(1.0e-4f));
    CHECK(stone.wet == Catch::Approx(0.08f + 0.52f * (5.96296f - 0.1f) / 7.9f).margin(1.0e-4f));
    CHECK(stone.damping == 0.20f);
    const RoomReverb dry = roomReverb(kNan, 10.0f, RoomMaterial::Cloth);
    CHECK(dry == RoomReverb{kRoomReverbMinDecay, 0.0f, 0.90f});
}
