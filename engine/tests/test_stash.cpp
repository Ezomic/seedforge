#include "ichor/economy/stash.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace ichor;

namespace {

constexpr std::uint32_t kMax32 = std::numeric_limits<std::uint32_t>::max();

StashRegistry withStash(std::uint32_t capacity = 10)
{
    StashRegistry r;
    REQUIRE(r.create(ContainerKind::Stash, "home", "ann", "hearth", capacity) == StashStatus::Ok);
    return r;
}

}

TEST_CASE("a stash holds items up to its capacity", "[stash]")
{
    auto r = withStash(10);
    CHECK(r.deposit("home", "ann", "ore", 6).stored == 6);
    const DepositResult second = r.deposit("home", "ann", "wood", 6);
    CHECK(second.status == StashStatus::Ok);
    CHECK(second.stored == 4);
    CHECK(r.find("home", "ann")->used == 10);
    const DepositResult full = r.deposit("home", "ann", "ore", 1);
    CHECK(full.status == StashStatus::Full);
    CHECK(full.stored == 0);
    CHECK(r.count("home", "ann", "ore") == 6);
    CHECK(r.count("home", "ann", "wood") == 4);
}

TEST_CASE("deposit counts never wrap", "[stash]")
{
    auto r = withStash(kMaxStashCapacity);
    CHECK(r.deposit("home", "ann", "ore", kMaxStashCapacity - 1).stored == kMaxStashCapacity - 1);
    CHECK(r.deposit("home", "ann", "ore", kMax32).stored == 1);
    CHECK(r.deposit("home", "ann", "wood", kMax32).status == StashStatus::Full);
    CHECK(r.find("home", "ann")->used == kMaxStashCapacity);
    CHECK(r.count("home", "ann", "ore") == kMaxStashCapacity);
}

TEST_CASE("a zero deposit is a no-op and a zero withdrawal is allowed", "[stash]")
{
    auto r = withStash();
    const DepositResult none = r.deposit("home", "ann", "ore", 0);
    CHECK(none.status == StashStatus::Ok);
    CHECK(none.stored == 0);
    CHECK(r.find("home", "ann")->items.empty());
    CHECK(r.withdraw("home", "ann", "ore", 0) == StashStatus::Ok);
}

TEST_CASE("withdrawing more than is stored changes nothing", "[stash]")
{
    auto r = withStash();
    (void)r.deposit("home", "ann", "ore", 5);
    CHECK(r.withdraw("home", "ann", "ore", 6) == StashStatus::Insufficient);
    CHECK(r.withdraw("home", "ann", "wood", 1) == StashStatus::Insufficient);
    CHECK(r.withdraw("home", "ann", "ore", kMax32) == StashStatus::Insufficient);
    CHECK(r.count("home", "ann", "ore") == 5);
    CHECK(r.withdraw("home", "ann", "ore", 5) == StashStatus::Ok);
    CHECK(r.find("home", "ann")->items.empty());
    CHECK(r.find("home", "ann")->used == 0);
}

TEST_CASE("only the owner uses a stash", "[stash]")
{
    auto r = withStash();
    (void)r.deposit("home", "ann", "ore", 5);
    CHECK(r.deposit("home", "bob", "ore", 1).status == StashStatus::NotAllowed);
    CHECK(r.withdraw("home", "bob", "ore", 1) == StashStatus::NotAllowed);
    CHECK(r.count("home", "ann", "ore") == 5);
}

TEST_CASE("unknown ids and hostile input are refused", "[stash]")
{
    StashRegistry r;
    CHECK(r.deposit("nope", "ann", "ore", 1).status == StashStatus::UnknownContainer);
    CHECK(r.withdraw("nope", "ann", "ore", 1) == StashStatus::UnknownContainer);
    CHECK(r.moveCart("nope", "ann", "x") == StashStatus::UnknownContainer);
    CHECK(r.markLost("nope") == StashStatus::UnknownContainer);
    CHECK(r.recover("nope") == StashStatus::UnknownContainer);
    CHECK(r.discover("nope") == StashStatus::UnknownContainer);
    CHECK(r.destroy("nope") == StashStatus::UnknownContainer);
    CHECK(r.raid("nope", 500).status == StashStatus::UnknownContainer);
    CHECK(r.find("nope", "ann") == nullptr);
    CHECK(r.count("nope", "ann", "ore") == 0);

    CHECK(r.create(ContainerKind::Stash, "", "ann", "p", 5) == StashStatus::InvalidInput);
    CHECK(r.create(ContainerKind::Stash, "a", "", "p", 5) == StashStatus::InvalidInput);
    CHECK(r.create(ContainerKind::Stash, "a", "ann", "", 5) == StashStatus::InvalidInput);
    CHECK(r.create(ContainerKind::Stash, "a", "ann", "p", 0) == StashStatus::InvalidInput);
    CHECK(r.create(ContainerKind::Stash, "a", "ann", "p", kMaxStashCapacity + 1) == StashStatus::InvalidInput);
    CHECK(r.create(ContainerKind::Stash, std::string(kMaxStashIdLength + 1, 'x'), "ann", "p", 5) == StashStatus::InvalidInput);
    CHECK(r.create(ContainerKind::Stash, "a", std::string(kMaxStashIdLength + 1, 'x'), "p", 5) == StashStatus::InvalidInput);
    CHECK(r.create(ContainerKind::Stash, "a", "ann", std::string(kMaxStashIdLength + 1, 'x'), 5) == StashStatus::InvalidInput);
    CHECK(r.create(ContainerKind::Stash, std::string("a\0b", 3), "ann", "p", 5) == StashStatus::Ok);
    CHECK(r.size() == 1);

    CHECK(r.create(ContainerKind::Stash, "s", "ann", "p", 5) == StashStatus::Ok);
    CHECK(r.create(ContainerKind::Cart, "s", "ann", "p", 5) == StashStatus::Duplicate);
    CHECK(r.deposit("s", "ann", "", 1).status == StashStatus::InvalidInput);
    CHECK(r.deposit("s", "ann", std::string(kMaxStashIdLength + 1, 'x'), 1).status == StashStatus::InvalidInput);
    CHECK(r.withdraw("s", "ann", "", 1) == StashStatus::InvalidInput);
    CHECK(r.find("s", "ann")->used == 0);
}

TEST_CASE("the number of item kinds and containers is bounded", "[stash]")
{
    StashRegistry r;
    REQUIRE(r.create(ContainerKind::Stash, "big", "ann", "p", kMaxStashCapacity) == StashStatus::Ok);
    for (std::size_t i = 0; i < kMaxItemKinds; ++i) {
        CHECK(r.deposit("big", "ann", "item" + std::to_string(i), 1).status == StashStatus::Ok);
    }
    const DepositResult extra = r.deposit("big", "ann", "one-too-many", 1);
    CHECK(extra.status == StashStatus::Full);
    CHECK(extra.stored == 0);
    CHECK(r.deposit("big", "ann", "item0", 1).stored == 1);

    StashRegistry many;
    for (std::size_t i = 0; i < kMaxContainers; ++i) {
        REQUIRE(many.create(ContainerKind::Stash, "c" + std::to_string(i), "ann", "p", 1) == StashStatus::Ok);
    }
    CHECK(many.create(ContainerKind::Stash, "overflow", "ann", "p", 1) == StashStatus::TooMany);
}

TEST_CASE("a cart moves with its owner and a stash does not", "[stash][cart]")
{
    StashRegistry r;
    REQUIRE(r.create(ContainerKind::Cart, "cart", "ann", "gate", 8) == StashStatus::Ok);
    REQUIRE(r.create(ContainerKind::Stash, "home", "ann", "hearth", 8) == StashStatus::Ok);
    CHECK(r.moveCart("cart", "ann", "mill") == StashStatus::Ok);
    CHECK(r.find("cart", "ann")->place == "mill");
    CHECK(r.moveCart("cart", "bob", "bog") == StashStatus::NotAllowed);
    CHECK(r.moveCart("cart", "ann", "") == StashStatus::InvalidInput);
    CHECK(r.find("cart", "ann")->place == "mill");
    CHECK(r.moveCart("home", "ann", "mill") == StashStatus::NotMobile);
    CHECK(r.markLost("home") == StashStatus::NotMobile);
    CHECK(r.find("home", "ann")->place == "hearth");
}

TEST_CASE("a lost cart keeps its contents but cannot be used until recovered", "[stash][cart]")
{
    StashRegistry r;
    REQUIRE(r.create(ContainerKind::Cart, "cart", "ann", "gate", 8) == StashStatus::Ok);
    (void)r.deposit("cart", "ann", "ore", 5);
    CHECK(r.markLost("cart") == StashStatus::Ok);
    CHECK(r.find("cart", "ann")->state == ContainerState::Lost);
    CHECK(r.deposit("cart", "ann", "ore", 1).status == StashStatus::Unavailable);
    CHECK(r.withdraw("cart", "ann", "ore", 1) == StashStatus::Unavailable);
    CHECK(r.moveCart("cart", "ann", "mill") == StashStatus::Unavailable);
    CHECK(r.raid("cart", 500).status == StashStatus::Unavailable);
    CHECK(r.markLost("cart") == StashStatus::Unavailable);
    CHECK(r.count("cart", "ann", "ore") == 5);
    CHECK(r.recover("cart") == StashStatus::Ok);
    CHECK(r.withdraw("cart", "ann", "ore", 5) == StashStatus::Ok);
    CHECK(r.recover("cart") == StashStatus::Unavailable);
}

TEST_CASE("a raid takes a share of every item, rounded down", "[stash][raid]")
{
    StashRegistry r;
    REQUIRE(r.create(ContainerKind::Cart, "cart", "ann", "gate", 100) == StashStatus::Ok);
    (void)r.deposit("cart", "ann", "ore", 10);
    (void)r.deposit("cart", "ann", "wood", 3);
    const RaidResult hit = r.raid("cart", 500);
    CHECK(hit.status == StashStatus::Ok);
    CHECK(hit.taken.at("ore") == 5);
    CHECK(hit.taken.at("wood") == 1);
    CHECK(r.count("cart", "ann", "ore") == 5);
    CHECK(r.count("cart", "ann", "wood") == 2);
    CHECK(r.find("cart", "ann")->used == 7);
    CHECK(r.find("cart", "ann")->raids == 1);

    const RaidResult small = r.raid("cart", 100);
    CHECK(small.taken.empty());
    CHECK(r.find("cart", "ann")->used == 7);
}

TEST_CASE("a full raid empties the container but leaves it usable", "[stash][raid]")
{
    StashRegistry r;
    REQUIRE(r.create(ContainerKind::Cart, "cart", "ann", "gate", 100) == StashStatus::Ok);
    (void)r.deposit("cart", "ann", "ore", 100);
    const RaidResult all = r.raid("cart", kPermilleAll);
    CHECK(all.taken.at("ore") == 100);
    CHECK(r.find("cart", "ann")->used == 0);
    CHECK(r.find("cart", "ann")->items.empty());
    CHECK(r.deposit("cart", "ann", "ore", 3).stored == 3);
}

TEST_CASE("a raid over 1000 permille is refused and large counts do not overflow", "[stash][raid]")
{
    StashRegistry r;
    REQUIRE(r.create(ContainerKind::Stash, "home", "ann", "hearth", kMaxStashCapacity) == StashStatus::Ok);
    (void)r.deposit("home", "ann", "ore", kMaxStashCapacity);
    CHECK(r.raid("home", kPermilleAll + 1).status == StashStatus::InvalidInput);
    CHECK(r.raid("home", kMax32).status == StashStatus::InvalidInput);
    CHECK(r.count("home", "ann", "ore") == kMaxStashCapacity);
    const RaidResult half = r.raid("home", 500);
    CHECK(half.taken.at("ore") == kMaxStashCapacity / 2);
    CHECK(r.count("home", "ann", "ore") == kMaxStashCapacity - kMaxStashCapacity / 2);
}

TEST_CASE("a destroyed container loses everything and refuses all use", "[stash][destroy]")
{
    StashRegistry r;
    REQUIRE(r.create(ContainerKind::Cart, "cart", "ann", "gate", 8) == StashStatus::Ok);
    (void)r.deposit("cart", "ann", "ore", 5);
    CHECK(r.destroy("cart") == StashStatus::Ok);
    const StashContainer* c = r.find("cart", "ann");
    REQUIRE(c != nullptr);
    CHECK(c->state == ContainerState::Destroyed);
    CHECK(c->items.empty());
    CHECK(c->used == 0);
    CHECK(r.deposit("cart", "ann", "ore", 1).status == StashStatus::Unavailable);
    CHECK(r.withdraw("cart", "ann", "ore", 0) == StashStatus::Unavailable);
    CHECK(r.raid("cart", 100).status == StashStatus::Unavailable);
    CHECK(r.markLost("cart") == StashStatus::Unavailable);
    CHECK(r.recover("cart") == StashStatus::Unavailable);
    CHECK(r.destroy("cart") == StashStatus::Unavailable);
    CHECK(r.moveCart("cart", "ann", "x") == StashStatus::Unavailable);
}

TEST_CASE("a hidden cache is invisible until discovered", "[stash][cache]")
{
    StashRegistry r;
    REQUIRE(r.create(ContainerKind::Cache, "hole", "ann", "bog", 10) == StashStatus::Ok);
    (void)r.deposit("hole", "ann", "ore", 4);
    CHECK(r.find("hole", "ann") != nullptr);
    CHECK(r.find("hole", "bob") == nullptr);
    CHECK(r.visibleTo("bob").empty());
    CHECK(r.visibleTo("ann") == std::vector<std::string>{"hole"});
    CHECK(r.count("hole", "bob", "ore") == 0);

    CHECK(r.withdraw("hole", "bob", "ore", 1) == StashStatus::UnknownContainer);
    CHECK(r.deposit("hole", "bob", "ore", 1).status == StashStatus::UnknownContainer);
    CHECK(r.raid("hole", 500).status == StashStatus::UnknownContainer);
    CHECK(r.count("hole", "ann", "ore") == 4);

    CHECK(r.discover("hole") == StashStatus::Ok);
    CHECK(r.find("hole", "bob") != nullptr);
    CHECK(r.visibleTo("bob") == std::vector<std::string>{"hole"});
    CHECK(r.withdraw("hole", "bob", "ore", 3) == StashStatus::Ok);
    CHECK(r.withdraw("hole", "bob", "ore", 3) == StashStatus::Insufficient);
    CHECK(r.deposit("hole", "bob", "ore", 1).status == StashStatus::NotAllowed);
    CHECK(r.raid("hole", 1000).status == StashStatus::Ok);
}

TEST_CASE("only a cache can be discovered", "[stash][cache]")
{
    auto r = withStash();
    CHECK(r.discover("home") == StashStatus::NotAllowed);
    CHECK(r.find("home", "bob") != nullptr);
}

TEST_CASE("a destroyed hidden cache stays hidden", "[stash][cache]")
{
    StashRegistry r;
    REQUIRE(r.create(ContainerKind::Cache, "hole", "ann", "bog", 10) == StashStatus::Ok);
    CHECK(r.destroy("hole") == StashStatus::Ok);
    CHECK(r.find("hole", "bob") == nullptr);
    CHECK(r.find("hole", "ann")->state == ContainerState::Destroyed);
}

TEST_CASE("two registries given the same calls end identical", "[stash]")
{
    auto build = [] {
        StashRegistry r;
        (void)r.create(ContainerKind::Cart, "b", "ann", "x", 20);
        (void)r.create(ContainerKind::Stash, "a", "ann", "y", 20);
        (void)r.deposit("b", "ann", "zinc", 7);
        (void)r.deposit("b", "ann", "ore", 9);
        (void)r.raid("b", 333);
        return r;
    };
    const StashRegistry x = build();
    const StashRegistry y = build();
    CHECK(x.find("b", "ann")->items == y.find("b", "ann")->items);
    CHECK(x.find("b", "ann")->items.at("ore") == 7);
    CHECK(x.find("b", "ann")->items.at("zinc") == 5);
    CHECK(x.visibleTo("ann") == std::vector<std::string>{"a", "b"});
}

TEST_CASE("a zero raid and an out of range kind do nothing", "[stash][raid]")
{
    StashRegistry r;
    CHECK(r.create(static_cast<ContainerKind>(7), "x", "ann", "p", 5) == StashStatus::InvalidInput);
    REQUIRE(r.create(ContainerKind::Cart, "cart", "ann", "gate", 5) == StashStatus::Ok);
    (void)r.deposit("cart", "ann", "ore", 4);
    const RaidResult none = r.raid("cart", 0);
    CHECK(none.status == StashStatus::Ok);
    CHECK(none.taken.empty());
    CHECK(r.find("cart", "ann")->raids == 0);
    CHECK(r.count("cart", "ann", "ore") == 4);
}
