#include "ichor/ledger/ledger_codec_format.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace ichor;

namespace {

ConsequenceLedger bigLedger(int n)
{
    ConsequenceLedger ledger;
    const char* kinds[] = {"burned", "betrayed", "tithe_paid", "freed", "taxed"};
    for (int i = 0; i < n; ++i) {
        Fact f{"fact:" + std::to_string(i), "place:" + std::to_string(i % 200), kinds[i % 5],
            (i % 3 == 0) ? "true" : std::to_string(i % 50), static_cast<std::uint64_t>(i) * 7};
        REQUIRE(ledger.append(std::move(f)) == AppendResult::Ok);
    }
    return ledger;
}

}

TEST_CASE("ledger codec round-trips an empty ledger", "[ledger_codec]")
{
    const ConsequenceLedger empty;
    const auto bytes = encodeLedger(empty);
    const auto back = decodeLedger(bytes);
    REQUIRE(back.has_value());
    CHECK(back->empty());
    CHECK(back->digest() == empty.digest());
}

TEST_CASE("ledger codec round-trips 100000 facts", "[ledger_codec]")
{
    const ConsequenceLedger ledger = bigLedger(100000);
    const auto back = decodeLedger(encodeLedger(ledger));
    REQUIRE(back.has_value());
    CHECK(back->size() == 100000);
    CHECK(back->digest() == ledger.digest());
    CHECK(back->facts() == ledger.facts());
}

TEST_CASE("ledger codec keeps out-of-order and extreme ticks", "[ledger_codec]")
{
    ConsequenceLedger ledger;
    const std::uint64_t ticks[] = {500, 3, 0, ~0ull, 1, ~0ull / 2};
    int i = 0;
    for (std::uint64_t t : ticks) {
        REQUIRE(ledger.append(Fact{"f" + std::to_string(i++), "s", "k", "", t}) == AppendResult::Ok);
    }
    const auto back = decodeLedger(encodeLedger(ledger));
    REQUIRE(back.has_value());
    CHECK(back->facts() == ledger.facts());
}

TEST_CASE("ledger codec output is byte-identical on repeat", "[ledger_codec]")
{
    const ConsequenceLedger ledger = bigLedger(5000);
    CHECK(encodeLedger(ledger) == encodeLedger(ledger));
    const auto back = decodeLedger(encodeLedger(ledger));
    REQUIRE(back.has_value());
    CHECK(encodeLedger(*back) == encodeLedger(ledger));
}

TEST_CASE("ledger codec is far smaller than the naive encoding", "[ledger_codec]")
{
    const ConsequenceLedger ledger = bigLedger(100000);
    const double naive = static_cast<double>(ledger.serialize().size());
    const double packed = static_cast<double>(encodeLedger(ledger).size());
    CHECK(packed < naive * 0.6);
}

TEST_CASE("ledger codec rejects an unknown version and empty input", "[ledger_codec]")
{
    auto bytes = encodeLedger(bigLedger(3));
    CHECK_FALSE(decodeLedger({}).has_value());
    bytes[0] = 0;
    CHECK_FALSE(decodeLedger(bytes).has_value());
    bytes[0] = kLedgerCodecVersion + 1;
    CHECK_FALSE(decodeLedger(bytes).has_value());
}

TEST_CASE("ledger codec rejects every truncation", "[ledger_codec]")
{
    const auto bytes = encodeLedger(bigLedger(20));
    for (std::size_t len = 0; len < bytes.size(); ++len) {
        CHECK_FALSE(decodeLedger(std::span(bytes.data(), len)).has_value());
    }
}

TEST_CASE("ledger codec rejects trailing bytes", "[ledger_codec]")
{
    auto bytes = encodeLedger(bigLedger(5));
    bytes.push_back(0);
    CHECK_FALSE(decodeLedger(bytes).has_value());
}

TEST_CASE("ledger codec survives every bit flip and never decodes to a different valid ledger silently", "[ledger_codec]")
{
    const ConsequenceLedger ledger = bigLedger(30);
    const auto bytes = encodeLedger(ledger);
    std::size_t rejected = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        for (int bit = 0; bit < 8; ++bit) {
            auto copy = bytes;
            copy[i] ^= static_cast<std::uint8_t>(1u << bit);
            const auto back = decodeLedger(copy);
            if (!back) {
                ++rejected;
                continue;
            }
            CHECK(back->size() <= ConsequenceLedger::kMaxFacts);
            CHECK(back->digest() != ledger.digest());
        }
    }
    // Version, counts, lengths and indices are structural, so many flips must be refused outright.
    CHECK(rejected > bytes.size() / 2);
}

TEST_CASE("ledger codec rejects hostile counts cheaply", "[ledger_codec]")
{
    const std::uint8_t hugeStrings[] = {kLedgerCodecVersion, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F};
    CHECK_FALSE(decodeLedger(hugeStrings).has_value());

    const std::uint8_t hugeFacts[] = {kLedgerCodecVersion, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F};
    CHECK_FALSE(decodeLedger(hugeFacts).has_value());

    const std::uint8_t tenByteVarint[] = {kLedgerCodecVersion, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F};
    CHECK_FALSE(decodeLedger(tenByteVarint).has_value());

    const std::uint8_t overlongVarint[] = {kLedgerCodecVersion, 0x80, 0x00};
    CHECK_FALSE(decodeLedger(overlongVarint).has_value());

    const std::uint8_t hugeString[] = {kLedgerCodecVersion, 0x01, 0x80, 0x80, 0x80, 0x80, 0x10, 'a'};
    CHECK_FALSE(decodeLedger(hugeString).has_value());
}

TEST_CASE("ledger codec rejects bad indices and duplicate ids", "[ledger_codec]")
{
    const std::uint8_t badIndex[] = {kLedgerCodecVersion, 1, 1, 'a', 1, 1, 'f', 5, 0, 0, 0};
    CHECK_FALSE(decodeLedger(badIndex).has_value());

    const std::uint8_t twoSame[] = {kLedgerCodecVersion, 1, 1, 'a', 2, 1, 'f', 0, 0, 0, 0, 1, 'f', 0, 0, 0, 0};
    CHECK_FALSE(decodeLedger(twoSame).has_value());

    const std::uint8_t ok[] = {kLedgerCodecVersion, 1, 1, 'a', 1, 1, 'f', 0, 0, 0, 0};
    CHECK(decodeLedger(ok).has_value());
}
