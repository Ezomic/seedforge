#pragma once

#include <filesystem>
#include <string>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace ichor::test {

// Test processes can run in parallel (ctest -j, several builds), so every temp path needs the process id.
inline std::filesystem::path tempPath(const std::filesystem::path& name)
{
#ifdef _WIN32
    const auto pid = static_cast<unsigned long>(_getpid());
#else
    const auto pid = static_cast<unsigned long>(getpid());
#endif
    return std::filesystem::temp_directory_path() / (name.stem().string() + "_" + std::to_string(pid) + name.extension().string());
}

inline std::filesystem::path outputPath(const std::filesystem::path& base, const std::filesystem::path& name)
{
#ifdef _WIN32
    const auto pid = static_cast<unsigned long>(_getpid());
#else
    const auto pid = static_cast<unsigned long>(getpid());
#endif
    return base / (name.string() + "_" + std::to_string(pid));
}

} // namespace ichor::test
