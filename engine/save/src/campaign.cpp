#include "ichor/save/campaign.h"

#include "ichor/core/hash.h"
#include "ichor/save/atomic_file.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <new>
#include <set>
#include <system_error>

namespace ichor {

namespace {

namespace fs = std::filesystem;

constexpr std::array<std::uint8_t, 4> kMagic = { 'I', 'C', 'M', 'F' };
constexpr std::uint8_t kVersion = 1;
constexpr std::uint32_t kHardMaxFiles = 1u << 16;
constexpr std::uintmax_t kMaxManifestBytes = std::uintmax_t{ 16 } << 20;
constexpr std::uint64_t kHashSeed = 14695981039346656037ull;

bool plainChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

void put(std::vector<std::uint8_t>& out, std::uint64_t value, int bytes)
{
    for (int i = 0; i < bytes; ++i) {
        out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
}

struct Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t at = 0;
    bool bad = false;

    std::uint64_t get(int count)
    {
        if (bad || bytes.size() - at < static_cast<std::size_t>(count)) {
            bad = true;
            return 0;
        }
        std::uint64_t value = 0;
        for (int i = 0; i < count; ++i) {
            value |= std::uint64_t{ bytes[at + static_cast<std::size_t>(i)] } << (8 * i);
        }
        at += static_cast<std::size_t>(count);
        return value;
    }
};

std::uint64_t hashBytes(std::span<const std::uint8_t> bytes)
{
    return hash64(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

// Layout, little-endian: "ICMF" | u8 version | u64 seed | u32 genVersion | u32 count
// | per file: u8 nameLen | name | u64 size | u64 hash | u64 checksum of everything before it.
std::vector<std::uint8_t> encodeManifest(const WorldManifest& manifest)
{
    std::vector<std::uint8_t> out(kMagic.begin(), kMagic.end());
    out.push_back(kVersion);
    put(out, manifest.worldSeed, 8);
    put(out, manifest.genVersion, 4);
    put(out, manifest.files.size(), 4);
    for (const WorldFileInfo& file : manifest.files) {
        out.push_back(static_cast<std::uint8_t>(file.name.size()));
        out.insert(out.end(), file.name.begin(), file.name.end());
        put(out, file.size, 8);
        put(out, file.hash, 8);
    }
    put(out, hashBytes(out), 8);
    return out;
}

std::optional<WorldManifest> decodeManifest(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() < kMagic.size() + 1 + 8 + 4 + 4 + 8 || !std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
        return std::nullopt;
    }
    Reader tail{ bytes, bytes.size() - 8 };
    if (tail.get(8) != hashBytes(bytes.first(bytes.size() - 8))) {
        return std::nullopt;
    }
    Reader in{ bytes.first(bytes.size() - 8), kMagic.size() };
    if (in.get(1) != kVersion) {
        return std::nullopt;
    }
    WorldManifest manifest;
    manifest.worldSeed = in.get(8);
    manifest.genVersion = static_cast<std::uint32_t>(in.get(4));
    const std::uint64_t count = in.get(4);
    if (in.bad || count > kHardMaxFiles) {
        return std::nullopt;
    }
    for (std::uint64_t i = 0; i < count; ++i) {
        WorldFileInfo file;
        const std::uint64_t length = in.get(1);
        if (in.bad || in.bytes.size() - in.at < length) {
            return std::nullopt;
        }
        file.name.assign(reinterpret_cast<const char*>(in.bytes.data() + in.at), static_cast<std::size_t>(length));
        in.at += static_cast<std::size_t>(length);
        file.size = in.get(8);
        file.hash = in.get(8);
        const bool unsorted = !manifest.files.empty() && !(manifest.files.back().name < file.name);
        if (in.bad || unsorted || !Campaign::validBlobName(file.name)) {
            return std::nullopt;
        }
        manifest.files.push_back(std::move(file));
    }
    if (in.bad || in.at != in.bytes.size()) {
        return std::nullopt;
    }
    return manifest;
}

// Streams the file so a multi-hundred-megabyte blob is hashed without a second copy.
// FNV-1a chains exactly, so the chunked hash equals hashBytes() of the whole file.
bool hashFile(const fs::path& path, std::uint64_t expectedSize, std::uint64_t& hash, std::vector<std::uint8_t>* keep)
{
    std::error_code ec;
    const std::uintmax_t actual = fs::file_size(path, ec);
    if (ec || actual != expectedSize) {
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    if (keep) {
        keep->assign(static_cast<std::size_t>(expectedSize), 0);
    }
    std::vector<char> chunk(1 << 16);
    std::uint64_t state = kHashSeed;
    std::uint64_t seen = 0;
    while (seen < expectedSize) {
        const auto want = static_cast<std::streamsize>(std::min<std::uint64_t>(chunk.size(), expectedSize - seen));
        file.read(chunk.data(), want);
        if (file.gcount() != want) {
            return false;
        }
        state = hash64(std::string_view(chunk.data(), static_cast<std::size_t>(want)), state);
        if (keep) {
            std::memcpy(keep->data() + seen, chunk.data(), static_cast<std::size_t>(want));
        }
        seen += static_cast<std::uint64_t>(want);
    }
    file.peek();
    if (!file.eof()) {
        return false; // the file grew after its size was checked
    }
    hash = state;
    return true;
}

// A folder a crashed forge left behind: no manifest, and nothing in it but the two folders create() makes.
bool abandonedForge(const fs::path& directory)
{
    std::error_code ec;
    if (fs::exists(directory / "world" / "manifest.bin", ec)) {
        return false;
    }
    for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name != "world" && name != "slots") {
            return false;
        }
    }
    return !ec;
}

}

bool Campaign::validName(std::string_view name)
{
    return !name.empty() && name.size() <= 48 && std::all_of(name.begin(), name.end(), plainChar);
}

bool Campaign::validBlobName(std::string_view name)
{
    return !name.empty() && name.size() <= 64 && name.front() != '.' && name.back() != '.' && name != kManifestFile && !name.ends_with(".tmp")
        && std::all_of(name.begin(), name.end(), [](char c) { return plainChar(c) || c == '.'; });
}

Campaign::Campaign(fs::path directory, std::string name, WorldManifest manifest)
    : directory_(std::move(directory))
    , name_(std::move(name))
    , manifest_(std::move(manifest))
{
}

CampaignStatus Campaign::create(const fs::path& root, std::string_view name, std::uint64_t worldSeed, std::uint32_t genVersion,
    std::span<const WorldBlob> blobs, const CampaignLimits& limits)
{
    if (!validName(name)) {
        return CampaignStatus::BadName;
    }
    if (blobs.size() > std::min(limits.maxFiles, kHardMaxFiles)) {
        return CampaignStatus::TooLarge;
    }
    WorldManifest manifest{ worldSeed, genVersion, {} };
    std::set<std::string_view> seen;
    for (const WorldBlob& blob : blobs) {
        if (!validBlobName(blob.name) || !seen.insert(blob.name).second) {
            return CampaignStatus::BadName;
        }
        if (blob.bytes.size() > limits.maxFileBytes) {
            return CampaignStatus::TooLarge;
        }
        manifest.files.push_back({ blob.name, blob.bytes.size(), hashBytes(blob.bytes) });
    }
    std::sort(manifest.files.begin(), manifest.files.end(), [](const WorldFileInfo& a, const WorldFileInfo& b) { return a.name < b.name; });

    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        return CampaignStatus::WriteFailed;
    }
    const fs::path directory = root / std::string(name);
    if (fs::exists(directory, ec)) {
        if (!abandonedForge(directory)) {
            return CampaignStatus::AlreadyExists;
        }
        fs::remove_all(directory, ec);
    }
    if (!fs::create_directory(directory, ec)) {
        return ec ? CampaignStatus::WriteFailed : CampaignStatus::AlreadyExists;
    }

    const auto fail = [&] {
        std::error_code ignored;
        fs::remove_all(directory, ignored);
        return CampaignStatus::WriteFailed;
    };
    const fs::path world = directory / "world";
    if (!fs::create_directory(world, ec) || !fs::create_directory(directory / "slots", ec)) {
        return fail();
    }
    AtomicWriteOptions once;
    once.backups = 0;
    for (const WorldBlob& blob : blobs) {
        if (!writeAtomic(world / blob.name, blob.bytes, once).ok()) {
            return fail();
        }
    }
    const std::vector<std::uint8_t> encoded = encodeManifest(manifest);
    if (!writeAtomic(world / kManifestFile, encoded, once).ok()) {
        return fail();
    }
    return CampaignStatus::Ok;
}

std::optional<Campaign> Campaign::open(const fs::path& root, std::string_view name, CampaignStatus* status)
{
    const auto finish = [&](CampaignStatus value) -> std::optional<Campaign> {
        if (status) {
            *status = value;
        }
        return std::nullopt;
    };
    if (!validName(name)) {
        return finish(CampaignStatus::BadName);
    }
    const fs::path directory = root / std::string(name);
    std::error_code ec;
    if (!fs::is_directory(directory, ec)) {
        return finish(CampaignStatus::NoSuchCampaign);
    }
    const fs::path manifestPath = directory / "world" / kManifestFile;
    if (!fs::exists(manifestPath, ec)) {
        return finish(CampaignStatus::Incomplete);
    }
    const RecoveredFile file = recoverNewestValid(manifestPath, 0, {}, kMaxManifestBytes);
    if (!file.found) {
        return finish(CampaignStatus::CorruptWorld);
    }
    std::optional<WorldManifest> manifest = decodeManifest(file.bytes);
    if (!manifest) {
        return finish(CampaignStatus::CorruptWorld);
    }
    for (const WorldFileInfo& info : manifest->files) {
        const fs::path blob = directory / "world" / info.name;
        if (!fs::is_regular_file(blob, ec) || fs::file_size(blob, ec) != info.size || ec) {
            return finish(CampaignStatus::CorruptWorld);
        }
    }
    if (status) {
        *status = CampaignStatus::Ok;
    }
    return Campaign(directory, std::string(name), std::move(*manifest));
}

std::vector<std::string> Campaign::list(const fs::path& root)
{
    std::vector<std::string> names;
    std::error_code ec;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (validName(name) && open(root, name)) {
            names.push_back(name);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

SaveContext Campaign::slotContext(std::uint64_t dataDigest) const
{
    return { manifest_.worldSeed, manifest_.genVersion, dataDigest };
}

SaveSlots Campaign::slots(SavePolicy policy) const
{
    return SaveSlots(slotsDirectory(), policy);
}

bool Campaign::verifyWorld() const
{
    return std::all_of(manifest_.files.begin(), manifest_.files.end(), [&](const WorldFileInfo& info) {
        std::uint64_t hash = 0;
        return hashFile(worldDirectory() / info.name, info.size, hash, nullptr) && hash == info.hash;
    });
}

std::optional<std::vector<std::uint8_t>> Campaign::readBlob(std::string_view blobName) const
{
    const auto info = std::find_if(manifest_.files.begin(), manifest_.files.end(), [&](const WorldFileInfo& f) { return f.name == blobName; });
    if (info == manifest_.files.end()) {
        return std::nullopt;
    }
    if (info->size > CampaignLimits{}.maxFileBytes) {
        return std::nullopt;
    }
    try {
        std::vector<std::uint8_t> bytes;
        std::uint64_t hash = 0;
        if (!hashFile(worldDirectory() / info->name, info->size, hash, &bytes) || hash != info->hash) {
            return std::nullopt;
        }
        return bytes;
    } catch (const std::bad_alloc&) {
        return std::nullopt;
    }
}

}
