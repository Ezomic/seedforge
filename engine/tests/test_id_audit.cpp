// Stable string id audit (SEED-233). Adding a catalogue entry must not change what an existing id
// means, so every id under data/ must be a stable string: well formed, unique in its table, and
// never a number or a list position.

#include "ichor/data/id_audit.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <process.h>
#define ICHOR_PID _getpid()
#else
#include <unistd.h>
#define ICHOR_PID getpid()
#endif

using namespace ichor;

namespace {

class Fixture {
public:
    Fixture()
    {
        static int counter = 0;
        root_ = std::filesystem::temp_directory_path() / ("ichor_idaudit_test_" + std::to_string(ICHOR_PID) + "_" + std::to_string(counter++));
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

bool mentions(const IdAuditReport& report, const std::string& reason, const std::string& id = {})
{
    return std::any_of(report.violations.begin(), report.violations.end(), [&](const IdViolation& v) {
        return v.reason.find(reason) != std::string::npos && (id.empty() || v.id == id);
    });
}

}

TEST_CASE("idProblem accepts stable ids", "[id_audit]")
{
    for (const char* id : {"bitter", "cataclysm.flood", "vista_open", "a", "tier2", "road_bonus", "a.b.c"}) {
        INFO(id);
        CHECK(idProblem(id).empty());
    }
}

TEST_CASE("idProblem rejects malformed, numeric and positional ids", "[id_audit]")
{
    for (const char* id : {"", "47", "0", "3d_model", "item_47", "slot_0", "tier_2", "a.12", "a.b_7", "Bitter", "has space", "dash-ed", "tab\t", ".a", "a.", "a..b", "_a", "ünï", "a/b", "a\n"}) {
        INFO(id);
        CHECK_FALSE(idProblem(id).empty());
    }
    CHECK_FALSE(idProblem(std::string(kMaxIdLength + 1, 'a')).empty());
    CHECK(idProblem(std::string(kMaxIdLength, 'a')).empty());
    CHECK_FALSE(idProblem(std::string("a\0b", 3)).empty());
}

TEST_CASE("a clean tree passes", "[id_audit]")
{
    Fixture fx;
    fx.write("npcs/traits.json", R"([{"id": "bitter"}, {"id": "cheerful"}])");
    fx.write("ui/hud.json", R"({"elements": [{"id": "compass"}, {"id": "clock"}]})");
    fx.write("axioms/flood.json", R"({"id": "cataclysm.flood", "slot": "cataclysm"})");
    fx.write("materials/response.json", R"({"roughness": [1, 2, 3]})");
    const IdAuditReport report = auditIds(fx.root());
    CHECK(report.ok());
    CHECK(report.filesScanned == 4);
    CHECK(report.idsChecked == 5);
}

TEST_CASE("a deliberately bad fixture is flagged", "[id_audit]")
{
    Fixture fx;
    fx.write("npcs/traits.json", R"([{"id": 47}, {"id": "item_47"}, {"id": "Bitter"}, {"id": "ok_one"}, {"name": "no id"}, 5])");
    fx.write("ui/hud.json", R"({"elements": [{"id": "compass"}, {"id": "compass"}, {"id": ""}]})");
    const IdAuditReport report = auditIds(fx.root());
    REQUIRE_FALSE(report.ok());
    CHECK(mentions(report, "not a string"));
    CHECK(mentions(report, "list position", "item_47"));
    CHECK(mentions(report, "lowercase", "Bitter"));
    CHECK(mentions(report, "record has no id"));
    CHECK(mentions(report, "duplicate id", "compass"));
    CHECK(mentions(report, "empty"));
    CHECK(report.violations.size() == 7);
}

TEST_CASE("ids are unique across all files of a table but not across tables", "[id_audit]")
{
    Fixture fx;
    fx.write("npcs/a.json", R"([{"id": "bitter"}])");
    fx.write("npcs/b.json", R"([{"id": "bitter"}])");
    fx.write("music/cues.json", R"([{"id": "bitter"}])");
    const IdAuditReport report = auditIds(fx.root());
    REQUIRE(report.violations.size() == 1);
    CHECK(report.violations[0].file == "npcs/b.json");
    CHECK(report.violations[0].reason.find("npcs/a.json") != std::string::npos);
}

TEST_CASE("axiom ids are unique across files and required", "[id_audit]")
{
    Fixture fx;
    fx.write("axioms/a.json", R"({"id": "cataclysm.flood"})");
    fx.write("axioms/b.json", R"({"id": "cataclysm.flood"})");
    fx.write("axioms/c.json", R"({"slot": "x"})");
    const IdAuditReport report = auditIds(fx.root());
    CHECK(mentions(report, "duplicate id", "cataclysm.flood"));
    CHECK(mentions(report, "axiom has no id"));
    CHECK(report.violations.size() == 2);
}

TEST_CASE("nested ids are checked and scoped to their list", "[id_audit]")
{
    Fixture fx;
    fx.write("ui/hud.json", R"({"elements": [{"id": "a", "parts": [{"id": "x"}, {"id": "x"}]}, {"id": "b", "parts": [{"id": "y1"}, {"id": "z_9"}]}]})");
    const IdAuditReport report = auditIds(fx.root());
    CHECK(mentions(report, "duplicate id", "x"));
    CHECK(mentions(report, "list position", "z_9"));
    CHECK(report.violations.size() == 2);
}

TEST_CASE("hostile files are reported, not crashed on", "[id_audit]")
{
    Fixture fx;
    fx.write("a/broken.json", "{\"id\": ");
    fx.write("a/empty.json", "");
    fx.write("a/null.json", "null");
    fx.write("a/zero_byte.json", std::string("[{\"id\": \"a\\u0000b\"}]"));
    fx.write("a/deep.json", std::string(5000, '[') + std::string(5000, ']'));
    fx.write("a/big.json", "[" + std::string(2 * 1024 * 1024, ' ') + "]");
    fx.write("a/note.txt", "not json {\"id\": 1}");
    const IdAuditReport report = auditIds(fx.root());
    CHECK(report.filesScanned == 6);
    CHECK(mentions(report, "not valid JSON"));
    CHECK(mentions(report, "larger than the audit limit"));
    CHECK(mentions(report, "nesting too deep"));
    CHECK(mentions(report, "only lowercase"));
}

TEST_CASE("a missing data root is an empty, clean report", "[id_audit]")
{
    const IdAuditReport report = auditIds(std::filesystem::temp_directory_path() / "ichor_idaudit_does_not_exist");
    CHECK(report.ok());
    CHECK(report.filesScanned == 0);
}

TEST_CASE("the report is sorted and stable", "[id_audit]")
{
    Fixture fx;
    fx.write("b/z.json", R"([{"id": 1}])");
    fx.write("a/y.json", R"([{"id": 2}, {"id": 3}])");
    const IdAuditReport report = auditIds(fx.root());
    REQUIRE(report.violations.size() == 3);
    CHECK(report.violations[0].file == "a/y.json");
    CHECK(report.violations[0].path == "[0]/id");
    CHECK(report.violations[1].path == "[1]/id");
    CHECK(report.violations[2].file == "b/z.json");
    CHECK(describe(report.violations[0]) == "a/y.json: [0]/id: id is not a string");
}

TEST_CASE("shipped data under data/ has only stable string ids", "[id_audit]")
{
    const IdAuditReport report = auditIds(ICHOR_REPO_DATA);
    for (const IdViolation& violation : report.violations) {
        UNSCOPED_INFO(describe(violation));
    }
    CHECK(report.ok());
    CHECK(report.filesScanned >= 30);
    CHECK(report.idsChecked >= 100);
}
