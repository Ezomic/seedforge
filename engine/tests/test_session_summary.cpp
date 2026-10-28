#include "ichor/core/session_summary.h"
#include "test_temp.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

using namespace ichor;
namespace fs = std::filesystem;

namespace {

struct Dir {
    fs::path path;
    explicit Dir(const char* name)
        : path(test::tempPath(name))
    {
        fs::remove_all(path);
    }
    ~Dir()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

std::string slurp(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
}

// 2026-10-28 15:04:05 UTC
std::chrono::system_clock::time_point quitTime()
{
    return std::chrono::sys_days{std::chrono::year{2026} / 10 / 28} + std::chrono::hours{15} + std::chrono::minutes{4} + std::chrono::seconds{5};
}

SessionSummary sample()
{
    SessionSummary summary;
    summary.seed = 22;
    summary.flags = {"--forged", "--seed", "22", "--combat"};
    summary.playSeconds = 12.5;
    for (int i = 0; i < 98; ++i) summary.frames.add(0.010);
    summary.frames.add(0.020);
    summary.frames.add(0.050);
    summary.deathsByCause = {{"unknown", 0}, {"stamina", 2}, {"bleed", 1}};
    summary.retries = 3;
    summary.quitTime = quitTime();
    return summary;
}

}

TEST_CASE("the session summary pins its field names")
{
    const nlohmann::json json = nlohmann::json::parse(sessionSummaryJson(sample()));
    std::set<std::string> keys;
    for (const auto& [key, value] : json.items()) keys.insert(key);
    REQUIRE(keys == std::set<std::string>{"schema", "build_version", "seed", "flags", "play_seconds", "frames", "fps", "deaths_by_cause", "retries", "quit_time_utc"});
    std::set<std::string> fps;
    for (const auto& [key, value] : json["fps"].items()) fps.insert(key);
    REQUIRE(fps == std::set<std::string>{"p50", "p95", "p99", "worst"});
    REQUIRE(json["schema"] == 1);
}

TEST_CASE("the session summary carries the run it describes")
{
    const nlohmann::json json = nlohmann::json::parse(sessionSummaryJson(sample()));
    REQUIRE(json["build_version"].get<std::string>() == kUnstampedBuild);
    REQUIRE(json["seed"] == 22);
    REQUIRE(json["flags"] == nlohmann::json::array({"--forged", "--seed", "22", "--combat"}));
    REQUIRE(json["play_seconds"].get<double>() == Catch::Approx(12.5));
    REQUIRE(json["frames"] == 100);
    REQUIRE(json["deaths_by_cause"]["stamina"] == 2);
    REQUIRE(json["deaths_by_cause"]["bleed"] == 1);
    REQUIRE(json["retries"] == 3);
    REQUIRE(json["quit_time_utc"] == "2026-10-28T15:04:05Z");
}

TEST_CASE("fps percentiles follow the slow frames, not the typical one")
{
    const nlohmann::json fps = nlohmann::json::parse(sessionSummaryJson(sample()))["fps"];
    REQUIRE(fps["p50"].get<double>() == Catch::Approx(100.0).margin(0.05));
    REQUIRE(fps["p95"].get<double>() == Catch::Approx(100.0).margin(0.05));
    // The 99th of 100 frames is the 20 ms one, and the worst is the 50 ms one: both well under the median.
    REQUIRE(fps["p99"].get<double>() == Catch::Approx(50.0).margin(0.05));
    REQUIRE(fps["worst"].get<double>() == Catch::Approx(20.0).margin(0.05));
}

TEST_CASE("an instant quit with no frames writes null fps rather than dividing by zero")
{
    SessionSummary summary;
    summary.quitTime = quitTime();
    const nlohmann::json json = nlohmann::json::parse(sessionSummaryJson(summary));
    REQUIRE(json["frames"] == 0);
    REQUIRE(json["play_seconds"].get<double>() == 0.0);
    REQUIRE(json["fps"]["p50"].is_null());
    REQUIRE(json["fps"]["p95"].is_null());
    REQUIRE(json["fps"]["p99"].is_null());
    REQUIRE(json["fps"]["worst"].is_null());
    REQUIRE(json["deaths_by_cause"].is_object());
    REQUIRE(json["deaths_by_cause"].empty());
    REQUIRE(json["retries"] == 0);
}

TEST_CASE("a zero-length frame does not make an infinite fps")
{
    SessionSummary summary;
    summary.frames.add(0.0);
    summary.frames.add(-1.0);
    const nlohmann::json fps = nlohmann::json::parse(sessionSummaryJson(summary))["fps"];
    REQUIRE(fps["p50"].is_null());
    REQUIRE(fps["worst"].is_null());
}

TEST_CASE("run flags keep their names and plain values but no paths")
{
    const std::vector<std::string> flags = scrubRunFlags({"--forged", "--seed", "22", "--saves", "C:\\Users\\someone\\saves", "--record", "runs/a.iinp", "--combat-bot", "expert", "--load", "/home/someone/x.isav"});
    REQUIRE(flags == std::vector<std::string>{"--forged", "--seed", "22", "--saves", "<path>", "--record", "<path>", "--combat-bot", "expert", "--load", "<path>"});
}

TEST_CASE("strings are escaped so the summary stays valid json")
{
    SessionSummary summary;
    summary.flags = {"--campaign", "a\"b\\c\nd"};
    const nlohmann::json json = nlohmann::json::parse(sessionSummaryJson(summary));
    REQUIRE(json["flags"][1] == "a\"b\\c\nd");
}

TEST_CASE("a code-page byte from a windows command line still makes valid json")
{
    SessionSummary summary;
    summary.flags = {"--campaign", "Caf\xE9"};
    const nlohmann::json json = nlohmann::json::parse(sessionSummaryJson(summary));
    REQUIRE(json["flags"][1] == "Caf?");
}

TEST_CASE("the session file is named for the quit time and never overwrites an earlier one")
{
    Dir dir("session_summary_files");
    const fs::path first = writeSessionFile(dir.path, "session", quitTime(), "{\"n\":1}");
    REQUIRE(first == dir.path / "session-20261028-150405Z.json");
    const fs::path second = writeSessionFile(dir.path, "session", quitTime(), "{\"n\":2}");
    REQUIRE(second == dir.path / "session-20261028-150405Z-2.json");
    const fs::path third = writeSessionFile(dir.path, "session", quitTime(), "{\"n\":3}");
    REQUIRE(third == dir.path / "session-20261028-150405Z-3.json");
    REQUIRE(slurp(first) == "{\"n\":1}");
    REQUIRE(slurp(second) == "{\"n\":2}");
    REQUIRE(slurp(third) == "{\"n\":3}");
}

TEST_CASE("a session file that cannot be written reports an empty path")
{
    Dir dir("session_summary_blocked");
    fs::create_directories(dir.path.parent_path());
    std::ofstream(dir.path) << "a file where the folder should be";
    REQUIRE(writeSessionFile(dir.path, "session", quitTime(), "{}").empty());
    fs::remove(dir.path);
}
