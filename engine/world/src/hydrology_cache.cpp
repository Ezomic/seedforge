#include "ichor/world/hydrology_cache.h"

#include "ichor/core/binary.h"
#include "ichor/core/hash.h"
#include "ichor/core/log.h"

#include <array>
#include <atomic>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string_view>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace ichor {

namespace {

constexpr std::array<char, 4> kMagic{'I', 'H', 'Y', 'D'};
constexpr std::uint64_t kSumBytes = sizeof(std::uint64_t);

std::uint64_t checksum(const std::uint8_t* data, std::size_t size)
{
    return hash64(std::string_view(reinterpret_cast<const char*>(data), size));
}

std::vector<std::uint8_t> descBytes(const WorldDesc& desc)
{
    BinaryWriter w;
    w.write(desc);
    return w.take();
}

bool structurallySound(const Hydrology& h, std::uint32_t side)
{
    const std::size_t total = static_cast<std::size_t>(side) * side;
    if (h.side != side || h.filled.size() != total || h.direction.size() != total || h.accumulation.size() != total || h.lake.size() != total) {
        return false;
    }
    constexpr std::array<int, 8> dx{1, 1, 0, -1, -1, -1, 0, 1};
    constexpr std::array<int, 8> dz{0, 1, 1, 1, 0, -1, -1, -1};
    for (std::size_t i = 0; i < total; ++i) {
        const std::uint8_t d = h.direction[i];
        if (h.accumulation[i] < 1 || h.accumulation[i] > total || !(h.filled[i] == h.filled[i])) {
            return false;
        }
        if (d == Hydrology::kOutlet) {
            continue;
        }
        const auto nx = static_cast<std::int64_t>(i % side) + (d < 8 ? dx[d] : 0);
        const auto nz = static_cast<std::int64_t>(i / side) + (d < 8 ? dz[d] : 0);
        if (d > 7 || nx < 0 || nz < 0 || nx >= side || nz >= side) {
            return false;
        }
    }
    for (const float depth : h.lakeDeepest) {
        if (!(depth == depth)) {
            return false;
        }
    }
    for (const std::uint32_t id : h.lake) {
        if (id > h.lakeDeepest.size()) {
            return false;
        }
    }
    return true;
}

std::filesystem::path uniqueTemp(const std::filesystem::path& path)
{
    static std::atomic<unsigned> counter{0};
#ifdef _WIN32
    const auto pid = static_cast<unsigned long>(_getpid());
#else
    const auto pid = static_cast<unsigned long>(getpid());
#endif
    return path.string() + "." + std::to_string(pid) + "." + std::to_string(counter++) + ".tmp";
}

bool writeAtomically(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    const std::filesystem::path temp = uniqueTemp(path);
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out) {
            out.close();
            std::error_code ignored;
            std::filesystem::remove(temp, ignored);
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

}

std::vector<std::uint8_t> encodeHydrologyCache(const Heightfield& heightfield, const Hydrology& hydrology)
{
    BinaryWriter w;
    w.write(kMagic);
    w.write(kHydrologyCacheVersion);
    w.write(heightfield.desc());
    w.write(heightfield.digest());
    w.write(hydrology.side);
    w.write(hydrology.filled);
    w.write(hydrology.direction);
    w.write(hydrology.accumulation);
    w.write(hydrology.lake);
    w.write(hydrology.lakeDeepest);
    std::vector<std::uint8_t> bytes = w.take();
    const std::uint64_t sum = checksum(bytes.data(), bytes.size());
    BinaryWriter tail;
    tail.write(sum);
    bytes.insert(bytes.end(), tail.bytes().begin(), tail.bytes().end());
    return bytes;
}

bool decodeHydrologyCache(const std::vector<std::uint8_t>& bytes, const Heightfield& heightfield, Hydrology& out)
{
    out = Hydrology{};
    if (heightfield.empty() || bytes.size() < kSumBytes) {
        return false;
    }
    const std::size_t bodySize = bytes.size() - kSumBytes;
    std::uint64_t stored = 0;
    std::memcpy(&stored, bytes.data() + bodySize, kSumBytes);
    if (stored != checksum(bytes.data(), bodySize)) {
        return false;
    }

    BinaryReader r(std::span<const std::uint8_t>(bytes.data(), bodySize));
    std::array<char, 4> magic{};
    std::uint32_t version = 0;
    WorldDesc desc;
    std::uint64_t digest = 0;
    Hydrology h;
    if (!r.read(magic) || magic != kMagic || !r.read(version) || version != kHydrologyCacheVersion || !r.read(desc) || descBytes(desc) != descBytes(heightfield.desc())
        || !r.read(digest) || digest != heightfield.digest() || !r.read(h.side) || !r.read(h.filled) || !r.read(h.direction) || !r.read(h.accumulation)
        || !r.read(h.lake) || !r.read(h.lakeDeepest) || !r.atEnd() || !structurallySound(h, heightfield.desc().samplesPerSide())) {
        return false;
    }
    out = std::move(h);
    return true;
}

Hydrology loadOrComputeHydrology(const std::filesystem::path& directory, const Heightfield& heightfield)
{
    const std::filesystem::path path = directory / "hydrology.ihyd";
    {
        std::error_code ec;
        std::ifstream in;
        if (std::filesystem::is_regular_file(path, ec)) {
            in.open(path, std::ios::binary);
        }
        if (in) {
            const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            Hydrology cached;
            if (decodeHydrologyCache(bytes, heightfield, cached)) {
                return cached;
            }
            logf(LogLevel::Warn, "{} is stale or damaged; computing the hydrology again", path.string());
        }
    }
    Hydrology computed = computeHydrology(heightfield);
    if (!computed.empty() && !writeAtomically(path, encodeHydrologyCache(heightfield, computed))) {
        logf(LogLevel::Warn, "cannot write {}; the hydrology will be computed again next run", path.string());
    }
    return computed;
}

}
