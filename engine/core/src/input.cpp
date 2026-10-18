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
constexpr std::uint32_t kVersion = 2;
constexpr std::uint32_t kLegacyVersion = 1; // buttons, moveX, moveY per frame

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
    legacyControls = false;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        logf(LogLevel::Error, "cannot open {}", path.string());
        return false;
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    BinaryReader r(bytes);
    std::array<char, 4> magic{};
    std::uint32_t version = 0;
    bool ok = r.read(magic) && magic == kMagic && r.read(version);
    if (ok && version == kLegacyVersion) {
        std::uint32_t count = 0;
        constexpr std::size_t kLegacyFrameBytes = sizeof(std::uint32_t) + 2 * sizeof(float);
        ok = r.read(count) && count <= r.remaining() / kLegacyFrameBytes;
        frames.clear();
        frames.resize(ok ? count : 0);
        for (InputFrame& frame : frames) {
            ok = ok && r.read(frame.buttons) && r.read(frame.moveX) && r.read(frame.moveY);
        }
        legacyControls = ok;
    } else {
        ok = ok && version == kVersion && r.read(frames);
    }
    if (!ok || !r.atEnd()) {
        logf(LogLevel::Error, "{} is not a version {} or {} input stream", path.string(), kLegacyVersion, kVersion);
        frames.clear();
        legacyControls = false;
        return false;
    }
    return true;
}

}
