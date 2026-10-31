#include "ichor/assets/texture_cook.h"

#include "ichor/core/log.h"

#include <bc7decomp.h>
#include <bc7enc.h>
#include <nlohmann/json.hpp>
#include <rgbcx.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <mutex>
#include <thread>

namespace ichor {

namespace {

std::string generic(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

void initEncoders()
{
    static std::once_flag once;
    std::call_once(once, [] {
        bc7enc_compress_block_init();
        rgbcx::init();
    });
}

// Runs `rowTask(first, last)` over block rows [0, rows), on several threads when the level is big enough to pay for them.
// Every block is encoded on its own from its own pixels, so the bytes do not depend on how the rows are split.
template <class RowTask>
void forBlockRows(std::uint32_t rows, std::uint32_t blocksPerRow, const RowTask& rowTask)
{
    const std::uint32_t hardware = std::max(1u, std::thread::hardware_concurrency());
    const std::uint32_t workers = std::min({hardware, rows, 16u});
    if (workers <= 1 || static_cast<std::uint64_t>(rows) * blocksPerRow < 256) {
        rowTask(0u, rows);
        return;
    }
    std::vector<std::thread> threads;
    const std::uint32_t per = (rows + workers - 1) / workers;
    for (std::uint32_t first = 0; first < rows; first += per) {
        threads.emplace_back([&rowTask, first, last = std::min(rows, first + per)] { rowTask(first, last); });
    }
    for (std::thread& t : threads) {
        t.join();
    }
}

// The 16 pixels of block (bx, by), edge pixels repeated where the block hangs over the level.
void gatherBlock(const std::uint8_t* rgba, std::uint32_t w, std::uint32_t h, std::uint32_t bx, std::uint32_t by, std::uint8_t out[64])
{
    for (std::uint32_t y = 0; y < 4; ++y) {
        const std::uint32_t sy = std::min(by * 4 + y, h - 1);
        for (std::uint32_t x = 0; x < 4; ++x) {
            const std::uint32_t sx = std::min(bx * 4 + x, w - 1);
            std::copy_n(rgba + (static_cast<std::size_t>(sy) * w + sx) * 4, 4, out + (y * 4 + x) * 4);
        }
    }
}

void encodeLevel(const std::uint8_t* rgba, std::uint32_t w, std::uint32_t h, TextureFormat format, std::uint8_t* dst)
{
    const std::uint32_t bw = (w + 3) / 4;
    const std::uint32_t bh = (h + 3) / 4;
    const std::uint32_t unit = texelUnitBytes(format);

    bc7enc_compress_block_params params;
    bc7enc_compress_block_params_init(&params);
    // The game cooks its art at launch and the tests cook it on every run, so the encoder is set to a third of its
    // default time (4 partitions, no least-squares pass). On the local art that costs about 0.8 dB of mean PSNR.
    params.m_max_partitions_mode = 4;
    params.m_try_least_squares = BC7ENC_FALSE;
    if (format == TextureFormat::Bc7Unorm) {
        bc7enc_compress_block_params_init_linear_weights(&params);
    }

    forBlockRows(bh, bw, [&](std::uint32_t first, std::uint32_t last) {
        std::uint8_t block[64];
        for (std::uint32_t by = first; by < last; ++by) {
            for (std::uint32_t bx = 0; bx < bw; ++bx) {
                gatherBlock(rgba, w, h, bx, by, block);
                void* out = dst + (static_cast<std::size_t>(by) * bw + bx) * unit;
                switch (format) {
                case TextureFormat::Bc7Srgb:
                case TextureFormat::Bc7Unorm: bc7enc_compress_block(out, block, &params); break;
                case TextureFormat::Bc5Unorm: rgbcx::encode_bc5(out, block, 0, 1, 4); break;
                default: rgbcx::encode_bc4(out, block, 4); break;
                }
            }
        }
    });
}

void decodeBc4Block(const std::uint8_t* b, std::uint8_t out[16])
{
    int ramp[8] = {b[0], b[1]};
    if (ramp[0] > ramp[1]) {
        for (int i = 1; i <= 6; ++i) ramp[i + 1] = ((7 - i) * ramp[0] + i * ramp[1] + 3) / 7;
    } else {
        for (int i = 1; i <= 4; ++i) ramp[i + 1] = ((5 - i) * ramp[0] + i * ramp[1] + 2) / 5;
        ramp[6] = 0;
        ramp[7] = 255;
    }
    std::uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= static_cast<std::uint64_t>(b[2 + i]) << (8 * i);
    for (int i = 0; i < 16; ++i) out[i] = static_cast<std::uint8_t>(ramp[(bits >> (3 * i)) & 7]);
}

std::uint8_t rebuildZ(std::uint8_t r, std::uint8_t g)
{
    const float x = static_cast<float>(r) / 127.5f - 1.0f;
    const float y = static_cast<float>(g) / 127.5f - 1.0f;
    const float z = std::sqrt(std::max(0.0f, 1.0f - x * x - y * y));
    return static_cast<std::uint8_t>(std::lround(std::clamp(z * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f));
}

bool decodeBlock(const std::uint8_t* block, TextureFormat format, std::uint8_t out[64])
{
    switch (format) {
    case TextureFormat::Bc7Srgb:
    case TextureFormat::Bc7Unorm: {
        bc7decomp::color_rgba pixels[16];
        if (!bc7decomp::unpack_bc7(block, pixels)) {
            return false;
        }
        for (int i = 0; i < 16; ++i) std::copy_n(pixels[i].m_comps, 4, out + i * 4);
        return true;
    }
    case TextureFormat::Bc5Unorm: {
        std::uint8_t r[16];
        std::uint8_t g[16];
        decodeBc4Block(block, r);
        decodeBc4Block(block + 8, g);
        for (int i = 0; i < 16; ++i) {
            out[i * 4 + 0] = r[i];
            out[i * 4 + 1] = g[i];
            out[i * 4 + 2] = rebuildZ(r[i], g[i]);
            out[i * 4 + 3] = 255;
        }
        return true;
    }
    default: {
        std::uint8_t r[16];
        decodeBc4Block(block, r);
        for (int i = 0; i < 16; ++i) {
            out[i * 4 + 0] = out[i * 4 + 1] = out[i * 4 + 2] = r[i];
            out[i * 4 + 3] = 255;
        }
        return true;
    }
    }
}

TextureFormat decodedFormat(TextureFormat format)
{
    return format == TextureFormat::Bc7Srgb ? TextureFormat::Rgba8Srgb : TextureFormat::Rgba8Unorm;
}

}

bool loadTextureBudget(const std::filesystem::path& file, TextureBudget& budget, std::vector<std::string>& problems)
{
    std::ifstream in(file);
    if (!in) {
        problems.push_back(file.string() + ": cannot open");
        return false;
    }
    const nlohmann::json root = nlohmann::json::parse(in, nullptr, false);
    if (!root.is_object()) {
        problems.push_back(file.string() + ": not a JSON object");
        return false;
    }
    const auto validSize = [](const nlohmann::json& v) {
        return v.is_number_integer() && v.get<std::int64_t>() >= 1 && v.get<std::int64_t>() <= kMaxTextureEdge;
    };
    if (const auto it = root.find("default"); it != root.end()) {
        if (validSize(*it)) {
            budget.defaultMax = it->get<std::uint32_t>();
        } else {
            problems.push_back("default must be a whole number from 1 to 16384");
        }
    }
    if (const auto it = root.find("prefixes"); it != root.end() && it->is_array()) {
        for (std::size_t i = 0; i < it->size(); ++i) {
            const nlohmann::json& entry = (*it)[i];
            const auto prefix = entry.is_object() && entry.contains("prefix") ? entry["prefix"] : nlohmann::json();
            const auto max = entry.is_object() && entry.contains("max") ? entry["max"] : nlohmann::json();
            if (!prefix.is_string() || prefix.get<std::string>().empty() || !validSize(max)) {
                problems.push_back("prefixes[" + std::to_string(i) + "] needs a non-empty \"prefix\" and a \"max\" from 1 to 16384");
                continue;
            }
            std::string text = generic(prefix.get<std::string>());
            if (text.back() != '/') {
                text += '/';
            }
            budget.entries.push_back({std::move(text), max.get<std::uint32_t>()});
        }
    } else if (it != root.end()) {
        problems.push_back("prefixes must be an array");
    }
    return true;
}

std::uint32_t maxTextureSize(const TextureBudget& budget, const std::filesystem::path& sourcePath)
{
    const std::string path = generic(sourcePath.generic_string());
    std::vector<std::string_view> candidates{path};
    for (std::size_t at = path.find("art/"); at != std::string::npos; at = path.find("art/", at + 1)) {
        if (at == 0 || path[at - 1] == '/') {
            candidates.emplace_back(std::string_view(path).substr(at));
        }
    }
    std::size_t bestLength = 0;
    std::uint32_t best = budget.defaultMax;
    for (const std::string_view candidate : candidates) {
        for (const TextureBudget::Entry& entry : budget.entries) {
            if (entry.prefix.size() > bestLength && candidate.starts_with(entry.prefix)) {
                bestLength = entry.prefix.size();
                best = entry.maxSize;
            }
        }
    }
    return best;
}

TextureFormat chooseTextureFormat(TextureUse use, bool singleChannelSource)
{
    switch (use) {
    case TextureUse::Color: return TextureFormat::Bc7Srgb;
    case TextureUse::Normal: return TextureFormat::Bc5Unorm;
    default: return singleChannelSource ? TextureFormat::Bc4Unorm : TextureFormat::Bc7Unorm;
    }
}

bool compressTexture(const Texture& rgba8, TextureFormat format, Texture& out)
{
    if (!isBlockCompressed(format) || isBlockCompressed(rgba8.format) || rgba8.width == 0 || rgba8.height == 0
        || rgba8.mipCount != mipLevelCount(rgba8.width, rgba8.height)
        || rgba8.pixels.size() != textureByteSize(rgba8.format, rgba8.width, rgba8.height, rgba8.mipCount)) {
        return false;
    }
    initEncoders();
    Texture result;
    result.name = rgba8.name;
    result.width = rgba8.width;
    result.height = rgba8.height;
    result.mipCount = rgba8.mipCount;
    result.format = format;
    result.pixels.resize(static_cast<std::size_t>(textureByteSize(format, rgba8.width, rgba8.height, rgba8.mipCount)));

    const std::uint8_t* src = rgba8.pixels.data();
    std::uint8_t* dst = result.pixels.data();
    for (std::uint32_t w = rgba8.width, h = rgba8.height, mip = 0; mip < rgba8.mipCount; ++mip) {
        encodeLevel(src, w, h, format, dst);
        src += mipByteSize(rgba8.format, w, h);
        dst += mipByteSize(format, w, h);
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }
    out = std::move(result);
    return true;
}

bool cookCompressedTexture(DecodedImage image, TextureFormat format, std::uint32_t maxSize, Texture& out)
{
    if (!isBlockCompressed(format)) {
        return false;
    }
    const bool srgb = format == TextureFormat::Bc7Srgb;
    shrinkToFit(image, std::max(1u, maxSize), srgb);
    Texture chain;
    buildMipChain(image, srgb ? TextureFormat::Rgba8Srgb : TextureFormat::Rgba8Unorm, chain);
    return compressTexture(chain, format, out);
}

bool decompressTexture(const Texture& compressed, Texture& out)
{
    if (!isBlockCompressed(compressed.format) || !isTextureLayoutValid(compressed)) {
        return false;
    }
    Texture result;
    result.name = compressed.name;
    result.width = compressed.width;
    result.height = compressed.height;
    result.mipCount = compressed.mipCount;
    result.format = decodedFormat(compressed.format);
    result.pixels.resize(static_cast<std::size_t>(textureByteSize(result.format, result.width, result.height, result.mipCount)));

    const std::uint8_t* src = compressed.pixels.data();
    std::uint8_t* dst = result.pixels.data();
    const std::uint32_t unit = texelUnitBytes(compressed.format);
    for (std::uint32_t w = compressed.width, h = compressed.height, mip = 0; mip < compressed.mipCount; ++mip) {
        const std::uint32_t bw = (w + 3) / 4;
        const std::uint32_t bh = (h + 3) / 4;
        for (std::uint32_t by = 0; by < bh; ++by) {
            for (std::uint32_t bx = 0; bx < bw; ++bx) {
                std::uint8_t block[64];
                if (!decodeBlock(src + (static_cast<std::size_t>(by) * bw + bx) * unit, compressed.format, block)) {
                    return false;
                }
                for (std::uint32_t y = 0; y < 4 && by * 4 + y < h; ++y) {
                    for (std::uint32_t x = 0; x < 4 && bx * 4 + x < w; ++x) {
                        std::copy_n(block + (y * 4 + x) * 4, 4, dst + (static_cast<std::size_t>(by * 4 + y) * w + bx * 4 + x) * 4);
                    }
                }
            }
        }
        src += mipByteSize(compressed.format, w, h);
        dst += mipByteSize(result.format, w, h);
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }
    out = std::move(result);
    return true;
}

const Texture* textureForDevice(const Texture& texture, bool blockCompressionSupported, Texture& scratch)
{
    if (!isBlockCompressed(texture.format) || blockCompressionSupported) {
        return &texture;
    }
    if (!decompressTexture(texture, scratch)) {
        logf(LogLevel::Error, "texture {}: block data does not decode", texture.name);
        return nullptr;
    }
    return &scratch;
}

}
