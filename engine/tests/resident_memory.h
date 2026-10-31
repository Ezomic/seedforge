#pragma once

#include <cstdint>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#elif defined(__linux__)
#include <cstdio>
#include <unistd.h>
#endif

namespace ichor::test {

// The bytes of this process that are in physical memory right now: the working set on Windows, the resident set
// (VmRSS) on Linux. 0 where the platform gives no number, which a caller treats as "not measured", never as a pass.
inline std::uint64_t residentBytes()
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        return 0;
    }
    return static_cast<std::uint64_t>(counters.WorkingSetSize);
#elif defined(__linux__)
    std::FILE* statm = std::fopen("/proc/self/statm", "r");
    if (statm == nullptr) {
        return 0;
    }
    unsigned long size = 0;
    unsigned long resident = 0;
    const int fields = std::fscanf(statm, "%lu %lu", &size, &resident);
    std::fclose(statm);
    if (fields != 2) {
        return 0;
    }
    return static_cast<std::uint64_t>(resident) * static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
#else
    return 0;
#endif
}

}
