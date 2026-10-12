#include "ichor/core/input.h"

#include "ichor/core/binary.h"
#include "ichor/core/file_io.h"
#include "ichor/core/log.h"

#include <array>
#include <fstream>
#include <iterator>

namespace ichor {

namespace {

constexpr std::array<char, 4> kMagic{'I', 'I', 'N', 'P'};
constexpr std::uint32_t kVersion = 1;

}

bool InputStream::save(const std::filesystem::path& path) const
{
    BinaryWriter w;
    w.write(kMagic);
    w.write(kVersion);
    w.write(frames);
    return writeFileAtomically(path, w.bytes());
}

bool InputStream::load(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        logf(LogLevel::Error, "cannot open {}", path.string());
        return false;
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    BinaryReader r(bytes);
    std::array<char, 4> magic{};
    std::uint32_t version = 0;
    if (!r.read(magic) || magic != kMagic || !r.read(version) || version != kVersion || !r.read(frames) || !r.atEnd()) {
        logf(LogLevel::Error, "{} is not a version {} input stream", path.string(), kVersion);
        frames.clear();
        return false;
    }
    return true;
}

}
