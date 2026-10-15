#include "ichor/progression/capabilities.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace ichor;

namespace {

std::string entry(const std::string& id, const std::string& needs = "")
{
    std::string text = R"({"id": ")" + id + R"(", "name": "Name )" + id + R"(", "description": "Does )" + id + R"("})";
    if (!needs.empty()) {
        text.pop_back();
        text += R"(, "requires": [)" + needs + "]}";
    }
    return text;
}

std::vector<std::string> parseProblems(const std::string& text, std::vector<Capability>* out = nullptr)
{
    std::vector<Capability> local;
    std::vector<std::string> problems;
    const bool ok = parseCapabilities("c.json", text, out != nullptr ? *out : local, problems);
    CHECK(ok == problems.empty());
    return problems;
}

bool mentions(const std::vector<std::string>& problems, const std::string& needle)
{
    return std::any_of(problems.begin(), problems.end(), [&](const std::string& p) { return p.find(needle) != std::string::npos; });
}

std::vector<Capability> chain()
{
    std::vector<Capability> table;
    REQUIRE(parseProblems("[" + entry("a") + "," + entry("b", R"("a")") + "," + entry("c", R"("a", "b")") + "," + entry("d") + "]", &table).empty());
    return table;
}

std::string authoredText()
{
    std::ifstream file(std::filesystem::path(ICHOR_GAME_DATA_DIR) / "progression" / "capabilities.json", std::ios::binary);
    REQUIRE(file.good());
    return std::string(std::istreambuf_iterator<char>(file), {});
}

}

TEST_CASE("the authored capability table parses")
{
    std::vector<Capability> table;
    CHECK(parseProblems(authoredText(), &table).empty());
    CHECK(table.size() == 8);
}

TEST_CASE("a table with prerequisites parses")
{
    const auto table = chain();
    REQUIRE(table.size() == 4);
    CHECK(table[2].prerequisites == std::vector<std::string>{"a", "b"});
    CHECK(table[0].prerequisites.empty());
}

TEST_CASE("bad table data is reported with a path")
{
    CHECK(mentions(parseProblems(""), "empty file"));
    CHECK(mentions(parseProblems("{"), "not valid JSON"));
    CHECK(mentions(parseProblems("{}"), "expected an array"));
    CHECK(mentions(parseProblems("[1]"), "[0]: expected an object"));
    CHECK(mentions(parseProblems(R"([{"name": "x", "description": "y"}])"), "[0].id: missing"));
    CHECK(mentions(parseProblems(R"([{"id": "Bad Id", "name": "x", "description": "y"}])"), "[0].id"));
    CHECK(mentions(parseProblems(R"([{"id": "a", "description": "y"}])"), "[0].name: missing"));
    CHECK(mentions(parseProblems(R"([{"id": "a", "name": "x"}])"), "[0].description: missing"));
    CHECK(mentions(parseProblems("[" + entry("a") + "," + entry("a") + "]"), "[1].id: duplicate"));
    CHECK(mentions(parseProblems(R"([{"id": "a", "id": "b", "name": "x", "description": "y"}])"), "duplicate key"));
    CHECK(mentions(parseProblems(R"([{"id": "a", "name": "x—y","description": "y"}])"), "em-dash"));
    CHECK(mentions(parseProblems(R"([{"id": "a", "name": "x", "description": "y", "requires": "b"}])"), "[0].requires: expected an array"));
    CHECK(mentions(parseProblems(R"([{"id": "a", "name": "x", "description": "y", "requires": [1]}])"), "[0].requires[0]: expected a string"));
}

TEST_CASE("input limits are enforced")
{
    const auto one = [](const std::string& field, const std::string& value) {
        return "[{\"id\": \"a\", \"name\": \"x\", \"description\": \"y\", \"" + field + "\": " + value + "}]";
    };
    CHECK(mentions(parseProblems(std::string(1024 * 1024 + 1, ' ')), "larger than"));
    CHECK(mentions(parseProblems(std::string(20, '[') + std::string(20, ']')), "nested deeper"));

    std::string many = "[";
    for (int i = 0; i < 257; ++i) {
        many += (i == 0 ? "" : ",") + entry("c" + std::to_string(i));
    }
    CHECK(mentions(parseProblems(many + "]"), "more than 256"));

    CHECK(mentions(parseProblems("[" + entry(std::string(65, 'a')) + "]"), "[0].id: longer than"));
    CHECK(mentions(parseProblems(R"([{"id": 5, "name": "x", "description": "y"}])"), "[0].id: expected a string"));
    CHECK(mentions(parseProblems(R"([{"id": "a", "name": "  ", "description": "y"}])"), "[0].name: must not be empty"));
    CHECK(mentions(parseProblems(R"([{"id": "a", "name": ")" + std::string(121, 'x') + R"(", "description": "y"}])"), "[0].name: longer than"));
    CHECK(mentions(parseProblems(R"([{"id": "a", "name": "x", "description": ")" + std::string(401, 'x') + R"("}])"), "[0].description: longer than"));

    std::string required;
    for (int i = 0; i < 17; ++i) {
        required += (i == 0 ? "" : ",") + std::string("\"p") + std::to_string(i) + "\"";
    }
    CHECK(mentions(parseProblems(one("requires", "[" + required + "]")), "more than 16 prerequisites"));
}

TEST_CASE("a prerequisite listed twice is refused")
{
    CHECK(mentions(parseProblems("[" + entry("a") + "," + entry("b", R"("a", "a")") + "]"), "[1].requires[1]: duplicate"));
}

TEST_CASE("a bad entry and a cycle are both reported")
{
    const auto problems = parseProblems(R"([{"id": "Bad", "name": "x", "description": "y"}, )" + entry("a", R"("b")") + "," + entry("b", R"("a")") + "]");
    CHECK(mentions(problems, "[0].id"));
    CHECK(mentions(problems, "prerequisite cycle: a -> b -> a"));
}

TEST_CASE("a capability carries no numeric bonus")
{
    CHECK(mentions(parseProblems(R"([{"id": "a", "name": "x", "description": "y", "damage_bonus": 5}])"), "[0].damage_bonus: unknown key"));
    CHECK(mentions(parseProblems(R"([{"id": "a", "name": "x", "description": "y", "rank": 2}])"), "[0].rank: unknown key"));
}

TEST_CASE("an unknown prerequisite is refused at load")
{
    CHECK(mentions(parseProblems("[" + entry("a", R"("ghost")") + "]"), "[0].requires[0]: unknown capability \"ghost\""));
}

TEST_CASE("a capability may not require itself")
{
    CHECK(mentions(parseProblems("[" + entry("a", R"("a")") + "]"), "prerequisite cycle: a -> a"));
}

TEST_CASE("a prerequisite cycle is refused at load and named")
{
    const auto problems = parseProblems("[" + entry("a", R"("c")") + "," + entry("b", R"("a")") + "," + entry("c", R"("b")") + "," + entry("d") + "]");
    REQUIRE(problems.size() == 1);
    CHECK(mentions(problems, "prerequisite cycle: a -> c -> b -> a"));
}

TEST_CASE("a diamond of prerequisites is not a cycle")
{
    CHECK(parseProblems("[" + entry("top") + "," + entry("l", R"("top")") + "," + entry("r", R"("top")") + "," + entry("bottom", R"("l", "r")") + "]").empty());
}

TEST_CASE("findPrerequisiteCycle answers the same whatever the table order")
{
    std::vector<Capability> table = {
        {"a", "A", "a", {"b"}},
        {"b", "B", "b", {"a"}},
        {"c", "C", "c", {}},
    };
    const auto forward = findPrerequisiteCycle(table);
    std::reverse(table.begin(), table.end());
    CHECK(findPrerequisiteCycle(table) == forward);
    CHECK(forward == std::vector<std::string>{"a", "b", "a"});
}

TEST_CASE("the table digest ignores source order")
{
    auto table = chain();
    const auto digest = capabilitiesDigest(table);
    std::reverse(table.begin(), table.end());
    CHECK(capabilitiesDigest(table) == digest);
    table[0].description = "changed";
    CHECK(capabilitiesDigest(table) != digest);
}

TEST_CASE("a new player can do nothing yet")
{
    const auto table = chain();
    const CapabilityUnlocks unlocks(table);
    CHECK_FALSE(unlocks.can("a"));
    CHECK(unlocks.unlocked().empty());
}

TEST_CASE("unlocking a capability makes can() true")
{
    const auto table = chain();
    CapabilityUnlocks unlocks(table);
    CHECK(unlocks.unlock("a") == UnlockResult::Unlocked);
    CHECK(unlocks.can("a"));
    CHECK_FALSE(unlocks.can("d"));
}

TEST_CASE("an unknown id is rejected and changes nothing")
{
    const auto table = chain();
    CapabilityUnlocks unlocks(table);
    const auto before = unlocks.digest();
    CHECK(unlocks.unlock("ghost") == UnlockResult::UnknownCapability);
    CHECK_FALSE(unlocks.can("ghost"));
    CHECK(unlocks.digest() == before);
}

TEST_CASE("unlocking twice is reported and changes nothing")
{
    const auto table = chain();
    CapabilityUnlocks unlocks(table);
    REQUIRE(unlocks.unlock("a") == UnlockResult::Unlocked);
    const auto before = unlocks.digest();
    CHECK(unlocks.unlock("a") == UnlockResult::AlreadyUnlocked);
    CHECK(unlocks.unlocked().size() == 1);
    CHECK(unlocks.digest() == before);
}

TEST_CASE("an unmet prerequisite blocks the unlock until it is met")
{
    const auto table = chain();
    CapabilityUnlocks unlocks(table);
    CHECK(unlocks.unlock("c") == UnlockResult::PrerequisiteUnmet);
    CHECK_FALSE(unlocks.can("c"));
    REQUIRE(unlocks.unlock("a") == UnlockResult::Unlocked);
    CHECK(unlocks.unlock("c") == UnlockResult::PrerequisiteUnmet);
    REQUIRE(unlocks.unlock("b") == UnlockResult::Unlocked);
    CHECK(unlocks.unlock("c") == UnlockResult::Unlocked);
    CHECK(unlocks.can("c"));
}

TEST_CASE("the unlocked list and digest do not depend on unlock order")
{
    const auto table = chain();
    CapabilityUnlocks first(table);
    CapabilityUnlocks second(table);
    first.unlock("d");
    first.unlock("a");
    second.unlock("a");
    second.unlock("d");
    CHECK(first.unlocked() == std::vector<std::string>{"a", "d"});
    CHECK(first.digest() == second.digest());
}

TEST_CASE("the digest changes when an unlock is added")
{
    const auto table = chain();
    CapabilityUnlocks unlocks(table);
    const auto empty = unlocks.digest();
    unlocks.unlock("a");
    CHECK(unlocks.digest() != empty);
}

TEST_CASE("the digest of a fixed state is a fixed number")
{
    const auto table = chain();
    CapabilityUnlocks unlocks(table);
    unlocks.unlock("a");
    unlocks.unlock("b");
    CHECK(unlocks.digest() == 0x2b18666df730f71eull);
}

TEST_CASE("unlock state round-trips through bytes")
{
    const auto table = chain();
    CapabilityUnlocks unlocks(table);
    unlocks.unlock("a");
    unlocks.unlock("b");
    unlocks.unlock("d");
    const auto bytes = unlocks.serialise();
    CHECK(bytes == unlocks.serialise());

    CapabilityUnlocks loaded(table);
    REQUIRE(loaded.deserialise(bytes));
    CHECK(loaded.unlocked() == unlocks.unlocked());
    CHECK(loaded.digest() == unlocks.digest());
    CHECK(loaded.serialise() == bytes);
}

TEST_CASE("the byte layout is version, count, then length-prefixed sorted ids")
{
    const auto table = chain();
    CapabilityUnlocks unlocks(table);
    unlocks.unlock("a");
    CHECK(unlocks.serialise() == std::vector<std::uint8_t>{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 'a'});
}

TEST_CASE("an empty unlock set serialises and loads")
{
    const auto table = chain();
    CapabilityUnlocks unlocks(table);
    const auto bytes = unlocks.serialise();
    CHECK(bytes == std::vector<std::uint8_t>{1, 0, 0, 0, 0, 0, 0, 0});

    CapabilityUnlocks target(table);
    target.unlock("a");
    REQUIRE(target.deserialise(bytes));
    CHECK(target.unlocked().empty());
}

TEST_CASE("bytes that name an unknown capability are refused and leave the state alone")
{
    const auto table = chain();
    const std::vector<Capability> other = {{"zzz", "Z", "z", {}}};
    CapabilityUnlocks source(other);
    source.unlock("zzz");

    CapabilityUnlocks target(table);
    target.unlock("a");
    const auto before = target.digest();
    CHECK_FALSE(target.deserialise(source.serialise()));
    CHECK(target.digest() == before);
}

TEST_CASE("bytes that unlock a capability without its prerequisite are refused")
{
    const auto table = chain();
    const std::vector<Capability> flat = {{"a", "A", "a", {}}, {"c", "C", "c", {}}};
    CapabilityUnlocks source(flat);
    source.unlock("c");

    CapabilityUnlocks target(table);
    CHECK_FALSE(target.deserialise(source.serialise()));
    CHECK(target.unlocked().empty());
}

TEST_CASE("truncated, padded and wrong-version bytes are refused")
{
    const auto table = chain();
    CapabilityUnlocks unlocks(table);
    unlocks.unlock("a");
    const auto bytes = unlocks.serialise();

    CapabilityUnlocks target(table);
    for (std::size_t cut = 0; cut < bytes.size(); ++cut) {
        CHECK_FALSE(target.deserialise(std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cut))));
    }
    auto padded = bytes;
    padded.push_back(0);
    CHECK_FALSE(target.deserialise(padded));
    auto wrongVersion = bytes;
    wrongVersion[0] = 99;
    CHECK_FALSE(target.deserialise(wrongVersion));
    CHECK(target.unlocked().empty());
}

TEST_CASE("bytes that list the same capability twice are refused")
{
    const auto table = chain();
    CapabilityUnlocks unlocks(table);
    unlocks.unlock("a");
    auto bytes = unlocks.serialise();
    // version u32, count u32, then one string (u32 length + bytes): bump the count and repeat the string.
    const std::vector<std::uint8_t> item(bytes.begin() + 8, bytes.end());
    bytes[4] = 2;
    bytes.insert(bytes.end(), item.begin(), item.end());

    CapabilityUnlocks target(table);
    CHECK_FALSE(target.deserialise(bytes));
}
