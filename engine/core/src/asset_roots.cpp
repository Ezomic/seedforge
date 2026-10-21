#include "ichor/core/asset_roots.h"

#include <string>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ichor {

namespace {

bool isDirectory(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
}

}

std::optional<AssetRoots> packagedAssetRoots(const std::filesystem::path& exeDir)
{
    if (exeDir.empty()) {
        return std::nullopt;
    }
    AssetRoots roots{exeDir / "content", exeDir / "shaders", exeDir / "scripts", exeDir / "data", exeDir / "art"};
    if (!isDirectory(roots.content) || !isDirectory(roots.shaders) || !isDirectory(roots.data)) {
        return std::nullopt;
    }
    return roots;
}

std::filesystem::path executableDirectory()
{
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return {};
    }
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
#else
    std::error_code ec;
    const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path{} : exe.parent_path();
#endif
}

}
