#include "ichor/core/crash_handler.h"

#include <cstdlib>

namespace ichor {

void installCrashHandler(const std::filesystem::path&) {}

std::optional<CrashTest> parseCrashTest(std::string_view)
{
    return std::nullopt;
}

void crashForTest(CrashTest)
{
    std::abort();
}

}
