#include "ichor/core/crash_handler.h"

#include <catch2/catch_test_macros.hpp>

using namespace ichor;

TEST_CASE("--crash-test names its three crashes and nothing else")
{
    CHECK(parseCrashTest("thread") == CrashTest::OtherThread);
    CHECK(parseCrashTest("handler") == CrashTest::InHandler);
    CHECK(parseCrashTest("overflow") == CrashTest::StackOverflow);
    CHECK_FALSE(parseCrashTest(""));
    CHECK_FALSE(parseCrashTest("--headless"));
    CHECK_FALSE(parseCrashTest("Thread"));
}

// Windows runs these crashes through the app instead (crash_handler_app_check.cmake): without fork a crash
// needs a process of its own, and the app is what ships. Linux PR CI builds no app, so the handler is
// crashed here in a forked child.
#ifndef _WIN32

#include "ichor/core/version.h"
#include "test_temp.h"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

namespace {

struct Crash {
    int status = 0;
    std::string report; // the one crash-*.txt, empty when none was written
    int reports = 0;
    std::string err;    // the child's stderr
};

std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The child's stdout and stderr go to a file, so Catch2's and the sanitizers' reports of the deliberate
// crash do not land in this test's output.
Crash crashChild(const fs::path& dir, const fs::path& logs, CrashTest kind)
{
    const fs::path errPath = dir / "stderr.txt";
    const pid_t pid = fork();
    if (pid == 0) {
        const int fd = open(errPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        dup2(fd, 1);
        dup2(fd, 2);
        installCrashHandler(logs);
        crashForTest(kind);
    }
    Crash crash;
    REQUIRE(pid > 0);
    REQUIRE(waitpid(pid, &crash.status, 0) == pid);
    crash.err = readFile(errPath);
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator(logs, ec)) {
        const std::string name = entry.path().filename().string();
        if (name.starts_with("crash-") && entry.path().extension() == ".txt") {
            crash.report = readFile(entry.path());
            ++crash.reports;
        }
    }
    return crash;
}

bool endedAbnormally(int status)
{
    return WIFSIGNALED(status) || (WIFEXITED(status) && WEXITSTATUS(status) != 0);
}

struct Dir {
    fs::path path;
    explicit Dir(const char* name)
        : path(test::tempPath(name))
    {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~Dir()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

long numberAfter(const std::string& text, const std::string& key)
{
    const std::size_t at = text.find(key);
    return at == std::string::npos ? -1 : std::stol(text.substr(at + key.size()));
}

}

TEST_CASE("a crash before the logs folder exists creates it and leaves a backtrace with the build")
{
    const Dir dir("crash_main");
    const fs::path logs = dir.path / "logs";
    REQUIRE_FALSE(fs::exists(logs));
    const Crash crash = crashChild(dir.path, logs, CrashTest::MainThread);
    CHECK(endedAbnormally(crash.status));
    REQUIRE(crash.reports == 1);
    CHECK(crash.report.starts_with("Ichor build " + std::string(versionStamp()) + "\n"));
    CHECK(crash.report.find("SIGSEGV") != std::string::npos);
    const std::size_t backtrace = crash.report.find("backtrace:\n");
    REQUIRE(backtrace != std::string::npos);
    // A real backtrace has a line per frame: main, the test runner and crashForTest at least.
    const std::string frames = crash.report.substr(backtrace);
    CHECK(std::count(frames.begin(), frames.end(), '\n') >= 4);
    CHECK(numberAfter(crash.report, "thread ") == numberAfter(crash.report, "process "));
    CHECK(crash.err.find("[error] crash: SIGSEGV, report in ") != std::string::npos);
}

TEST_CASE("a crash on another thread is reported with that thread")
{
    const Dir dir("crash_thread");
    const Crash crash = crashChild(dir.path, dir.path / "logs", CrashTest::OtherThread);
    CHECK(endedAbnormally(crash.status));
    REQUIRE(crash.reports == 1);
    CHECK(crash.report.find("SIGSEGV") != std::string::npos);
    const long thread = numberAfter(crash.report, "thread ");
    CHECK(thread > 0);
    CHECK(thread != numberAfter(crash.report, "process "));
}

TEST_CASE("a second crash inside the handler ends the process and keeps the report")
{
    const Dir dir("crash_nested");
    const Crash crash = crashChild(dir.path, dir.path / "logs", CrashTest::InHandler);
    CHECK(endedAbnormally(crash.status));
    REQUIRE(crash.reports == 1);
    CHECK(crash.report.find("backtrace:\n") != std::string::npos);
}

TEST_CASE("a logs folder that cannot be written still ends the process and says so")
{
    const Dir dir("crash_unwritable");
    const fs::path logs = dir.path / "logs";
    std::ofstream(logs) << "a file where the folder should be";
    const Crash crash = crashChild(dir.path, logs, CrashTest::MainThread);
    CHECK(endedAbnormally(crash.status));
    CHECK(crash.err.find("[error] crash: SIGSEGV, the report could not be written under ") != std::string::npos);
    CHECK(readFile(logs) == "a file where the folder should be");
}

TEST_CASE("a stack overflow on the main thread is reported")
{
    const Dir dir("crash_overflow");
    const Crash crash = crashChild(dir.path, dir.path / "logs", CrashTest::StackOverflow);
    CHECK(endedAbnormally(crash.status));
    REQUIRE(crash.reports == 1);
    CHECK(crash.report.find("SIGSEGV") != std::string::npos);
}

#endif
