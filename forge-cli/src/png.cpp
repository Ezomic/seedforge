#include <forgecli/png.h>

#include <algorithm>
#include <array>
#include <cstddef>

namespace forgecli {

namespace {

constexpr int kMaxDimension = 1 << 15;
constexpr std::size_t kStoredBlockMax = 65535;

std::array<std::uint32_t, 256> makeCrcTable()
{
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t n = 0; n < 256; ++n) {
        std::uint32_t c = n;
        for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xedb88320u ^ (c >> 1) : c >> 1;
        table[n] = c;
    }
    return table;
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

void putChunk(std::vector<std::uint8_t>& out, const char (&type)[5], std::span<const std::uint8_t> data)
{
    put32(out, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    put32(out, crc32(body));
}

}

std::uint32_t crc32(std::span<const std::uint8_t> data, std::uint32_t crc)
{
    static const auto table = makeCrcTable();
    std::uint32_t c = ~crc;
    for (const std::uint8_t b : data) c = table[(c ^ b) & 0xffu] ^ (c >> 8);
    return ~c;
}

std::uint32_t adler32(std::span<const std::uint8_t> data)
{
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const std::uint8_t byte : data) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

std::optional<std::vector<std::uint8_t>> encodePng(int width, int height, int channels, std::span<const std::uint8_t> pixels)
{
    if (width < 1 || height < 1 || width > kMaxDimension || height > kMaxDimension) return std::nullopt;
    if (channels != 1 && channels != 3) return std::nullopt;
    const std::size_t rowBytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(channels);
    if (pixels.size() != rowBytes * static_cast<std::size_t>(height)) return std::nullopt;

    std::vector<std::uint8_t> raw;
    raw.reserve((rowBytes + 1) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        const auto row = pixels.subspan(static_cast<std::size_t>(y) * rowBytes, rowBytes);
        raw.insert(raw.end(), row.begin(), row.end());
    }

    std::vector<std::uint8_t> z = {0x78, 0x01};
    std::size_t pos = 0;
    do {
        const std::size_t n = std::min(kStoredBlockMax, raw.size() - pos);
        const bool last = pos + n == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(n & 0xffu));
        z.push_back(static_cast<std::uint8_t>(n >> 8));
        z.push_back(static_cast<std::uint8_t>(~n & 0xffu));
        z.push_back(static_cast<std::uint8_t>((~n >> 8) & 0xffu));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos), raw.begin() + static_cast<std::ptrdiff_t>(pos + n));
        pos += n;
    } while (pos < raw.size());
    put32(z, adler32(raw));

    std::vector<std::uint8_t> out = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    std::vector<std::uint8_t> ihdr;
    put32(ihdr, static_cast<std::uint32_t>(width));
    put32(ihdr, static_cast<std::uint32_t>(height));
    ihdr.push_back(8);
    ihdr.push_back(channels == 1 ? 0 : 2);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    putChunk(out, "IHDR", ihdr);
    putChunk(out, "IDAT", z);
    putChunk(out, "IEND", {});
    return out;
}

}
