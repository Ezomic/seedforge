#include "ichor/save/save_slots.h"

#include "ichor/core/file_io.h"
#include "ichor/core/hash.h"
#include "ichor/sim/scene.h"
#include "ichor/sim/scene_io.h"

#include <algorithm>
#include <array>
#include <bit>
#include <set>
#include <cstring>
#include <fstream>
#include <iterator>

namespace ichor {

namespace {

static_assert(std::endian::native == std::endian::little, "slot files are little-endian");

constexpr char kMagic[4] = {'I', 'S', 'L', 'T'};
constexpr std::uintmax_t kMaxSlotBytes = 512ull * 1024 * 1024;

struct Header {
    char magic[4];
    std::uint32_t format;
    std::uint64_t worldSeed;
    std::uint32_t genVersion;
    std::uint32_t reserved;
    std::uint64_t dataDigest;
    std::uint64_t tick;
    std::uint64_t payloadSize;  // the snapshot, then the chronicle
    std::uint64_t snapshotSize; // how much of the payload is the snapshot
    std::uint64_t checksum;     // of the header and the payload
};
static_assert(sizeof(Header) == 64);

// Covers the header, with its own checksum field zeroed, and the payload, so a
// flipped bit anywhere makes the copy corrupt rather than wrong-world or
// unsupported, and the backups get their turn.
std::uint64_t checksum(Header header, const std::vector<std::uint8_t>& payload)
{
    header.checksum = 0;
    const std::uint64_t first = hash64(std::string_view(reinterpret_cast<const char*>(&header), sizeof(Header)));
    return hash64(std::string_view(reinterpret_cast<const char*>(payload.data()), payload.size()), first);
}

bool readFile(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes)
{
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec || size > kMaxSlotBytes) {
        bytes.clear();
        return !ec; // an implausibly large file is read as nothing, which is corrupt
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    bytes.resize(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    return static_cast<std::uintmax_t>(file.gcount()) == size;
}

std::vector<std::uint8_t> encode(Header header, const std::vector<std::uint8_t>& payload)
{
    header.payloadSize = payload.size();
    header.checksum = checksum(header, payload);
    std::vector<std::uint8_t> bytes(sizeof(Header) + payload.size());
    std::memcpy(bytes.data(), &header, sizeof(Header));
    std::copy(payload.begin(), payload.end(), bytes.begin() + static_cast<std::ptrdiff_t>(sizeof(Header)));
    return bytes;
}

// Ok covers every format this build can read, the current one or one with a migration path.
enum class SlotRead { Ok, Missing, Corrupt, UnsupportedVersion };

SlotRead readSlot(const MigrationRegistry& registry, const std::filesystem::path& path, Header& header, std::vector<std::uint8_t>& payload)
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
    if (header.payloadSize != bytes.size() - sizeof(Header) || header.snapshotSize > header.payloadSize) {
        return SlotRead::Corrupt;
    }
    payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(sizeof(Header)), bytes.end());
    if (checksum(header, payload) != header.checksum || header.reserved != 0) {
        return SlotRead::Corrupt;
    }
    // Only a copy that passes its checksum may claim to be from another format.
    return header.format == registry.current() || registry.canMigrate(header.format) ? SlotRead::Ok : SlotRead::UnsupportedVersion;
}

std::string unsupportedMessage(const MigrationRegistry& registry, std::uint32_t format)
{
    if (registry.isNewer(format)) {
        return "the slot was saved by a newer version of the game (format " + std::to_string(format) + ", this build reads up to "
            + std::to_string(registry.current()) + "); it has been left untouched";
    }
    return "the slot is in format " + std::to_string(format) + ", which this build can no longer migrate; it has been left untouched";
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
    // Lowercase ASCII only, so a slot is one file on case-insensitive filesystems too.
    const bool plain = !name.empty() && name.size() <= 64
        && std::all_of(name.begin(), name.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'; });
    if (!plain) {
        return false;
    }
    // Windows resolves these to devices whatever the extension.
    static const std::set<std::string_view> reserved = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
        "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    return !reserved.contains(name);
}

std::filesystem::path SaveSlots::pathOf(std::string_view name, std::uint32_t backup) const
{
    std::string file(name);
    file += backup == 0 ? ".sav" : ".bak" + std::to_string(backup);
    return directory_ / file;
}

bool SaveSlots::save(std::string_view name, const Scene& scene, const SaveContext& context, const Chronicle& chronicle)
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
    const MigrationRegistry& registry = migrations();
    for (std::uint32_t backup = 0; backup <= policy_.backups; ++backup) {
        Header existing{};
        std::vector<std::uint8_t> existingPayload;
        if (readSlot(registry, pathOf(name, backup), existing, existingPayload) == SlotRead::UnsupportedVersion) {
            error_ = unsupportedMessage(registry, existing.format) + "; save under another name";
            return false;
        }
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

    const std::size_t snapshotSize = payload.size();
    const std::string story = chronicle.serialize();
    payload.insert(payload.end(), story.begin(), story.end());

    Header header{};
    std::memcpy(header.magic, kMagic, sizeof(kMagic));
    header.format = registry.current();
    header.worldSeed = context.worldSeed;
    header.genVersion = context.genVersion;
    header.dataDigest = context.dataDigest;
    header.tick = scene.tickCount();
    header.payloadSize = payload.size();
    header.snapshotSize = snapshotSize;
    const std::vector<std::uint8_t> bytes = encode(header, payload);

    // The new file is written complete beside the slot first, so a failed write
    // leaves every existing copy exactly as it was.
    const std::filesystem::path fresh = pathOf(name, 0).replace_extension(".new");
    if (!writeFileAtomically(fresh, bytes)) {
        error_ = "the slot file could not be written";
        return false;
    }

    // Then the history shifts up: older backups first, and the current file only
    // joins the history if it is itself good, so a corrupt copy never pushes a
    // good backup out.
    for (std::uint32_t i = policy_.backups; i >= 2; --i) {
        const std::filesystem::path from = pathOf(name, i - 1);
        if (std::filesystem::exists(from, ec)) {
            std::filesystem::rename(from, pathOf(name, i), ec);
        }
    }
    Header currentHeader{};
    std::vector<std::uint8_t> currentPayload;
    if (policy_.backups >= 1 && readSlot(registry, pathOf(name, 0), currentHeader, currentPayload) == SlotRead::Ok) {
        std::filesystem::copy_file(pathOf(name, 0), pathOf(name, 1), std::filesystem::copy_options::overwrite_existing, ec);
    }
    std::filesystem::rename(fresh, pathOf(name, 0), ec);
    if (ec) {
        error_ = "the slot file could not be put in place: " + ec.message();
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
    const MigrationRegistry& registry = migrations();
    bool anyFile = false;
    for (std::uint32_t backup = 0; backup <= policy_.backups; ++backup) {
        Header header{};
        std::vector<std::uint8_t> payload;
        const SlotRead read = readSlot(registry, pathOf(name, backup), header, payload);
        if (read == SlotRead::Missing) {
            continue;
        }
        anyFile = true;
        if (read == SlotRead::UnsupportedVersion) {
            result.status = LoadStatus::UnsupportedVersion;
            result.savedFormat = header.format;
            error_ = unsupportedMessage(registry, header.format);
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
        const std::uint32_t savedFormat = header.format;
        if (savedFormat != registry.current()) {
            SlotBody body{{payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(header.snapshotSize)},
                std::string(payload.begin() + static_cast<std::ptrdiff_t>(header.snapshotSize), payload.end())};
            if (!registry.migrate(savedFormat, body)) {
                result.status = LoadStatus::MigrationFailed;
                result.savedFormat = savedFormat;
                error_ = "a migration step refused the slot file (format " + std::to_string(savedFormat) + "); it has been left untouched";
                continue;
            }
            header.format = registry.current();
            header.snapshotSize = body.snapshot.size();
            payload = std::move(body.snapshot);
            payload.insert(payload.end(), body.chronicle.begin(), body.chronicle.end());
        }
        const std::filesystem::path scratch = directory_ / (std::string(name) + ".load");
        bool restored = false;
        {
            std::ofstream out(scratch, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(header.snapshotSize));
            if (!out) {
                error_ = "the scratch file for the snapshot could not be written";
            } else {
                out.close();
                restored = loadScene(scratch, scene);
            }
        }
        std::error_code ec;
        std::filesystem::remove(scratch, ec);
        result.info = infoOf(name, header);
        if (!restored) {
            // A checksummed payload that will not restore is a bad snapshot, not bad media:
            // an older backup may still be good.
            result.status = LoadStatus::RestoreFailed;
            continue;
        }
        error_.clear();
        if (savedFormat != registry.current()) {
            result.migratedFrom = savedFormat;
            // Only the slot file itself is rewritten, after the old one is kept whole beside it.
            // A backup is migrated in memory and left as it is, so the rotating history is never touched.
            if (backup == 0) {
                std::filesystem::path kept = pathOf(name, 0);
                kept.replace_extension(".premigrate-v" + std::to_string(savedFormat));
                std::filesystem::copy_file(pathOf(name, 0), kept, std::filesystem::copy_options::overwrite_existing, ec);
                if (ec || !writeFileAtomically(pathOf(name, 0), encode(header, payload))) {
                    error_ = "the slot was migrated in memory only; the file on disk is unchanged";
                }
            }
        }
        // The story is read after the scene is back; a damaged one costs the journal, not the save.
        const std::string_view story(reinterpret_cast<const char*>(payload.data()) + header.snapshotSize, payload.size() - header.snapshotSize);
        result.chronicleDamaged = !story.empty() && !Chronicle::parse(story, result.chronicle);
        result.status = LoadStatus::Loaded;
        result.fromBackup = backup > 0;
        result.dataChanged = header.dataDigest != expected.dataDigest;
        return result;
    }
    if (result.status != LoadStatus::RestoreFailed && result.status != LoadStatus::MigrationFailed) {
        result.status = anyFile ? LoadStatus::Corrupt : LoadStatus::NoSuchSlot;
    }
    return result;
}

std::vector<SlotInfo> SaveSlots::list() const
{
    // A slot is every name with a .sav or a backup, shown by its newest good copy.
    std::set<std::string> names;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(directory_, ec)) {
        const std::string extension = entry.path().extension().string();
        if (entry.is_regular_file() && (extension == ".sav" || extension.starts_with(".bak"))) {
            names.insert(entry.path().stem().string());
        }
    }
    std::vector<SlotInfo> slots;
    for (const std::string& name : names) {
        for (std::uint32_t backup = 0; backup <= policy_.backups; ++backup) {
            Header header{};
            std::vector<std::uint8_t> payload;
            if (readSlot(migrations(), pathOf(name, backup), header, payload) == SlotRead::Ok) {
                slots.push_back(infoOf(name, header));
                break;
            }
        }
    }
    std::sort(slots.begin(), slots.end(), [](const SlotInfo& a, const SlotInfo& b) { return a.name < b.name; });
    return slots;
}

}
