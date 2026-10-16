#include "ichor/save/codex_store.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace ichor;

TEST_CASE("codex unlocks once", "[codex]")
{
    CodexStore store;
    CHECK(store.unlock(CodexCategory::Axiom, "salt-age") == CodexResult::Unlocked);
    CHECK(store.unlock(CodexCategory::Axiom, "salt-age") == CodexResult::AlreadyKnown);
    CHECK(store.size() == 1);
    CHECK(store.has(CodexCategory::Axiom, "salt-age"));
    CHECK(store.count(CodexCategory::Axiom) == 1);
}

TEST_CASE("codex ids are scoped by category", "[codex]")
{
    CodexStore store;
    CHECK(store.unlock(CodexCategory::Axiom, "x") == CodexResult::Unlocked);
    CHECK(store.unlock(CodexCategory::Faction, "x") == CodexResult::Unlocked);
    CHECK_FALSE(store.has(CodexCategory::Synergy, "x"));
    CHECK(store.size() == 2);
}

TEST_CASE("codex keeps unlock order", "[codex]")
{
    CodexStore store;
    store.unlock(CodexCategory::Synergy, "b");
    store.unlock(CodexCategory::Axiom, "a");
    REQUIRE(store.all().size() == 2);
    CHECK(store.all()[0].id == "b");
    CHECK(store.all()[1].id == "a");
}

TEST_CASE("codex caps entries per category", "[codex]")
{
    CodexLimits limits;
    limits.maxEntriesPerCategory = 3;
    CodexStore store(limits);
    for (int i = 0; i < 3; ++i) {
        REQUIRE(store.unlock(CodexCategory::World, "w" + std::to_string(i)) == CodexResult::Unlocked);
    }
    CHECK(store.unlock(CodexCategory::World, "w3") == CodexResult::TooMany);
    CHECK(store.size() == 3);
    CHECK(store.unlock(CodexCategory::World, "w0") == CodexResult::AlreadyKnown);
    CHECK(store.unlock(CodexCategory::Seed, "s0") == CodexResult::Unlocked);
}

TEST_CASE("codex rejects hostile ids and categories", "[codex]")
{
    CodexStore store;
    CHECK(store.unlock(CodexCategory::Axiom, "") == CodexResult::InvalidId);
    CHECK(store.unlock(CodexCategory::Axiom, "has space") == CodexResult::InvalidId);
    CHECK(store.unlock(CodexCategory::Axiom, "a/b") == CodexResult::InvalidId);
    CHECK(store.unlock(CodexCategory::Axiom, "a\\b") == CodexResult::InvalidId);
    CHECK(store.unlock(CodexCategory::Axiom, std::string("a\0b", 3)) == CodexResult::InvalidId);
    CHECK(store.unlock(CodexCategory::Axiom, "caf\xC3\xA9") == CodexResult::InvalidId);
    CHECK(store.unlock(CodexCategory::Axiom, "line\nbreak") == CodexResult::InvalidId);
    CHECK(store.unlock(CodexCategory::Axiom, std::string(65, 'a')) == CodexResult::InvalidId);
    CHECK(store.unlock(CodexCategory::Axiom, std::string(64, 'a')) == CodexResult::Unlocked);
    CHECK(store.unlock(CodexCategory::Count_, "ok") == CodexResult::InvalidCategory);
    CHECK(store.unlock(static_cast<CodexCategory>(200), "ok") == CodexResult::InvalidCategory);
    CHECK(store.size() == 1);
}

TEST_CASE("codex has and count tolerate bad input", "[codex]")
{
    CodexStore store;
    CHECK_FALSE(store.has(CodexCategory::Axiom, ""));
    CHECK_FALSE(store.has(static_cast<CodexCategory>(200), "a"));
    CHECK(store.count(static_cast<CodexCategory>(200)) == 0);
}
