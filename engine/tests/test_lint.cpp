#include "ichor/lint/lint.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using namespace ichor::lint;

namespace {

namespace fs = std::filesystem;

// A scratch copy of the shipped data, so a test can break it without touching the repo.
class DataCopy {
public:
    DataCopy()
    {
        static int counter = 0;
        root_ = fs::path(ICHOR_TEST_OUTPUT) / ("lint_copy_" + std::to_string(counter++));
        fs::remove_all(root_);
        fs::create_directories(root_);
        fs::copy(ICHOR_REPO_DATA, root_, fs::copy_options::recursive);
    }
    ~DataCopy()
    {
        std::error_code code;
        fs::remove_all(root_, code);
    }
    DataCopy(const DataCopy&) = delete;
    DataCopy& operator=(const DataCopy&) = delete;

    void write(const std::string& relative, const std::string& text) const
    {
        const fs::path path = root_ / relative;
        fs::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << text;
    }
    const fs::path& root() const { return root_; }

private:
    fs::path root_;
};

std::vector<Problem> from(const Report& report, const std::string& validator)
{
    std::vector<Problem> found;
    for (const Problem& p : report.problems)
        if (p.validator == validator) found.push_back(p);
    return found;
}

}

TEST_CASE("the shipped data passes every registered validator", "[lint]")
{
    const Report report = runLint(ICHOR_REPO_DATA);
    for (const Problem& p : report.problems) INFO(describe(p));
    CHECK(report.error.empty());
    CHECK(report.problems.empty());
    CHECK(report.results.size() == validators().size());
}

TEST_CASE("registry names are unique and each entry is complete", "[lint]")
{
    std::set<std::string_view> names;
    for (const Validator& v : validators()) {
        CHECK_FALSE(v.name.empty());
        CHECK_FALSE(v.summary.empty());
        CHECK(v.run != nullptr);
        CHECK(names.insert(v.name).second);
    }
}

TEST_CASE("a bad data root or validator name is an error and runs nothing", "[lint]")
{
    const Report missing = runLint(fs::path(ICHOR_TEST_OUTPUT) / "lint_no_such_dir");
    CHECK_FALSE(missing.ok());
    CHECK_FALSE(missing.error.empty());
    CHECK(missing.results.empty());

    const Report unknown = runLint(ICHOR_REPO_DATA, {"nope"});
    CHECK_FALSE(unknown.ok());
    CHECK(unknown.results.empty());
}

TEST_CASE("--only style filtering runs just the named validators", "[lint]")
{
    const Report report = runLint(ICHOR_REPO_DATA, {"reskin"});
    REQUIRE(report.results.size() == 1);
    CHECK(report.results.front().name == "reskin");
}

TEST_CASE("a broken reskin layer is reported with its file", "[lint]")
{
    const DataCopy copy;
    copy.write("reskin/broken.json", R"([{"id":"a","kind":"biome","key":"k","scale_permille":0}])");
    const Report report = runLint(copy.root());
    CHECK_FALSE(report.ok());
    const std::vector<Problem> found = from(report, "reskin");
    REQUIRE_FALSE(found.empty());
    CHECK(found.front().file == "reskin/broken.json");
    CHECK(from(report, "ids").empty());
}

TEST_CASE("a malformed table file and a positional id are both reported", "[lint]")
{
    const DataCopy copy;
    copy.write("traversal/zz_broken.json", "{not json");
    copy.write("weapon_classes/zz_ids.json", R"([{"id":"item_47"}])");
    const Report report = runLint(copy.root());
    CHECK_FALSE(report.ok());

    const std::vector<Problem> tables = from(report, "tables");
    REQUIRE(tables.size() == 1);
    CHECK(tables.front().file.find("zz_broken.json") != std::string::npos);

    const std::vector<Problem> ids = from(report, "ids");
    REQUIRE_FALSE(ids.empty());
    CHECK(std::any_of(ids.begin(), ids.end(), [](const Problem& p) { return p.id == "item_47" && p.file.find("zz_ids.json") != std::string::npos; }));
}

TEST_CASE("a synergy naming unknown axioms is reported with its file", "[lint]")
{
    const DataCopy copy;
    REQUIRE(runLint(copy.root()).ok());

    copy.write("synergies/zz_dangling.json",
        R"({"synergies":[{"id":"zz.dangling","tier":"cataclysm_powers","axioms":["cataclysm.no_such","powers.no_such"],"content_tier":2,"world_name":"X","flavour":{"a":"b"}}]})");
    const Report report = runLint(copy.root());
    CHECK_FALSE(report.ok());
    const std::vector<Problem> found = from(report, "synergies");
    REQUIRE_FALSE(found.empty());
    CHECK(found.front().file.find("zz_dangling.json") != std::string::npos);
}

TEST_CASE("the report is identical across runs", "[lint]")
{
    const DataCopy copy;
    copy.write("reskin/broken.json", R"([{"id":"a","kind":"x","key":"k"},{"id":"a","kind":"biome","key":"k","tint":"red"}])");
    const Report a = runLint(copy.root());
    const Report b = runLint(copy.root());
    REQUIRE(a.problems.size() == b.problems.size());
    for (std::size_t i = 0; i < a.problems.size(); ++i) CHECK(describe(a.problems[i]) == describe(b.problems[i]));
}
