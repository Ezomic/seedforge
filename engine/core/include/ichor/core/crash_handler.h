#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace ichor {

// Installs the process-wide crash handler (SEED-698). On an unhandled crash it writes
// logs/crash-<start time>Z-<pid>.txt with the build stamp, the cause and the thread, plus a minidump
// (.dmp) on Windows or a backtrace in the .txt on Linux, and says where on stderr. The log is already
// flushed: ichor::log flushes every line. Calling it again only moves the folder. Not thread-safe:
// call it during startup, before other threads exist.
void installCrashHandler(const std::filesystem::path& logs);

enum class CrashTest {
    MainThread,    // an access violation on the calling thread
    OtherThread,   // the same on a new thread
    InHandler,     // the handler itself crashes once the report is written
    StackOverflow, // unbounded recursion on the calling thread
};

// "--crash-test" values: "thread", "handler", "overflow". The main-thread crash has no name: it is the bare flag.
std::optional<CrashTest> parseCrashTest(std::string_view name);

[[noreturn]] void crashForTest(CrashTest kind);

}
