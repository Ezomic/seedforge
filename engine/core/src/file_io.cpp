#include "ichor/core/file_io.h"

#include "ichor/core/log.h"

#include <fstream>

namespace ichor {

bool writeFileAtomically(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    const std::filesystem::path temp = path.string() + ".tmp";
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
