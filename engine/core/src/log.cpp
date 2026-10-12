#include "ichor/core/log.h"

#include <cstdio>

namespace ichor {

namespace {

const char* prefix(LogLevel level)
{
    switch (level) {
    case LogLevel::Debug: return "debug";
    case LogLevel::Info: return "info ";
    case LogLevel::Warn: return "warn ";
    case LogLevel::Error: return "error";
    }
    return "?";
}

}

void log(LogLevel level, std::string_view message)
{
    std::FILE* out = level >= LogLevel::Warn ? stderr : stdout;
    std::fprintf(out, "[%s] %.*s\n", prefix(level), static_cast<int>(message.size()), message.data());
    std::fflush(out);
}

}
