#include "ichor/world/world_identity.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>

using namespace ichor;

namespace {

WorldDesc smallWorld(std::uint64_t seed)
{
    WorldDesc desc;
    desc.seed = seed;
    desc.sizeMetres = 512;
    desc.metresPerSample = 8;
    desc.cellMetres = 128;
    return desc;
}

}

TEST_CASE("an identity survives its string form")
{
    const WorldIdentity maxed{std::numeric_limits<std::uint64_t>::max(), std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::uint64_t>::max()};
    for (const WorldIdentity id : {WorldIdentity{0, 0, 0}, WorldIdentity{42, 1, 0xdeadbeefcafef00dull}, maxed}) {
        const std::string text = toString(id);
        const auto parsed = parseWorldIdentity(text);
        REQUIRE(parsed.has_value());
        REQUIRE(*parsed == id);
        REQUIRE(toString(*parsed) == text);
    }
}

TEST_CASE("the string form is pinned")
{
    const std::string text = toString({1, 2, 3});
    REQUIRE(text.size() == 54);
    REQUIRE(text.substr(0, 47) == "ichor1.0000000000000001.00000002.00000000000000");
}

TEST_CASE("malformed identity strings are rejected")
{
    const std::string good = toString({42, 1, 7});
    REQUIRE(parseWorldIdentity(good).has_value());
    REQUIRE_FALSE(parseWorldIdentity("").has_value());
    REQUIRE_FALSE(parseWorldIdentity("ichor1").has_value());
    REQUIRE_FALSE(parseWorldIdentity(good.substr(0, good.size() - 1)).has_value());
    REQUIRE_FALSE(parseWorldIdentity(good + "0").has_value());
    REQUIRE_FALSE(parseWorldIdentity(" " + good).has_value());
    REQUIRE_FALSE(parseWorldIdentity(good + "\n").has_value());
    std::string upper = good;
    upper[7] = 'A';
    REQUIRE_FALSE(parseWorldIdentity(upper).has_value());
    std::string wrongPrefix = good;
    wrongPrefix[5] = '2';
    REQUIRE_FALSE(parseWorldIdentity(wrongPrefix).has_value());
    std::string wrongSep = good;
    wrongSep[6] = ':';
    REQUIRE_FALSE(parseWorldIdentity(wrongSep).has_value());
    std::string nonHex = good;
    nonHex[8] = 'g';
    REQUIRE_FALSE(parseWorldIdentity(nonHex).has_value());
    std::string signChar = good;
    signChar[8] = '-';
    REQUIRE_FALSE(parseWorldIdentity(signChar).has_value());
    std::string embeddedNul = good;
    embeddedNul[9] = '\0';
    REQUIRE_FALSE(parseWorldIdentity(std::string_view(embeddedNul)).has_value());
    for (std::size_t i = 0; i < good.size(); ++i) {
        std::string changed = good;
        changed[i] = changed[i] == '5' ? '6' : '5';
        REQUIRE_FALSE(parseWorldIdentity(changed).has_value());
    }
}

TEST_CASE("the same identity forges the same digest, a different one does not")
{
    const auto a = identifyWorld(smallWorld(9));
    const auto b = identifyWorld(smallWorld(9));
    const auto c = identifyWorld(smallWorld(10));
    REQUIRE(a.has_value());
    REQUIRE(*a == *b);
    REQUIRE(a->contentDigest != c->contentDigest);
    REQUIRE(verifyWorld(*a, smallWorld(1)) == WorldMatch::Match);
    REQUIRE(verifyWorld(*parseWorldIdentity(toString(*a)), smallWorld(77)) == WorldMatch::Match);
    REQUIRE(verifyWorld(*c, smallWorld(9)) == WorldMatch::Match);
}

TEST_CASE("a changed digest, version or shape is reported")
{
    const WorldIdentity id = *identifyWorld(smallWorld(9));
    WorldIdentity tampered = id;
    tampered.contentDigest ^= 1;
    REQUIRE(verifyWorld(tampered, smallWorld(9)) == WorldMatch::DigestMismatch);
    WorldIdentity otherSeed = id;
    otherSeed.seed = 10;
    REQUIRE(verifyWorld(otherSeed, smallWorld(9)) == WorldMatch::DigestMismatch);
    WorldIdentity future = id;
    future.genVersion = kForgeGenVersion + 1;
    REQUIRE(verifyWorld(future, smallWorld(9)) == WorldMatch::UnsupportedVersion);
    WorldDesc taller = smallWorld(9);
    taller.heightRange = 401.0f;
    REQUIRE(verifyWorld(id, taller) == WorldMatch::DigestMismatch);
}

TEST_CASE("an unforgeable shape is refused before forging")
{
    const WorldIdentity id{1, kForgeGenVersion, 0};
    WorldDesc zero = smallWorld(1);
    zero.cellMetres = 0;
    REQUIRE(verifyWorld(id, zero) == WorldMatch::InvalidShape);
    REQUIRE_FALSE(identifyWorld(zero).has_value());
    WorldDesc nan = smallWorld(1);
    nan.minHeight = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(verifyWorld(id, nan) == WorldMatch::InvalidShape);
    WorldDesc inf = smallWorld(1);
    inf.heightRange = std::numeric_limits<float>::infinity();
    REQUIRE_FALSE(identifyWorld(inf).has_value());
    WorldDesc huge = smallWorld(1);
    huge.sizeMetres = 0xffffffffu;
    REQUIRE(verifyWorld(id, huge) == WorldMatch::InvalidShape);
}
