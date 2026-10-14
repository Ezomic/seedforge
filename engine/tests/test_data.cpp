#include "ichor/data/data_set.h"
#include "ichor/script/data_bindings.h"
#include "ichor/script/script_host.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

using namespace ichor;

namespace {

class Fixture {
public:
    Fixture()
    {
        static int counter = 0;
        static const unsigned process = std::random_device{}();
        root_ = std::filesystem::temp_directory_path() / ("ichor_data_test_" + std::to_string(process) + "_" + std::to_string(counter++));
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);
    }
    ~Fixture() { std::filesystem::remove_all(root_); }

    void write(const std::string& relative, const std::string& text) const
    {
        const std::filesystem::path path = root_ / relative;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << text;
    }
    const std::filesystem::path& root() const { return root_; }

private:
    std::filesystem::path root_;
};

constexpr const char* kWeapons = R"([
    {"id": "iron_sword", "damage": 5, "two_handed": false, "name": "Iron Sword", "tags": ["blade", "common"]},
    {"id": "spear", "damage": 4.5, "two_handed": true, "name": "Spear", "tags": []}
])";

}

TEST_CASE("a data directory loads into id-keyed tables")
{
    const Fixture fixture;
    fixture.write("catalogue/weapons.json", kWeapons);
    fixture.write("axioms/laws.json", R"([{"id": "mortality", "weight": 3}])");
    DataSet data;
    REQUIRE(data.loadDirectory(fixture.root()));
    REQUIRE(data.tableCount() == 2);
    REQUIRE(data.table("catalogue") != nullptr);
    REQUIRE(data.table("catalogue")->records.size() == 2);

    const DataValue* damage = data.field("catalogue", "iron_sword", "damage");
    REQUIRE(damage != nullptr);
    REQUIRE(std::get<double>(*damage) == Catch::Approx(5.0));
    REQUIRE(std::get<bool>(*data.field("catalogue", "spear", "two_handed")) == true);
    REQUIRE(std::get<std::string>(*data.field("catalogue", "iron_sword", "name")) == "Iron Sword");
    REQUIRE(std::get<std::vector<std::string>>(*data.field("catalogue", "iron_sword", "tags")).size() == 2);
    REQUIRE(data.field("catalogue", "iron_sword", "missing") == nullptr);
    REQUIRE(data.record("catalogue", "bow") == nullptr);
    REQUIRE(data.table("nothing") == nullptr);
}

TEST_CASE("several files of one table share their ids")
{
    const Fixture fixture;
    fixture.write("catalogue/a.json", R"([{"id": "one", "n": 1}])");
    fixture.write("catalogue/b.json", R"([{"id": "two", "n": 2}])");
    DataSet data;
    REQUIRE(data.loadDirectory(fixture.root()));
    REQUIRE(data.table("catalogue")->records.size() == 2);

    fixture.write("catalogue/c.json", R"([{"id": "one", "n": 3}])");
    REQUIRE_FALSE(data.loadDirectory(fixture.root()));
    REQUIRE(data.lastError().find("duplicate id 'one'") != std::string::npos);
    REQUIRE(data.lastError().find("c.json") != std::string::npos);
    REQUIRE(data.table("catalogue")->records.size() == 2); // the last good data stays
}

TEST_CASE("malformed data is refused with the file named")
{
    const Fixture fixture;
    DataSet data;
    const auto expectError = [&](const std::string& text, const std::string& fragment) {
        fixture.write("t/bad.json", text);
        INFO(text);
        REQUIRE_FALSE(data.loadDirectory(fixture.root()));
        REQUIRE(data.lastError().find("bad.json") != std::string::npos);
        REQUIRE(data.lastError().find(fragment) != std::string::npos);
    };
    expectError("{ not json", "not valid JSON");
    expectError(R"({"id": "x"})", "array");
    expectError(R"([{"damage": 1}])", "id");
    expectError(R"([{"id": ""}])", "id");
    expectError(R"([{"id": 7}])", "id");
    expectError(R"([{"id": "x", "nested": {"a": 1}}])", "'nested'");
    expectError(R"([{"id": "x", "mixed": ["a", 2]}])", "'mixed'");
    expectError(R"([{"id": "x", "nothing": null}])", "'nothing'");
    REQUIRE_FALSE(data.loadDirectory(fixture.root() / "nowhere"));
}

TEST_CASE("the digest follows the content, not the file layout")
{
    const Fixture one;
    one.write("catalogue/weapons.json", kWeapons);
    const Fixture split;
    split.write("catalogue/a.json", R"([{"id": "iron_sword", "damage": 5, "two_handed": false, "name": "Iron Sword", "tags": ["blade", "common"]}])");
    split.write("catalogue/b.json", R"([{"id": "spear", "damage": 4.5, "two_handed": true, "name": "Spear", "tags": []}])");
    const Fixture changed;
    changed.write("catalogue/weapons.json", R"([
        {"id": "iron_sword", "damage": 6, "two_handed": false, "name": "Iron Sword", "tags": ["blade", "common"]},
        {"id": "spear", "damage": 4.5, "two_handed": true, "name": "Spear", "tags": []}
    ])");

    DataSet a;
    DataSet b;
    DataSet c;
    REQUIRE(a.loadDirectory(one.root()));
    REQUIRE(b.loadDirectory(split.root()));
    REQUIRE(c.loadDirectory(changed.root()));
    REQUIRE(a.digest() == b.digest());
    REQUIRE(a.digest() != c.digest());
    REQUIRE(a.digest() != DataSet{}.digest());
}

TEST_CASE("scripts read the data layer and see a reload")
{
    const Fixture fixture;
    fixture.write("catalogue/weapons.json", kWeapons);
    DataSet data;
    REQUIRE(data.loadDirectory(fixture.root()));
    ScriptHost host(1);
    bindData(host, data);
    REQUIRE(host.run("q", R"(
        function damage(id) return ichor.data('catalogue', id, 'damage') end
        function label(id) return ichor.data('catalogue', id, 'tags') end
        function known(id) return ichor.data_has('catalogue', id) end
    )"));

    ScriptValue result;
    REQUIRE(host.call("damage", {std::string("iron_sword")}, &result));
    REQUIRE(std::get<double>(result) == Catch::Approx(5.0));
    REQUIRE(host.call("label", {std::string("iron_sword")}, &result));
    REQUIRE(std::get<std::string>(result) == "blade,common");
    REQUIRE(host.call("damage", {std::string("bow")}, &result));
    REQUIRE(std::holds_alternative<std::monostate>(result));
    REQUIRE(host.call("known", {std::string("spear")}, &result));
    REQUIRE(std::get<bool>(result) == true);

    fixture.write("catalogue/weapons.json", R"([{"id": "iron_sword", "damage": 9}])");
    REQUIRE(data.loadDirectory(fixture.root()));
    REQUIRE(host.call("damage", {std::string("iron_sword")}, &result));
    REQUIRE(std::get<double>(result) == Catch::Approx(9.0));
    REQUIRE(host.call("known", {std::string("spear")}, &result));
    REQUIRE(std::get<bool>(result) == false);

    REQUIRE_FALSE(host.run("bad", "ichor.data(1, 2, 3)"));
    REQUIRE(host.lastError().find("data") != std::string::npos);
}

TEST_CASE("the repo data digest is the same through the game path and the forge path")
{
    DataSet game;
    DataSet forge;
    REQUIRE(game.loadDirectory(ICHOR_GAME_DATA_DIR));
    REQUIRE(forge.loadDirectory(std::filesystem::path(ICHOR_REPO_DATA) / ""));
    REQUIRE(game.tableCount() > 0);
    REQUIRE(game.digest() != 0);
    REQUIRE(game.digest() == forge.digest());
}

TEST_CASE("a trailing slash does not change the digest")
{
    const Fixture fixture;
    fixture.write("catalogue/weapons.json", kWeapons);
    DataSet plain;
    DataSet slashed;
    REQUIRE(plain.loadDirectory(fixture.root()));
    REQUIRE(slashed.loadDirectory(fixture.root().string() + "/"));
    REQUIRE(plain.digest() == slashed.digest());
}

TEST_CASE("CRLF and LF checkouts of a data file give the same digest")
{
    const std::string lf = "[\n  {\"id\": \"a\", \"name\": \"x\", \"n\": 1.5},\n  {\"id\": \"b\", \"tags\": [\"t\"]}\n]\n";
    std::string crlf;
    for (const char c : lf) {
        if (c == '\n') crlf += '\r';
        crlf += c;
    }
    const Fixture first;
    const Fixture second;
    first.write("catalogue/a.json", lf);
    second.write("catalogue/a.json", crlf);
    DataSet a;
    DataSet b;
    REQUIRE(a.loadDirectory(first.root()));
    REQUIRE(b.loadDirectory(second.root()));
    REQUIRE(a.digest() == b.digest());
}

TEST_CASE("files created in a different order give the same digest")
{
    const Fixture first;
    const Fixture second;
    first.write("catalogue/a.json", R"([{"id": "one", "n": 1}])");
    first.write("catalogue/b.json", R"([{"id": "two", "n": 2}])");
    first.write("npcs/c.json", R"([{"id": "three", "n": 3}])");
    second.write("npcs/c.json", R"([{"id": "three", "n": 3}])");
    second.write("catalogue/b.json", R"([{"id": "two", "n": 2}])");
    second.write("catalogue/a.json", R"([{"id": "one", "n": 1}])");
    DataSet a;
    DataSet b;
    REQUIRE(a.loadDirectory(first.root()));
    REQUIRE(b.loadDirectory(second.root()));
    REQUIRE(a.digest() == b.digest());
}

TEST_CASE("a missing directory, a plain file and a malformed file are refused with a message")
{
    const Fixture fixture;
    DataSet data;
    REQUIRE_FALSE(data.loadDirectory(fixture.root() / "nothing"));
    REQUIRE_FALSE(data.lastError().empty());

    fixture.write("plain.txt", "x");
    REQUIRE_FALSE(data.loadDirectory(fixture.root() / "plain.txt"));
    REQUIRE_FALSE(data.lastError().empty());

    fixture.write("catalogue/good.json", R"([{"id": "one"}])");
    fixture.write("catalogue/bad.json", "[{\"id\": ");
    REQUIRE_FALSE(data.loadDirectory(fixture.root()));
    REQUIRE(data.lastError().find("bad.json") != std::string::npos);
    REQUIRE(data.tableCount() == 0);
}

TEST_CASE("an empty directory loads as no tables")
{
    const Fixture fixture;
    DataSet data;
    REQUIRE(data.loadDirectory(fixture.root()));
    REQUIRE(data.tableCount() == 0);
}
