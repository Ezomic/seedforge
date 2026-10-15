#include "ichor/ledger/consequence_ledger.h"

#include "ichor/core/binary.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace ichor;

namespace {

Fact fact(std::string id, std::string subject, std::string kind, std::string value = "1", std::uint64_t tick = 0)
{
    return Fact{std::move(id), std::move(subject), std::move(kind), std::move(value), tick};
}

ConsequenceLedger sample()
{
    ConsequenceLedger ledger;
    REQUIRE(ledger.append(fact("f1", "saltmarsh", "burned", "true", 10)) == AppendResult::Ok);
    REQUIRE(ledger.append(fact("f2", "ilse", "betrayed", "harl", 20)) == AppendResult::Ok);
    REQUIRE(ledger.append(fact("f3", "saltmarsh", "tithe_paid", "40", 30)) == AppendResult::Ok);
    REQUIRE(ledger.append(fact("f4", "ilse", "tithe_paid", "5", 40)) == AppendResult::Ok);
    return ledger;
}

}

TEST_CASE("append keeps facts in order and find returns them by id", "[consequence_ledger]")
{
    const ConsequenceLedger ledger = sample();
    REQUIRE(ledger.size() == 4);
    CHECK(ledger.facts()[0].id == "f1");
    CHECK(ledger.facts()[3].id == "f4");
    REQUIRE(ledger.find("f2") != nullptr);
    CHECK(ledger.find("f2")->value == "harl");
    CHECK(ledger.find("nope") == nullptr);
}

TEST_CASE("append refuses bad fields and duplicate ids without changing the ledger", "[consequence_ledger]")
{
    ConsequenceLedger ledger = sample();
    const std::uint64_t before = ledger.digest();
    CHECK(ledger.append(fact("f1", "x", "y")) == AppendResult::DuplicateId);
    CHECK(ledger.append(fact("", "x", "y")) == AppendResult::InvalidField);
    CHECK(ledger.append(fact("Upper", "x", "y")) == AppendResult::InvalidField);
    CHECK(ledger.append(fact("a b", "x", "y")) == AppendResult::InvalidField);
    CHECK(ledger.append(fact("g", "", "y")) == AppendResult::InvalidField);
    CHECK(ledger.append(fact("g", "x", "")) == AppendResult::InvalidField);
    CHECK(ledger.append(fact(std::string(ConsequenceLedger::kMaxKeyBytes + 1, 'a'), "x", "y")) == AppendResult::InvalidField);
    CHECK(ledger.append(fact("g", "x", "y", std::string(ConsequenceLedger::kMaxValueBytes + 1, 'v'))) == AppendResult::InvalidField);
    CHECK(ledger.size() == 4);
    CHECK(ledger.digest() == before);
    CHECK(ledger.find("g") == nullptr);
    CHECK(ledger.bySubject("x").empty());
}

TEST_CASE("lookups by subject and kind return facts in append order", "[consequence_ledger]")
{
    const ConsequenceLedger ledger = sample();
    const auto saltmarsh = ledger.bySubject("saltmarsh");
    REQUIRE(saltmarsh.size() == 2);
    CHECK(saltmarsh[0]->id == "f1");
    CHECK(saltmarsh[1]->id == "f3");

    const auto tithes = ledger.byKind("tithe_paid");
    REQUIRE(tithes.size() == 2);
    CHECK(tithes[0]->id == "f3");
    CHECK(tithes[1]->id == "f4");

    const auto both = ledger.bySubjectAndKind("ilse", "tithe_paid");
    REQUIRE(both.size() == 1);
    CHECK(both[0]->id == "f4");

    CHECK(ledger.bySubject("nobody").empty());
    CHECK(ledger.byKind("nothing").empty());
    CHECK(ledger.bySubjectAndKind("ilse", "burned").empty());
}

TEST_CASE("the digest depends on every field and on order", "[consequence_ledger]")
{
    const std::uint64_t base = sample().digest();
    CHECK(sample().digest() == base);
    CHECK(ConsequenceLedger{}.digest() != base);

    const auto digestWith = [](const Fact& changed) {
        ConsequenceLedger ledger;
        REQUIRE(ledger.append(fact("f1", "saltmarsh", "burned", "true", 10)) == AppendResult::Ok);
        REQUIRE(ledger.append(changed) == AppendResult::Ok);
        return ledger.digest();
    };
    const std::uint64_t reference = digestWith(fact("f2", "ilse", "betrayed", "harl", 20));
    CHECK(digestWith(fact("f9", "ilse", "betrayed", "harl", 20)) != reference);
    CHECK(digestWith(fact("f2", "harl", "betrayed", "harl", 20)) != reference);
    CHECK(digestWith(fact("f2", "ilse", "freed", "harl", 20)) != reference);
    CHECK(digestWith(fact("f2", "ilse", "betrayed", "maren", 20)) != reference);
    CHECK(digestWith(fact("f2", "ilse", "betrayed", "harl", 21)) != reference);

    ConsequenceLedger swapped;
    REQUIRE(swapped.append(fact("f2", "ilse", "betrayed", "harl", 20)) == AppendResult::Ok);
    REQUIRE(swapped.append(fact("f1", "saltmarsh", "burned", "true", 10)) == AppendResult::Ok);
    ConsequenceLedger ordered;
    REQUIRE(ordered.append(fact("f1", "saltmarsh", "burned", "true", 10)) == AppendResult::Ok);
    REQUIRE(ordered.append(fact("f2", "ilse", "betrayed", "harl", 20)) == AppendResult::Ok);
    CHECK(swapped.digest() != ordered.digest());
}

TEST_CASE("the digest does not blur field boundaries", "[consequence_ledger]")
{
    ConsequenceLedger a;
    REQUIRE(a.append(fact("f1", "ab", "c")) == AppendResult::Ok);
    ConsequenceLedger b;
    REQUIRE(b.append(fact("f1", "a", "bc")) == AppendResult::Ok);
    CHECK(a.digest() != b.digest());
}

TEST_CASE("the digest of a known ledger is pinned", "[consequence_ledger]")
{
    // Changing this number changes every saved ledger's identity on every platform.
    CHECK(sample().digest() == 0xca3febb71e47d1c4ull);
}

TEST_CASE("serialize and parse round trip", "[consequence_ledger]")
{
    ConsequenceLedger ledger = sample();
    std::string value(ConsequenceLedger::kMaxValueBytes, '\xff');
    value[3] = '\0';
    REQUIRE(ledger.append(fact("edge", std::string(ConsequenceLedger::kMaxKeyBytes, 'k'), "big", value, UINT64_MAX)) == AppendResult::Ok);

    const auto bytes = ledger.serialize();
    const auto back = ConsequenceLedger::parse(bytes);
    REQUIRE(back.has_value());
    CHECK(back->facts() == ledger.facts());
    CHECK(back->digest() == ledger.digest());
    CHECK(back->serialize() == bytes);
    REQUIRE(back->find("f3") != nullptr);
    CHECK(back->bySubject("ilse").size() == 2);
    CHECK(back->byKind("tithe_paid").size() == 2);

    const auto empty = ConsequenceLedger::parse(ConsequenceLedger{}.serialize());
    REQUIRE(empty.has_value());
    CHECK(empty->empty());
}

namespace {

std::vector<std::uint8_t> header(std::uint32_t count)
{
    BinaryWriter writer;
    writer.write(std::uint32_t{0x4743444Cu});
    writer.write(std::uint32_t{1});
    writer.write(count);
    return writer.take();
}

}

TEST_CASE("parse is all or nothing", "[consequence_ledger]")
{
    const auto bytes = sample().serialize();

    SECTION("every truncation fails")
    {
        for (std::size_t n = 0; n < bytes.size(); ++n) {
            const std::vector<std::uint8_t> cut(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(n));
            CHECK_FALSE(ConsequenceLedger::parse(cut).has_value());
        }
    }
    SECTION("trailing bytes fail")
    {
        auto longer = bytes;
        longer.push_back(0);
        CHECK_FALSE(ConsequenceLedger::parse(longer).has_value());
    }
    SECTION("wrong magic or version fails")
    {
        auto bad = bytes;
        bad[0] ^= 0xFF;
        CHECK_FALSE(ConsequenceLedger::parse(bad).has_value());
        bad = bytes;
        bad[4] = 2;
        CHECK_FALSE(ConsequenceLedger::parse(bad).has_value());
    }
    SECTION("a duplicate id or invalid field in the bytes fails")
    {
        for (const Fact& bad : {fact("f1", "a", "b"), fact("F2", "a", "b")}) {
            BinaryWriter writer;
            writer.write(std::uint32_t{0x4743444Cu});
            writer.write(std::uint32_t{1});
            writer.write(std::vector<Fact>{fact("f1", "a", "b"), bad});
            CHECK_FALSE(ConsequenceLedger::parse(writer.bytes()).has_value());
        }
    }
}

TEST_CASE("parse refuses a count the bytes cannot hold before allocating for it", "[consequence_ledger]")
{
    CHECK_FALSE(ConsequenceLedger::parse(header(0xFFFFFFFFu)).has_value());
    CHECK_FALSE(ConsequenceLedger::parse(header(1)).has_value());

    auto tooMany = header(static_cast<std::uint32_t>(ConsequenceLedger::kMaxFacts + 1));
    tooMany.resize(tooMany.size() + (ConsequenceLedger::kMaxFacts + 1) * ConsequenceLedger::kMinFactBytes);
    CHECK_FALSE(ConsequenceLedger::parse(tooMany).has_value());
}

TEST_CASE("a fact takes at least kMinFactBytes in the saved form", "[consequence_ledger]")
{
    BinaryWriter writer;
    writer.write(Fact{});
    CHECK(writer.bytes().size() == ConsequenceLedger::kMinFactBytes);
}

TEST_CASE("append stops at kMaxFacts", "[consequence_ledger]")
{
    ConsequenceLedger ledger;
    for (std::size_t i = 0; i < ConsequenceLedger::kMaxFacts; ++i) {
        REQUIRE(ledger.append(fact("f" + std::to_string(i), "s", "k", "", i)) == AppendResult::Ok);
    }
    CHECK(ledger.append(fact("one_more", "s", "k")) == AppendResult::Full);
    CHECK(ledger.size() == ConsequenceLedger::kMaxFacts);
    CHECK(ledger.find("one_more") == nullptr);
}
