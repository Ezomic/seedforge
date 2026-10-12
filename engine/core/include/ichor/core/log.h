#pragma once

#include <format>
#include <string_view>

namespace ichor {

enum class LogLevel { Debug, Info, Warn, Error };

void log(LogLevel level, std::string_view message);

template <class... Args>
void logf(LogLevel level, std::format_string<Args...> fmt, Args&&... args)
{
    log(level, std::format(fmt, std::forward<Args>(args)...));
}

}
