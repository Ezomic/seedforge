#include "ichor/core/file_io.h"

#include "ichor/core/log.h"

#include <atomic>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace ichor {

namespace {

unsigned long processId()
{
#ifdef _WIN32
    return static_cast<unsigned long>(_getpid());
#else
    return static_cast<unsigned long>(getpid());
#endif
}

}

bool readFileBytes(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes, std::uintmax_t maxBytes)
{
    bytes.clear();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return false;
    }
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec || size > maxBytes) {
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    bytes.resize(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (static_cast<std::uintmax_t>(in.gcount()) != size) {
        bytes.clear();
        return false;
    }
    return true;
}

bool writeFileAtomically(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    static std::atomic<unsigned> counter{0};
    const std::filesystem::path temp = path.string() + "." + std::to_string(processId()) + "." + std::to_string(counter++) + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            logf(LogLevel::Error, "cannot write {}", temp.string());
            return false;
        }
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out) {
            logf(LogLevel::Error, "write failed for {}", temp.string());
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        logf(LogLevel::Error, "cannot replace {}: {}", path.string(), ec.message());
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

}
