#include "ichor/world/world_io.h"

#include "ichor/core/binary.h"
#include "ichor/core/file_io.h"
#include "ichor/core/log.h"

#include <array>
#include <cstring>

namespace ichor {

namespace {

constexpr std::array<char, 4> kMagic{'I', 'W', 'L', 'D'};
constexpr std::uint32_t kVersion = 2;

}

bool writeWorld(const std::filesystem::path& directory, const Heightfield& heightfield)
{
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    BinaryWriter w;
    w.write(kMagic);
    w.write(kVersion);
    w.write(heightfield.desc());
    w.write(heightfield.digest());
    if (!writeFileAtomically(directory / "manifest.iworld", w.bytes())) {
        return false;
    }
    const auto& samples = heightfield.samples();
    std::vector<std::uint8_t> raw(samples.size() * sizeof(std::uint16_t));
    std::memcpy(raw.data(), samples.data(), raw.size());
    return writeFileAtomically(directory / "heightmap.r16", raw);
}

bool readWorld(const std::filesystem::path& directory, Heightfield& heightfield)
{
    std::vector<std::uint8_t> bytes;
    if (!readFileBytes(directory / "manifest.iworld", bytes)) {
        logf(LogLevel::Error, "no world manifest in {}", directory.string());
        return false;
    }
    BinaryReader r(bytes);
    std::array<char, 4> magic{};
    std::uint32_t version = 0;
    WorldDesc desc;
    std::uint64_t digest = 0;
    if (!r.read(magic) || magic != kMagic || !r.read(version) || version != kVersion || !r.read(desc) || !r.read(digest) || !r.atEnd() || !desc.valid()) {
        logf(LogLevel::Error, "{} is not a version {} world", directory.string(), kVersion);
        return false;
    }

    std::vector<std::uint8_t> raw;
    if (!readFileBytes(directory / "heightmap.r16", raw)) {
        logf(LogLevel::Error, "no heightmap in {}", directory.string());
        return false;
    }
    const std::size_t size = raw.size();
    const std::size_t expected = static_cast<std::size_t>(desc.samplesPerSide()) * desc.samplesPerSide() * sizeof(std::uint16_t);
    if (size != expected) {
        logf(LogLevel::Error, "heightmap in {} has {} bytes, the manifest expects {}", directory.string(), size, expected);
        return false;
    }
    std::vector<std::uint16_t> samples(size / sizeof(std::uint16_t));
    std::memcpy(samples.data(), raw.data(), size);
    heightfield = Heightfield(desc, std::move(samples));
    if (heightfield.digest() != digest) {
        logf(LogLevel::Error, "heightmap in {} does not match its manifest", directory.string());
        return false;
    }
    return true;
}

}
