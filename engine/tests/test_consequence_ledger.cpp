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
    ledger.append(fact("f1", "saltmarsh", "burned", "true", 10));
    ledger.append(fact("f2", "ilse", "betrayed", "harl", 20));
    ledger.append(fact("f3", "saltmarsh", "tithe_paid", "40", 30));
    ledger.append(fact("f4", "ilse", "tithe_paid", "5", 40));
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
        ledger.append(fact("f1", "saltmarsh", "burned", "true", 10));
        ledger.append(changed);
        return ledger.digest();
    };
    const std::uint64_t reference = digestWith(fact("f2", "ilse", "betrayed", "harl", 20));
    CHECK(digestWith(fact("f9", "ilse", "betrayed", "harl", 20)) != reference);
    CHECK(digestWith(fact("f2", "harl", "betrayed", "harl", 20)) != reference);
    CHECK(digestWith(fact("f2", "ilse", "freed", "harl", 20)) != reference);
    CHECK(digestWith(fact("f2", "ilse", "betrayed", "maren", 20)) != reference);
    CHECK(digestWith(fact("f2", "ilse", "betrayed", "harl", 21)) != reference);

    ConsequenceLedger swapped;
    swapped.append(fact("f2", "ilse", "betrayed", "harl", 20));
    swapped.append(fact("f1", "saltmarsh", "burned", "true", 10));
    ConsequenceLedger ordered;
    ordered.append(fact("f1", "saltmarsh", "burned", "true", 10));
    ordered.append(fact("f2", "ilse", "betrayed", "harl", 20));
    CHECK(swapped.digest() != ordered.digest());
}

TEST_CASE("the digest does not blur field boundaries", "[consequence_ledger]")
{
    ConsequenceLedger a;
    a.append(fact("f1", "ab", "c"));
    ConsequenceLedger b;
    b.append(fact("f1", "a", "bc"));
    CHECK(a.digest() != b.digest());
}

TEST_CASE("the digest of a known ledger is pinned", "[consequence_ledger]")
{
    // Changing this number changes every saved ledger's identity on every platform.
    CHECK(sample().digest() == 0xca3febb71e47d1c4ull);
}

TEST_CASE("serialize and parse round trip", "[consequence_ledger]")
{
    const ConsequenceLedger ledger = sample();
    const auto bytes = ledger.serialize();
    ConsequenceLedger back;
    REQUIRE(ConsequenceLedger::parse(bytes, back));
    CHECK(back.facts() == ledger.facts());
    CHECK(back.digest() == ledger.digest());
    CHECK(back.serialize() == bytes);
    REQUIRE(back.find("f3") != nullptr);
    CHECK(back.bySubject("ilse").size() == 2);
    CHECK(back.byKind("tithe_paid").size() == 2);

    ConsequenceLedger empty;
    ConsequenceLedger emptyBack = sample();
    REQUIRE(ConsequenceLedger::parse(empty.serialize(), emptyBack));
    CHECK(emptyBack.empty());
}

TEST_CASE("parse is all or nothing", "[consequence_ledger]")
{
    const auto bytes = sample().serialize();

    ConsequenceLedger target;
    target.append(fact("keep", "x", "y"));
    const std::uint64_t kept = target.digest();

    SECTION("every truncation fails")
    {
        for (std::size_t n = 0; n < bytes.size(); ++n) {
            const std::vector<std::uint8_t> cut(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(n));
            CHECK_FALSE(ConsequenceLedger::parse(cut, target));
        }
    }
    SECTION("trailing bytes fail")
    {
        auto longer = bytes;
        longer.push_back(0);
        CHECK_FALSE(ConsequenceLedger::parse(longer, target));
    }
    SECTION("wrong magic or version fails")
    {
        auto bad = bytes;
        bad[0] ^= 0xFF;
        CHECK_FALSE(ConsequenceLedger::parse(bad, target));
        bad = bytes;
        bad[4] = 2;
        CHECK_FALSE(ConsequenceLedger::parse(bad, target));
    }
    SECTION("a duplicate id or invalid field in the bytes fails")
    {
        Fact twin = fact("f1", "a", "b");
        Fact upper = fact("F2", "a", "b");
        for (const Fact& bad : {twin, upper}) {
            BinaryWriter writer;
            writer.write(std::uint32_t{0x4743444Cu});
            writer.write(std::uint32_t{1});
            writer.write(std::vector<Fact>{fact("f1", "a", "b"), bad});
            CHECK_FALSE(ConsequenceLedger::parse(writer.bytes(), target));
        }
    }
    CHECK(target.digest() == kept);
    CHECK(target.size() == 1);
}
