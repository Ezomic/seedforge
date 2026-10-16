#include "ichor/save/codex_codec.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace ichor;

namespace {

CodexStore sample()
{
    CodexStore store;
    REQUIRE(store.unlock(CodexCategory::Axiom, "salt-age") == CodexResult::Unlocked);
    REQUIRE(store.unlock(CodexCategory::Synergy, "trade.hieroph") == CodexResult::Unlocked);
    REQUIRE(store.unlock(CodexCategory::Faction, "satrapy") == CodexResult::Unlocked);
    REQUIRE(store.unlock(CodexCategory::World, "w_1") == CodexResult::Unlocked);
    REQUIRE(store.unlock(CodexCategory::Seed, "s-1") == CodexResult::Unlocked);
    return store;
}

CodexStore twoAxioms()
{
    CodexStore store;
    store.unlock(CodexCategory::Axiom, "aa");
    store.unlock(CodexCategory::Axiom, "bb");
    return store;
}

// Header is 9 bytes; each entry here is category, length, two id bytes.
constexpr std::size_t kSecondEntry = 13;

} // namespace

TEST_CASE("codex round trips through bytes", "[codex_codec]")
{
    const auto bytes = encodeCodex(sample());
    const auto decoded = decodeCodex(bytes);
    REQUIRE(decoded.ok());
    REQUIRE(decoded.store.size() == 5);
    CHECK(decoded.store.has(CodexCategory::Synergy, "trade.hieroph"));
    CHECK(decoded.store.all()[4].id == "s-1");
    CHECK(encodeCodex(decoded.store) == bytes);
}

TEST_CASE("empty codex round trips", "[codex_codec]")
{
    const auto bytes = encodeCodex(CodexStore{});
    CHECK(bytes.size() == 9);
    const auto decoded = decodeCodex(bytes);
    REQUIRE(decoded.ok());
    CHECK(decoded.store.size() == 0);
}

TEST_CASE("codex bytes are fixed and versioned", "[codex_codec]")
{
    CodexStore store;
    store.unlock(CodexCategory::Faction, "ab");
    const std::vector<std::uint8_t> expected = {'I', 'C', 'C', 'X', 1, 1, 0, 0, 0, 2, 2, 'a', 'b'};
    CHECK(encodeCodex(store) == expected);
}

TEST_CASE("codex rejects unknown versions and bad magic", "[codex_codec]")
{
    const auto bytes = encodeCodex(sample());
    for (std::uint8_t version : {0, 2, 255}) {
        auto changed = bytes;
        changed[4] = version;
        CHECK(decodeCodex(changed).status == CodexDecodeStatus::BadVersion);
    }
    auto magic = bytes;
    magic[0] = 'X';
    CHECK(decodeCodex(magic).status == CodexDecodeStatus::BadMagic);
}

TEST_CASE("every truncation of a codex fails cleanly", "[codex_codec]")
{
    const auto bytes = encodeCodex(sample());
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        const auto r = decodeCodex(std::span<const std::uint8_t>(bytes.data(), n));
        CHECK_FALSE(r.ok());
        CHECK(r.store.size() == 0);
    }
}

TEST_CASE("codex rejects trailing bytes", "[codex_codec]")
{
    auto bytes = encodeCodex(sample());
    bytes.push_back(0);
    CHECK(decodeCodex(bytes).status == CodexDecodeStatus::TrailingBytes);
}

TEST_CASE("codex rejects a huge or unbacked count", "[codex_codec]")
{
    auto bytes = encodeCodex(CodexStore{});
    bytes[5] = bytes[6] = bytes[7] = bytes[8] = 0xFF;
    CHECK(decodeCodex(bytes).status == CodexDecodeStatus::TooMany);
    bytes[5] = 100;
    bytes[6] = bytes[7] = bytes[8] = 0;
    CHECK(decodeCodex(bytes).status == CodexDecodeStatus::TooMany);
}

TEST_CASE("codex rejects duplicates, bad categories and hostile ids", "[codex_codec]")
{
    auto dup = encodeCodex(twoAxioms());
    dup[kSecondEntry + 2] = 'a';
    dup[kSecondEntry + 3] = 'a';
    CHECK(decodeCodex(dup).status == CodexDecodeStatus::InvalidRecord);

    auto category = encodeCodex(twoAxioms());
    category[9] = static_cast<std::uint8_t>(CodexCategory::Count_);
    CHECK(decodeCodex(category).status == CodexDecodeStatus::InvalidRecord);

    auto slash = encodeCodex(twoAxioms());
    slash[kSecondEntry + 2] = '/';
    CHECK(decodeCodex(slash).status == CodexDecodeStatus::InvalidRecord);

    auto control = encodeCodex(twoAxioms());
    control[11] = 0;
    CHECK(decodeCodex(control).status == CodexDecodeStatus::InvalidRecord);

    auto emptyId = encodeCodex(twoAxioms());
    emptyId[10] = 0;
    CHECK_FALSE(decodeCodex(emptyId).ok());
}

TEST_CASE("codex decode honours the limits", "[codex_codec]")
{
    CodexLimits tight;
    tight.maxEntriesPerCategory = 1;
    CHECK(decodeCodex(encodeCodex(twoAxioms()), tight).status == CodexDecodeStatus::InvalidRecord);
    CodexLimits shortIds;
    shortIds.maxIdBytes = 1;
    CHECK(decodeCodex(encodeCodex(twoAxioms()), shortIds).status == CodexDecodeStatus::InvalidRecord);
}

TEST_CASE("codex decode survives corrupted bytes", "[codex_codec]")
{
    const auto base = encodeCodex(sample());
    for (std::size_t i = 0; i < base.size(); ++i) {
        for (int flip : {0x01, 0x80, 0xFF}) {
            auto bytes = base;
            bytes[i] = static_cast<std::uint8_t>(bytes[i] ^ flip);
            const auto r = decodeCodex(bytes);
            if (!r.ok()) {
                CHECK(r.store.size() == 0);
            } else {
                CHECK(encodeCodex(r.store) == bytes);
            }
        }
    }
}
