#include "ichor/world/world_identity.h"

#include "ichor/core/hash.h"
#include "ichor/world/forge.h"

#include <bit>
#include <cmath>

namespace ichor {

namespace {

constexpr std::string_view kPrefix = "ichor1";
constexpr std::size_t kLength = 6 + 1 + 16 + 1 + 8 + 1 + 16 + 1 + 4;

std::uint16_t checkOf(const WorldIdentity& id)
{
    std::uint64_t h = mix64(id.seed);
    h = mix64(h ^ id.genVersion);
    h = mix64(h ^ id.contentDigest);
    return static_cast<std::uint16_t>(h >> 48);
}

void appendHex(std::string& out, std::uint64_t value, int digits)
{
    constexpr char kDigits[] = "0123456789abcdef";
    for (int i = digits - 1; i >= 0; --i) {
        out.push_back(kDigits[(value >> (4 * i)) & 0xf]);
    }
}

bool readHex(std::string_view text, std::uint64_t& out)
{
    out = 0;
    for (const char c : text) {
        std::uint64_t v;
        if (c >= '0' && c <= '9') {
            v = static_cast<std::uint64_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            v = static_cast<std::uint64_t>(c - 'a' + 10);
        } else {
            return false;
        }
        out = (out << 4) | v;
    }
    return true;
}

void putU32(std::string& out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
    }
}

bool forgeable(const WorldDesc& desc)
{
    return desc.valid() && std::isfinite(desc.minHeight) && std::isfinite(desc.heightRange);
}

}

std::string toString(const WorldIdentity& identity)
{
    std::string out(kPrefix);
    out.push_back('.');
    appendHex(out, identity.seed, 16);
    out.push_back('.');
    appendHex(out, identity.genVersion, 8);
    out.push_back('.');
    appendHex(out, identity.contentDigest, 16);
    out.push_back('.');
    appendHex(out, checkOf(identity), 4);
    return out;
}

std::optional<WorldIdentity> parseWorldIdentity(std::string_view text)
{
    if (text.size() != kLength || text.substr(0, kPrefix.size()) != kPrefix) {
        return std::nullopt;
    }
    const std::string_view body = text.substr(kPrefix.size());
    if (body[0] != '.' || body[17] != '.' || body[26] != '.' || body[43] != '.') {
        return std::nullopt;
    }
    std::uint64_t seed = 0, gen = 0, digest = 0, check = 0;
    if (!readHex(body.substr(1, 16), seed) || !readHex(body.substr(18, 8), gen) || !readHex(body.substr(27, 16), digest)
        || !readHex(body.substr(44, 4), check)) {
        return std::nullopt;
    }
    const WorldIdentity id{seed, static_cast<std::uint32_t>(gen), digest};
    if (checkOf(id) != check) {
        return std::nullopt;
    }
    return id;
}

std::uint64_t worldContentDigest(const WorldDesc& desc)
{
    const Heightfield field = forgeHeightfield(desc);
    std::string shape;
    putU32(shape, desc.sizeMetres);
    putU32(shape, desc.metresPerSample);
    putU32(shape, desc.cellMetres);
    putU32(shape, std::bit_cast<std::uint32_t>(desc.minHeight));
    putU32(shape, std::bit_cast<std::uint32_t>(desc.heightRange));
    putU32(shape, std::bit_cast<std::uint32_t>(desc.waterHeight));
    return hashCombine(field.digest(), hash64(shape));
}

std::optional<WorldIdentity> identifyWorld(const WorldDesc& desc, std::uint32_t genVersion)
{
    if (!forgeable(desc)) {
        return std::nullopt;
    }
    return WorldIdentity{desc.seed, genVersion, worldContentDigest(desc)};
}

WorldMatch verifyWorld(const WorldIdentity& identity, const WorldDesc& shape)
{
    if (!forgeable(shape)) {
        return WorldMatch::InvalidShape;
    }
    if (identity.genVersion != kForgeGenVersion) {
        return WorldMatch::UnsupportedVersion;
    }
    WorldDesc desc = shape;
    desc.seed = identity.seed;
    return worldContentDigest(desc) == identity.contentDigest ? WorldMatch::Match : WorldMatch::DigestMismatch;
}

}
