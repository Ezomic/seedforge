#include "ichor/audio/oneshot_scheduler.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <utility>
#include <vector>

using namespace ichor;
using Catch::Approx;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

using Emitters = std::vector<OneShotEmitter>;

OneShotEmitter emitterAt(std::uint32_t id, double x, double z, double radius = 100.0)
{
    OneShotEmitter e;
    e.id = id;
    e.x = x;
    e.z = z;
    e.radius = radius;
    e.meanIntervalSeconds = 10.0;
    e.minIntervalSeconds = 5.0;
    e.durationSeconds = 3.0;
    return e;
}

Emitters one(OneShotEmitter e)
{
    return Emitters{e};
}

// Runs 1 s ticks and returns every cue with the tick it fired on.
std::vector<std::pair<int, OneShotCue>> run(OneShotScheduler& s, const OneShotListener& l,
    const Emitters& em, int seconds)
{
    std::vector<std::pair<int, OneShotCue>> out;
    for (int t = 0; t < seconds; ++t) {
        for (const OneShotCue& c : s.step(1.0, l, em)) {
            out.emplace_back(t, c);
        }
    }
    return out;
}

}

TEST_CASE("gain falls to nothing at the radius and is full on top of the emitter", "[oneshot]")
{
    CHECK(OneShotScheduler::gainAt(0.0, 100.0) == Approx(1.0f));
    CHECK(OneShotScheduler::gainAt(50.0, 100.0) == Approx(0.25f));
    CHECK(OneShotScheduler::gainAt(99.999, 100.0) < 0.001f);
    CHECK(OneShotScheduler::gainAt(100.0, 100.0) == 0.0f);
    CHECK(OneShotScheduler::gainAt(500.0, 100.0) == 0.0f);
    CHECK(OneShotScheduler::gainAt(-5.0, 100.0) == Approx(1.0f));
    CHECK(OneShotScheduler::gainAt(kNaN, 100.0) == 0.0f);
    CHECK(OneShotScheduler::gainAt(10.0, 0.0) == 0.0f);
    CHECK(OneShotScheduler::gainAt(10.0, kNaN) == 0.0f);
    CHECK(OneShotScheduler::gainAt(kInf, kInf) == 0.0f);
}

TEST_CASE("pan follows the side the emitter is on", "[oneshot]")
{
    const OneShotListener l; // at the origin looking down -z
    CHECK(OneShotScheduler::panFor(l, 10.0, 0.0) == Approx(1.0f));
    CHECK(OneShotScheduler::panFor(l, -10.0, 0.0) == Approx(-1.0f));
    CHECK(OneShotScheduler::panFor(l, 0.0, -10.0) == Approx(0.0f).margin(1e-6));
    CHECK(OneShotScheduler::panFor(l, 0.0, 10.0) == Approx(0.0f).margin(1e-6));
    CHECK(OneShotScheduler::panFor(l, 0.0, 0.0) == 0.0f);

    OneShotListener turned;
    turned.forwardX = 1.0; // looking down +x: +z is now on the right
    turned.forwardZ = 0.0;
    CHECK(OneShotScheduler::panFor(turned, 0.0, 10.0) == Approx(1.0f));

    OneShotListener blank;
    blank.forwardX = 0.0;
    blank.forwardZ = 0.0;
    CHECK(OneShotScheduler::panFor(blank, 10.0, 0.0) == 0.0f);
    OneShotListener nan;
    nan.forwardX = kNaN;
    CHECK(OneShotScheduler::panFor(nan, 10.0, 0.0) == 0.0f);
    CHECK(OneShotScheduler::panFor(l, kNaN, 0.0) == 0.0f);
}

TEST_CASE("nothing fires outside the radius, and the radius edge is outside", "[oneshot]")
{
    OneShotScheduler s(1);
    const OneShotListener l;
    CHECK(run(s, l, one(emitterAt(1, 100.0, 0.0, 100.0)), 600).empty());
    CHECK(run(s, l, one(emitterAt(2, 500.0, 0.0, 100.0)), 600).empty());
    CHECK_FALSE(run(s, l, one(emitterAt(3, 99.0, 0.0, 100.0)), 600).empty());
}

TEST_CASE("a one-shot never fires the moment the listener arrives", "[oneshot]")
{
    OneShotScheduler s(1);
    CHECK(s.step(1.0, OneShotListener{}, one(emitterAt(1, 10.0, 0.0))).empty());
}

TEST_CASE("the same seed repeats exactly and another seed differs", "[oneshot]")
{
    const Emitters em{emitterAt(1, 10.0, 0.0), emitterAt(2, -20.0, 5.0)};
    const OneShotListener l;
    OneShotScheduler a(42);
    OneShotScheduler b(42);
    OneShotScheduler c(43);
    const auto ra = run(a, l, em, 600);
    const auto rb = run(b, l, em, 600);
    const auto rc = run(c, l, em, 600);
    REQUIRE(ra.size() == rb.size());
    REQUIRE_FALSE(ra.empty());
    for (std::size_t i = 0; i < ra.size(); ++i) {
        CHECK(ra[i].first == rb[i].first);
        CHECK(ra[i].second.emitterId == rb[i].second.emitterId);
        CHECK(ra[i].second.gain == rb[i].second.gain);
    }
    bool differs = ra.size() != rc.size();
    for (std::size_t i = 0; !differs && i < ra.size(); ++i) {
        differs = ra[i].first != rc[i].first;
    }
    CHECK(differs);
}

TEST_CASE("tick size does not change how often it fires", "[oneshot]")
{
    const Emitters em = one(emitterAt(1, 10.0, 0.0));
    const OneShotListener l;
    OneShotScheduler coarse(7);
    OneShotScheduler fine(7);
    int a = 0;
    int b = 0;
    for (int i = 0; i < 600; ++i) {
        a += static_cast<int>(coarse.step(1.0, l, em).size());
        for (int k = 0; k < 10; ++k) {
            b += static_cast<int>(fine.step(0.1, l, em).size());
        }
    }
    CHECK(a > 20);
    CHECK(std::abs(a - b) <= 2);
}

TEST_CASE("the average pace is about the mean interval and never faster than the minimum", "[oneshot]")
{
    OneShotScheduler s(9);
    OneShotEmitter e = emitterAt(1, 0.0, 0.0);
    e.durationSeconds = 0.0;
    const auto cues = run(s, OneShotListener{}, one(e), 10000);
    REQUIRE(cues.size() > 700); // mean 10 s over 10000 s is about 1000
    CHECK(cues.size() < 1300);
    for (std::size_t i = 1; i < cues.size(); ++i) {
        CHECK(cues[i].first - cues[i - 1].first >= 5);
    }
}

TEST_CASE("the cooldown holds back a short interval", "[oneshot]")
{
    OneShotScheduler s(3);
    OneShotEmitter e = emitterAt(1, 0.0, 0.0);
    e.meanIntervalSeconds = 0.2;
    e.minIntervalSeconds = 20.0;
    e.durationSeconds = 0.0;
    const auto cues = run(s, OneShotListener{}, one(e), 200);
    REQUIRE(cues.size() >= 5);
    CHECK(cues.size() <= 10);
    for (std::size_t i = 1; i < cues.size(); ++i) {
        CHECK(cues[i].first - cues[i - 1].first >= 20);
    }
}

TEST_CASE("simultaneous voices never exceed the cap", "[oneshot]")
{
    OneShotScheduler s(5, 2);
    Emitters em;
    for (std::uint32_t i = 0; i < 20; ++i) {
        OneShotEmitter e = emitterAt(i + 1, static_cast<double>(i), 3.0);
        e.meanIntervalSeconds = 1.0;
        e.minIntervalSeconds = 0.0;
        e.durationSeconds = 8.0;
        em.push_back(e);
    }
    std::size_t total = 0;
    for (int t = 0; t < 300; ++t) {
        total += s.step(1.0, OneShotListener{}, em).size();
        REQUIRE(s.activeVoices() <= 2);
    }
    CHECK(total > 20); // the cap defers cues, it does not silence the layer
}

TEST_CASE("a zero cap fires nothing", "[oneshot]")
{
    OneShotScheduler s(5, 0);
    CHECK(run(s, OneShotListener{}, one(emitterAt(1, 0.0, 0.0)), 1000).empty());
}

TEST_CASE("cue gain and pan come from where the emitter is", "[oneshot]")
{
    OneShotScheduler s(2);
    const auto cues = run(s, OneShotListener{}, one(emitterAt(1, 25.0, 0.0, 100.0)), 100);
    REQUIRE_FALSE(cues.empty());
    CHECK(cues[0].second.gain == Approx(0.5625f));
    CHECK(cues[0].second.pan == Approx(1.0f));
    CHECK(cues[0].second.durationSeconds == Approx(3.0));
}

TEST_CASE("leaving the radius restarts the wait", "[oneshot]")
{
    // The wait is at least 5 s and at most 15 s, so after 20 s in range a cue has fired.
    const Emitters em = one(emitterAt(1, 10.0, 0.0, 50.0));
    OneShotListener far;
    far.x = 1000.0;
    for (std::uint64_t seed = 0; seed < 8; ++seed) {
        OneShotScheduler control(seed);
        OneShotScheduler left(seed);
        int controlCues = 0;
        int leftCues = 0;
        for (int i = 0; i < 4; ++i) {
            controlCues += static_cast<int>(control.step(1.0, OneShotListener{}, em).size());
            leftCues += static_cast<int>(left.step(1.0, OneShotListener{}, em).size());
        }
        left.step(1.0, far, em);
        for (int i = 0; i < 4; ++i) {
            // Back in range, the wait starts over, so nothing sounds for at least 5 s.
            leftCues += static_cast<int>(left.step(1.0, OneShotListener{}, em).size());
        }
        CHECK(leftCues == 0);
        for (int i = 0; i < 20; ++i) {
            controlCues += static_cast<int>(control.step(1.0, OneShotListener{}, em).size());
        }
        CHECK(controlCues >= 1);
    }
}

TEST_CASE("hostile numbers never fire and never crash", "[oneshot]")
{
    OneShotScheduler s(8);
    OneShotListener bad;
    bad.x = kNaN;
    CHECK(s.step(1.0, bad, one(emitterAt(1, 0.0, 0.0))).empty());
    bad.x = kInf;
    CHECK(s.step(1.0, bad, one(emitterAt(1, 0.0, 0.0))).empty());
    bad.x = 1e300;
    CHECK(s.step(1.0, bad, one(emitterAt(1, 0.0, 0.0))).empty());

    Emitters em;
    em.push_back(emitterAt(1, kNaN, 0.0));
    em.push_back(emitterAt(2, kInf, 0.0));
    em.push_back(emitterAt(3, 1e300, 0.0, 1e301));
    em.push_back(emitterAt(4, 0.0, 0.0, kNaN));
    em.push_back(emitterAt(5, 0.0, 0.0, -1.0));
    em.push_back(emitterAt(6, 0.0, 0.0, 0.0));
    OneShotEmitter nanTimes = emitterAt(7, 0.0, 0.0);
    nanTimes.meanIntervalSeconds = kNaN;
    nanTimes.minIntervalSeconds = kInf;
    nanTimes.durationSeconds = kNaN;
    nanTimes.baseGain = std::numeric_limits<float>::quiet_NaN();
    em.push_back(nanTimes);
    for (int i = 0; i < 100; ++i) {
        for (const OneShotCue& c : s.step(1.0, OneShotListener{}, em)) {
            CHECK(std::isfinite(c.gain));
            CHECK(c.gain >= 0.0f);
            CHECK(c.gain <= 1.0f);
            CHECK(std::isfinite(c.pan));
        }
        CHECK(s.step(kNaN, OneShotListener{}, em).empty());
        CHECK(s.step(-5.0, OneShotListener{}, em).empty());
        CHECK(s.step(kInf, OneShotListener{}, em).size() <= 1);
    }
}

TEST_CASE("a huge pause counts as one second, not an hour", "[oneshot]")
{
    OneShotScheduler s(4);
    const Emitters em = one(emitterAt(1, 10.0, 0.0));
    s.step(1.0, OneShotListener{}, em);
    // The shortest possible wait is 5 s, so one clamped step cannot fire it.
    CHECK(s.step(3600.0, OneShotListener{}, em).empty());
}

TEST_CASE("duplicate ids are one emitter", "[oneshot]")
{
    OneShotScheduler s(6, 1000000);
    const Emitters em(5000, emitterAt(1, 0.0, 0.0));
    for (int i = 0; i < 200; ++i) {
        CHECK(s.step(1.0, OneShotListener{}, em).size() <= 1);
    }
}

TEST_CASE("an oversized list keeps the lowest ids whatever the order", "[oneshot]")
{
    Emitters em;
    for (std::uint32_t i = 0; i < 1000; ++i) {
        OneShotEmitter e = emitterAt(1000 - i, 0.0, 0.0);
        e.meanIntervalSeconds = 0.01;
        e.minIntervalSeconds = 0.0;
        e.durationSeconds = 0.0;
        em.push_back(e);
    }
    OneShotScheduler s(6, 100000);
    s.step(1.0, OneShotListener{}, em);
    const auto cues = s.step(1.0, OneShotListener{}, em);
    REQUIRE(cues.size() == OneShotScheduler::kMaxEmitters);
    CHECK(cues.front().emitterId == 1);
    CHECK(cues.back().emitterId == OneShotScheduler::kMaxEmitters);
}

TEST_CASE("cues come out in emitter id order whatever the input order", "[oneshot]")
{
    OneShotEmitter a = emitterAt(9, 0.0, 0.0);
    OneShotEmitter b = emitterAt(2, 0.0, 0.0);
    a.meanIntervalSeconds = b.meanIntervalSeconds = 0.01;
    a.minIntervalSeconds = b.minIntervalSeconds = 0.0;
    a.durationSeconds = b.durationSeconds = 0.0;
    OneShotScheduler s(1, 8);
    const Emitters em{a, b};
    s.step(1.0, OneShotListener{}, em);
    const auto cues = s.step(1.0, OneShotListener{}, em);
    REQUIRE(cues.size() == 2);
    CHECK(cues[0].emitterId == 2);
    CHECK(cues[1].emitterId == 9);
}
