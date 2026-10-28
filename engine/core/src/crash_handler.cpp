#include "ichor/core/crash_handler.h"

#include "ichor/core/version.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <format>
#include <iterator>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
// dbghelp.h needs windows.h first
#include <dbghelp.h>
#else
#include <csignal>
#include <execinfo.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace ichor {

namespace {

// The handler formats into fixed buffers: after a crash the heap may be what broke.
struct Text {
    char data[1024];
    std::size_t size = 0;

    Text& add(std::string_view text)
    {
        const std::size_t n = std::min(text.size(), sizeof(data) - size);
        std::memcpy(data + size, text.data(), n);
        size += n;
        return *this;
    }
    Text& number(std::uint64_t value, unsigned base = 10)
    {
        char digits[24];
        int count = 0;
        do {
            digits[count++] = "0123456789abcdef"[value % base];
            value /= base;
        } while (value != 0);
        if (base == 16) add("0x");
        while (count > 0) add({&digits[--count], 1});
        return *this;
    }
};

// Everything the handler needs, filled in at install so the handler only copies bytes.
struct Paths {
    char build[64]{};
    char logsUtf8[1024]{};
    char reportUtf8[1024]{};
#ifdef _WIN32
    wchar_t logs[1024]{};
    wchar_t report[1024]{};
    wchar_t dump[1024]{};
#endif
};

Paths paths;
// The thread writing the report, 0 while none is. A second crash on that thread ends the process at once;
// one on another thread waits, so the first report is finished before the process ends.
std::atomic<std::uint64_t> handlingThread{0};
std::atomic<bool> crashInHandler{false};

template <class Char>
bool copyPath(Char (&out)[1024], const std::basic_string<Char>& in)
{
    if (in.size() >= 1024) {
        out[0] = 0;
        return false;
    }
    std::memcpy(out, in.c_str(), (in.size() + 1) * sizeof(Char));
    return true;
}

std::string utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

void storePaths(const std::filesystem::path& logs)
{
    const std::filesystem::path dir = std::filesystem::absolute(logs).lexically_normal();
    const auto started = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
#ifdef _WIN32
    const unsigned long pid = GetCurrentProcessId();
#else
    const unsigned long pid = static_cast<unsigned long>(getpid());
#endif
    const std::filesystem::path report = dir / std::format("crash-{:%Y%m%d-%H%M%S}Z-{}.txt", started, pid);
    copyPath(paths.logsUtf8, utf8(dir));
    copyPath(paths.reportUtf8, utf8(report));
#ifdef _WIN32
    copyPath(paths.logs, dir.wstring());
    copyPath(paths.report, report.wstring());
    copyPath(paths.dump, std::filesystem::path(report).replace_extension(".dmp").wstring());
#endif
    const std::string_view build = versionStamp();
    std::memcpy(paths.build, build.data(), std::min(build.size(), sizeof(paths.build) - 1));
}

void crashHere()
{
    // Not null, so UBSan has no null store to report before the fault itself.
    *reinterpret_cast<volatile int*>(std::uintptr_t{16}) = 1;
}

volatile bool stopRecursing = false;

#ifdef _MSC_VER
__declspec(noinline)
#else
[[gnu::noinline]]
#endif
int recurse(volatile char* previous)
{
    volatile char frame[4096];
    frame[0] = previous ? static_cast<char>(previous[0] + 1) : 0;
    if (stopRecursing) return frame[0];
    return recurse(frame) + frame[1];
}

#ifdef _WIN32

HANDLE stderrHandle() { return GetStdHandle(STD_ERROR_HANDLE); }

void writeAll(HANDLE file, const Text& text)
{
    DWORD written = 0;
    WriteFile(file, text.data, static_cast<DWORD>(text.size), &written, nullptr);
}

// Loaded at install rather than linked: the dependency policy only links vcpkg packages, and a library
// loaded before the crash is not loaded from inside it.
decltype(&MiniDumpWriteDump) writeMiniDump = nullptr;
EXCEPTION_POINTERS* crashPointers = nullptr;
DWORD crashThread = 0;

bool writeDump()
{
    if (!writeMiniDump) return false;
    const HANDLE file = CreateFileW(paths.dump, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    MINIDUMP_EXCEPTION_INFORMATION exception{crashThread, crashPointers, FALSE};
    char comment[80] = "Ichor build ";
    strncat_s(comment, paths.build, _TRUNCATE);
    MINIDUMP_USER_STREAM stream{CommentStreamA, static_cast<ULONG>(std::strlen(comment) + 1), comment};
    MINIDUMP_USER_STREAM_INFORMATION streams{1, &stream};
    const BOOL ok = writeMiniDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal, &exception, &streams, nullptr);
    CloseHandle(file);
    return ok != FALSE;
}

DWORD WINAPI dumpThread(void*)
{
    return writeDump() ? 0 : 1;
}

// The dump is written from a thread of its own: after a stack overflow the crashed thread has too little
// stack left for MiniDumpWriteDump.
bool writeDumpFromOtherThread()
{
    const HANDLE thread = CreateThread(nullptr, 256 * 1024, dumpThread, nullptr, 0, nullptr);
    if (thread == nullptr) return false;
    DWORD result = 1;
    if (WaitForSingleObject(thread, 20'000) == WAIT_OBJECT_0) GetExitCodeThread(thread, &result);
    CloseHandle(thread);
    return result == 0;
}

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* info)
{
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    std::uint64_t none = 0;
    if (!handlingThread.compare_exchange_strong(none, GetCurrentThreadId())) {
        if (none == GetCurrentThreadId()) TerminateProcess(GetCurrentProcess(), code);
        Sleep(INFINITE);
    }
    crashPointers = info;
    crashThread = GetCurrentThreadId();

    Text cause;
    cause.add("exception ").number(code, 16);
    Text report;
    report.add("Ichor build ").add(paths.build).add("\n").add({cause.data, cause.size});
    report.add(" at address ").number(reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress), 16).add("\n");
    report.add("thread ").number(crashThread).add(", process ").number(GetCurrentProcessId()).add("\n");

    CreateDirectoryW(paths.logs, nullptr);
    const HANDLE file = paths.report[0] ? CreateFileW(paths.report, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr) : INVALID_HANDLE_VALUE;
    Text line;
    line.add("[error] crash: ").add({cause.data, cause.size});
    if (file != INVALID_HANDLE_VALUE) {
        const bool dumped = writeDumpFromOtherThread();
        report.add(dumped ? "minidump beside this file\n" : "the minidump could not be written\n");
        writeAll(file, report);
        CloseHandle(file);
        line.add(", report in ").add(paths.reportUtf8).add("\n");
    } else {
        line.add(", the report could not be written under ").add(paths.logsUtf8).add("\n");
    }
    writeAll(stderrHandle(), line);
    if (crashInHandler) crashHere();
    return EXCEPTION_EXECUTE_HANDLER;
}

// The previous filter is not chained: it is the CRT's, whose path for an uncaught C++ exception ends in
// abort() and can raise the Windows Error Reporting dialog. MSVC's ASan uses a vectored handler instead.
void install()
{
    if (const HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll")) {
        writeMiniDump = reinterpret_cast<decltype(&MiniDumpWriteDump)>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));
    }
    SetUnhandledExceptionFilter(onUnhandledException);
}

#else

constexpr int kSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
struct sigaction previous[std::size(kSignals)];

const char* signalName(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS: return "SIGBUS";
    case SIGILL: return "SIGILL";
    case SIGFPE: return "SIGFPE";
    case SIGABRT: return "SIGABRT";
    }
    return "signal";
}

void writeAll(int fd, const Text& text)
{
    for (std::size_t done = 0; done < text.size;) {
        const ssize_t n = write(fd, text.data + done, text.size - done);
        if (n <= 0) return;
        done += static_cast<std::size_t>(n);
    }
}

// Hands the signal to whoever had it before (a sanitizer reports it; otherwise the default action ends
// the process), so ASan and UBSan reports are unchanged.
void chain(int sig, siginfo_t* info, void* context)
{
    for (std::size_t i = 0; i < std::size(kSignals); ++i) {
        if (kSignals[i] != sig) continue;
        sigaction(sig, &previous[i], nullptr);
        if (previous[i].sa_flags & SA_SIGINFO) {
            if (previous[i].sa_sigaction) previous[i].sa_sigaction(sig, info, context);
        } else if (previous[i].sa_handler != SIG_DFL && previous[i].sa_handler != SIG_IGN) {
            previous[i].sa_handler(sig);
        }
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

// Async-signal-safe apart from backtrace(), which glibc does not promise: its first call loads libgcc_s
// and allocates, so install() calls it once to do that outside the handler.
void onSignal(int sig, siginfo_t* info, void* context)
{
    const auto self = static_cast<std::uint64_t>(syscall(SYS_gettid));
    std::uint64_t none = 0;
    if (!handlingThread.compare_exchange_strong(none, self)) {
        if (none == self) {
            signal(sig, SIG_DFL);
            raise(sig);
            return;
        }
        for (;;) pause();
    }
    Text cause;
    cause.add(signalName(sig));
    Text report;
    report.add("Ichor build ").add(paths.build).add("\n").add({cause.data, cause.size}).add(" (signal ").number(static_cast<std::uint64_t>(sig)).add(")");
    report.add(" at address ").number(reinterpret_cast<std::uintptr_t>(info->si_addr), 16).add("\n");
    report.add("thread ").number(self).add(", process ").number(static_cast<std::uint64_t>(getpid())).add("\n");
    report.add("backtrace:\n");

    mkdir(paths.logsUtf8, 0755);
    const int fd = paths.reportUtf8[0] ? open(paths.reportUtf8, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644) : -1;
    Text line;
    line.add("[error] crash: ").add({cause.data, cause.size});
    if (fd >= 0) {
        writeAll(fd, report);
        void* frames[64];
        backtrace_symbols_fd(frames, backtrace(frames, 64), fd);
        close(fd);
        line.add(", report in ").add(paths.reportUtf8).add("\n");
    } else {
        line.add(", the report could not be written under ").add(paths.logsUtf8).add("\n");
    }
    writeAll(STDERR_FILENO, line);
    if (crashInHandler) crashHere();
    chain(sig, info, context);
}

void install()
{
    void* frames[4];
    backtrace(frames, 4);

    // Only the installing thread gets a stack to report its own overflow on. It replaces a sanitizer's or
    // Catch2's smaller one, so the handler and the one it chains to both fit.
    static char altStack[256 * 1024];
    stack_t stack{};
    stack.ss_sp = altStack;
    stack.ss_size = sizeof(altStack);
    sigaltstack(&stack, nullptr);
    struct sigaction action{};
    action.sa_sigaction = onSignal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (std::size_t i = 0; i < std::size(kSignals); ++i) {
        sigaction(kSignals[i], &action, &previous[i]);
    }
}

#endif

bool installed = false;

}

void installCrashHandler(const std::filesystem::path& logs)
{
    storePaths(logs);
    if (!installed) {
        install();
        installed = true;
    }
}

std::optional<CrashTest> parseCrashTest(std::string_view name)
{
    if (name == "thread") return CrashTest::OtherThread;
    if (name == "handler") return CrashTest::InHandler;
    if (name == "overflow") return CrashTest::StackOverflow;
    return std::nullopt;
}

void crashForTest(CrashTest kind)
{
    switch (kind) {
    case CrashTest::MainThread: crashHere(); break;
    case CrashTest::OtherThread: std::thread(crashHere).join(); break;
    case CrashTest::InHandler:
        crashInHandler = true;
        crashHere();
        break;
    case CrashTest::StackOverflow: recurse(nullptr); break;
    }
    std::abort();
}

}
