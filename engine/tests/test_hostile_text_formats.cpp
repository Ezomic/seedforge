#include "codec_fuzz.h"
#include "hostile_text.h"

#include "ichor/data/featured_seeds.h"
#include "ichor/save/chronicle.h"
#include "ichor/save/ironman.h"
#include "ichor/save/settings.h"
#include "ichor/world/world_identity.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

using namespace ichor;
using ichor::fuzz::Bytes;
using ichor::fuzz::Canonical;
using ichor::fuzz::Options;

namespace {

Bytes bytesOf(const std::string& s) { return Bytes(s.begin(), s.end()); }
std::string textOf(std::span<const std::uint8_t> b) { return std::string(b.begin(), b.end()); }

void expectClean(const ichor::fuzz::Report& report)
{
    INFO(ichor::fuzz::summary(report));
    CHECK(report.cases > 100);
    CHECK(report.rejected > 0);
    CHECK(report.ok());
}

void expectCleanText(const hostile::TextReport& report)
{
    INFO(hostile::summary(report));
    CHECK(report.cases > 100);
    CHECK(report.rejected > 0);
    CHECK(report.ok());
}

}

TEST_CASE("hostile: chronicle text", "[hostile][chronicle]")
{
    Chronicle chronicle;
    REQUIRE(chronicle.add(10, "death", "The smith fell from the bridge"));
    REQUIRE(chronicle.add(250, "deed", "Ada burned the granary", ChronicleTags{{4, 9, 12}, 7, 200}));
    REQUIRE(chronicle.add(900, "trade", "Salt for iron", ChronicleTags{{2}, 3, 20}));
    const std::string valid = chronicle.serialize();

    Options options;
    options.allowNonCanonical = true;
    expectClean(ichor::fuzz::fuzzCodec(bytesOf(valid), [](std::span<const std::uint8_t> b) -> Canonical {
        Chronicle out;
        if (!Chronicle::parse(textOf(b), out)) return std::nullopt;
        return bytesOf(out.serialize());
    }, options));

    expectCleanText(hostile::mutateText(valid, [](std::string_view text) {
        Chronicle out;
        return Chronicle::parse(text, out);
    }));
}

TEST_CASE("hostile: chronicle with a huge entry count line is bounded", "[hostile][chronicle]")
{
    std::string many;
    for (std::size_t i = 0; i < Chronicle::kMaxEntries + 50; ++i) many += "1\tdeed\tx\n";
    Chronicle out;
    const bool ok = Chronicle::parse(many, out);
    CHECK((!ok || out.entries().size() <= Chronicle::kMaxEntries));
}

TEST_CASE("hostile: ironman run text", "[hostile][ironman]")
{
    IronmanRun run;
    run.recordDeath(123456);
    expectClean(ichor::fuzz::fuzzCodec(bytesOf(run.encode()), [](std::span<const std::uint8_t> b) -> Canonical {
        const auto r = IronmanRun::decode(textOf(b));
        if (!r) return std::nullopt;
        return bytesOf(r->encode());
    }));
}

TEST_CASE("hostile: settings text", "[hostile][settings]")
{
    Settings settings;
    const std::string valid = serializeSettings(settings);
    const hostile::TextReport report = hostile::mutateText(valid, [&](std::string_view text) {
        const Settings parsed = parseSettings(text);
        const Settings clamped = parsed.clamped();
        return parsed == clamped;
    });
    INFO(hostile::summary(report));
    CHECK(report.cases > 100);
    CHECK(report.accepted > 0);
    CHECK(report.ok());
}

TEST_CASE("hostile: world identity string", "[hostile][world_identity]")
{
    WorldIdentity identity;
    identity.seed = 0x1122334455667788ull;
    identity.genVersion = 3;
    identity.contentDigest = 0xAABBCCDDEEFF0011ull;
    expectClean(ichor::fuzz::fuzzCodec(bytesOf(toString(identity)), [](std::span<const std::uint8_t> b) -> Canonical {
        const auto r = parseWorldIdentity(textOf(b));
        if (!r) return std::nullopt;
        return bytesOf(toString(*r));
    }));
}

TEST_CASE("hostile: featured seed list string", "[hostile][featured_seeds]")
{
    const std::vector<FeaturedSeed> list = {
        {42, "The drowned crown", {"flood", "league"}},
        {7, "Quiet roads", {}},
        {0xFFFFFFFFFFFFFFFFull, "Last light", {"kronos"}},
    };
    const auto encoded = encodeFeaturedList(list);
    REQUIRE(encoded.has_value());
    expectClean(ichor::fuzz::fuzzCodec(bytesOf(*encoded), [](std::span<const std::uint8_t> b) -> Canonical {
        const auto r = decodeFeaturedList(textOf(b));
        if (!r) return std::nullopt;
        const auto again = encodeFeaturedList(*r);
        if (!again) return Bytes{};
        return bytesOf(*again);
    }));
}
