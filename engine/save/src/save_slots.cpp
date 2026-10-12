#include "ichor/save/save_slots.h"

#include "ichor/core/file_io.h"
#include "ichor/core/hash.h"
#include "ichor/sim/scene.h"
#include "ichor/sim/scene_io.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>

namespace ichor {

namespace {

constexpr char kMagic[4] = {'I', 'S', 'L', 'T'};
constexpr std::uint32_t kFormat = 1;

struct Header {
    char magic[4];
    std::uint32_t format;
    std::uint64_t worldSeed;
    std::uint32_t genVersion;
    std::uint32_t reserved;
    std::uint64_t dataDigest;
    std::uint64_t tick;
    std::uint64_t payloadSize;
    std::uint64_t checksum; // of the payload
};
static_assert(sizeof(Header) == 56);

std::uint64_t checksum(const std::vector<std::uint8_t>& payload)
{
    return hash64(std::string_view(reinterpret_cast<const char*>(payload.data()), payload.size()));
}

bool readFile(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

enum class SlotRead { Ok, Missing, Corrupt, UnsupportedVersion };

SlotRead readSlot(const std::filesystem::path& path, Header& header, std::vector<std::uint8_t>& payload)
{
    std::vector<std::uint8_t> bytes;
    if (!readFile(path, bytes)) {
        return SlotRead::Missing;
    }
    if (bytes.size() < sizeof(Header)) {
        return SlotRead::Corrupt;
    }
    std::memcpy(&header, bytes.data(), sizeof(Header));
    if (std::memcmp(header.magic, kMagic, sizeof(kMagic)) != 0) {
        return SlotRead::Corrupt;
    }
    if (header.format != kFormat) {
        return SlotRead::UnsupportedVersion;
    }
    if (header.payloadSize != bytes.size() - sizeof(Header)) {
        return SlotRead::Corrupt;
    }
    payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(sizeof(Header)), bytes.end());
    return checksum(payload) == header.checksum ? SlotRead::Ok : SlotRead::Corrupt;
}

SlotInfo infoOf(std::string_view name, const Header& header)
{
    SlotInfo info;
    info.name = std::string(name);
    info.context = {header.worldSeed, header.genVersion, header.dataDigest};
    info.tick = header.tick;
    return info;
}

}

SaveSlots::SaveSlots(std::filesystem::path directory, SavePolicy policy)
    : directory_(std::move(directory))
    , policy_(policy)
{
}

bool SaveSlots::validName(std::string_view name)
{
    return !name.empty() && name.size() <= 64
        && std::all_of(name.begin(), name.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_'; });
}

std::filesystem::path SaveSlots::pathOf(std::string_view name, std::uint32_t backup) const
{
    std::string file(name);
    file += backup == 0 ? ".sav" : ".bak" + std::to_string(backup);
    return directory_ / file;
}

bool SaveSlots::save(std::string_view name, const Scene& scene, const SaveContext& context)
{
    error_.clear();
    if (!validName(name)) {
        error_ = "invalid slot name";
        return false;
    }
    if (policy_.ironman && name != kIronmanSlot) {
        error_ = "ironman allows one rolling slot only";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(directory_, ec);

    // The snapshot goes through the scene's own writer into a scratch file, then
    // into the slot with its header and checksum.
    const std::filesystem::path scratch = directory_ / (std::string(name) + ".tmp");
    if (!saveScene(scratch, scene)) {
        error_ = "the scene snapshot could not be written";
        return false;
    }
    std::vector<std::uint8_t> payload;
    const bool read = readFile(scratch, payload);
    std::filesystem::remove(scratch, ec);
    if (!read) {
        error_ = "the scene snapshot could not be read back";
        return false;
    }

    Header header{};
    std::memcpy(header.magic, kMagic, sizeof(kMagic));
    header.format = kFormat;
    header.worldSeed = context.worldSeed;
    header.genVersion = context.genVersion;
    header.dataDigest = context.dataDigest;
    header.tick = scene.tickCount();
    header.payloadSize = payload.size();
    header.checksum = checksum(payload);
    std::vector<std::uint8_t> bytes(sizeof(Header) + payload.size());
    std::memcpy(bytes.data(), &header, sizeof(Header));
    std::copy(payload.begin(), payload.end(), bytes.begin() + static_cast<std::ptrdiff_t>(sizeof(Header)));

    // Rotate only what is there, oldest first, and only after the new bytes are
    // known good in memory; the replacement itself is atomic.
    for (std::uint32_t i = policy_.backups; i >= 1; --i) {
        const std::filesystem::path from = pathOf(name, i - 1);
        if (std::filesystem::exists(from, ec)) {
            std::filesystem::rename(from, pathOf(name, i), ec);
            if (ec) {
                std::filesystem::remove(pathOf(name, i), ec);
                std::filesystem::rename(from, pathOf(name, i), ec);
            }
        }
    }
    if (!writeFileAtomically(pathOf(name, 0), bytes)) {
        error_ = "the slot file could not be written";
        return false;
    }
    return true;
}

LoadResult SaveSlots::load(std::string_view name, Scene& scene, const SaveContext& expected)
{
    error_.clear();
    LoadResult result;
    if (!validName(name)) {
        error_ = "invalid slot name";
        return result;
    }
    bool anyFile = false;
    for (std::uint32_t backup = 0; backup <= policy_.backups; ++backup) {
        Header header{};
        std::vector<std::uint8_t> payload;
        const SlotRead read = readSlot(pathOf(name, backup), header, payload);
        if (read == SlotRead::Missing) {
            continue;
        }
        anyFile = true;
        if (read == SlotRead::UnsupportedVersion) {
            result.status = LoadStatus::UnsupportedVersion;
            return result;
        }
        if (read == SlotRead::Corrupt) {
            continue;
        }
        if (header.worldSeed != expected.worldSeed || header.genVersion != expected.genVersion) {
            result.status = LoadStatus::WrongWorld;
            result.info = infoOf(name, header);
            return result;
        }
        const std::filesystem::path scratch = directory_ / (std::string(name) + ".load");
        {
            std::ofstream out(scratch, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
        }
        const bool restored = loadScene(scratch, scene);
        std::error_code ec;
        std::filesystem::remove(scratch, ec);
        result.info = infoOf(name, header);
        if (!restored) {
            // A checksummed payload that will not restore is a bad snapshot, not bad media:
            // an older backup may still be good.
            result.status = LoadStatus::RestoreFailed;
            continue;
        }
        result.status = LoadStatus::Loaded;
        result.fromBackup = backup > 0;
        result.dataChanged = header.dataDigest != expected.dataDigest;
        return result;
    }
    if (result.status != LoadStatus::RestoreFailed) {
        result.status = anyFile ? LoadStatus::Corrupt : LoadStatus::NoSuchSlot;
    }
    return result;
}

std::vector<SlotInfo> SaveSlots::list() const
{
    std::vector<SlotInfo> slots;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(directory_, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".sav") {
            continue;
        }
        Header header{};
        std::vector<std::uint8_t> payload;
        if (readSlot(entry.path(), header, payload) == SlotRead::Ok) {
            slots.push_back(infoOf(entry.path().stem().string(), header));
        }
    }
    std::sort(slots.begin(), slots.end(), [](const SlotInfo& a, const SlotInfo& b) { return a.name < b.name; });
    return slots;
}

}
