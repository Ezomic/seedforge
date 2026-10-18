#include "codec_fuzz.h"
#include "ichor/ledger/consequence_ledger.h"
#include "ichor/save/atomic_file.h"
#include "ichor/save/campaign.h"
#include "ichor/save/chronicle.h"
#include "ichor/save/codex_codec.h"
#include "ichor/save/death_markers.h"
#include "ichor/save/delta_store.h"
#include "ichor/save/forge_cache.h"
#include "ichor/save/ironman.h"
#include "ichor/save/notable_events.h"
#include "ichor/save/settings.h"
#include "ichor/save/snapshot_tail.h"
#include "ichor/sim/scene.h"
#include "ichor/ui/map_annotation_codec.h"
#include "ichor/ui/map_annotation_share.h"
#include "test_temp.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

// SEED-412: one campaign that touches every save-related module on main, written through the real
// Campaign / SaveSlots / writeAtomic paths into a temp folder, read back, and compared byte for byte.
// Then each file is truncated, flipped, extended and deleted in turn, and the loaders must refuse or
// recover to a state the campaign really had, without touching any other file's state.
//
// Not covered, because there is no persistence for it on main:
//   - fight records (stage0 FightRecord has an encoder, but it lives in ichor_stage0, which this test
//     executable does not link);
//   - the journal screen model and the map screen model (they only read the chronicle and annotations);
//   - the autosave policy's AutosaveState and the codex inside a slot (no byte form yet).

using namespace ichor;
namespace fs = std::filesystem;

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr std::uint64_t kWorldSeed = 77;
constexpr std::uint32_t kGenVersion = 3;
constexpr std::uint64_t kDataDigest = 4242;
constexpr std::uint64_t kSceneSeed = 5;
constexpr double kDt = 1.0 / 60.0;
constexpr const char* kCampaignName = "roundtrip-77";
constexpr const char* kSlotName = "main";
constexpr std::uint32_t kSideBackups = 2;

class Dir {
public:
    Dir()
    {
        static int counter = 0;
        path_ = test::tempPath("ichor_campaign_rt_" + std::to_string(counter++));
        fs::remove_all(path_);
        fs::create_directories(path_);
    }
    ~Dir() { fs::remove_all(path_); }
    Dir(const Dir&) = delete;
    Dir& operator=(const Dir&) = delete;
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

Bytes readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
}

void writeFile(const fs::path& path, const Bytes& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

Bytes toBytes(const std::string& text)
{
    return { text.begin(), text.end() };
}

Bytes pattern(std::size_t size, std::uint8_t salt)
{
    Bytes bytes(size);
    for (std::size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<std::uint8_t>((i * 31 + salt) & 0xff);
    }
    return bytes;
}

Bytes flatten(const DeltaState& state)
{
    Bytes out;
    for (const auto& [key, value] : state) {
        out.insert(out.end(), key.begin(), key.end());
        out.push_back(0);
        out.push_back(static_cast<std::uint8_t>(value.size() & 0xff));
        out.push_back(static_cast<std::uint8_t>(value.size() >> 8));
        out.insert(out.end(), value.begin(), value.end());
    }
    return out;
}

Bytes sceneAndChronicle(const Scene& scene, const Chronicle& chronicle)
{
    Bytes out;
    const std::uint64_t hash = scene.stateHash();
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>(hash >> (8 * i)));
    }
    const Bytes text = toBytes(chronicle.serialize());
    out.insert(out.end(), text.begin(), text.end());
    return out;
}

void spawnWell(Scene& scene)
{
    SpawnCommand command;
    command.id = StableId::of("well");
    command.name.value = "well";
    command.transform.position = { 1.0, 0.0, 0.0 };
    scene.enqueue(command);
}

// The paths of every file the campaign keeps, relative to the campaign folder.
const char* const kManifest = "world/manifest.bin";
const char* const kHeight = "world/heightmap.r16";
const char* const kLayouts = "world/dungeon_layouts.bin";
const char* const kSlotFile = "slots/main.sav";
const char* const kLedger = "player/ledger.bin";
const char* const kDeaths = "player/deaths.bin";
const char* const kNotes = "player/annotations.bin";
const char* const kShare = "player/shared_map.bin";
const char* const kCodex = "player/codex.bin";
const char* const kIronman = "player/ironman.txt";
const char* const kSettings = "player/settings.txt";
const char* const kTail = "player/state.tail";

const ForgeCacheKey kKeyA{ kWorldSeed, kGenVersion, 0x1122334455667788ull };
const ForgeCacheKey kKeyB{ 9001, kGenVersion, 0x99aabbccddeeff00ull };

// The state the campaign is in when it is saved. Everything is built through the public APIs.
struct Model {
    Scene scene{ kSceneSeed };
    Chronicle chronicle;
    Chronicle earlierChronicle;
    Bytes earlierSceneAndChronicle;
    ConsequenceLedger ledger;
    DeathMarkerSet deaths{ kWorldSeed };
    MapAnnotationStore notes;
    CodexStore codex;
    IronmanRun ironman;
    Settings settings;
    std::optional<SnapshotTailLog> tail;
    std::vector<Bytes> tailHistory;
    Bytes sharedEnvelope;
    Bytes forgeA = pattern(700, 11);
    Bytes forgeB = pattern(333, 12);
    Bytes height = pattern(4000, 1);
    Bytes layouts = pattern(900, 2);
};

DeltaState stateStep(int step)
{
    DeltaState state;
    state["player.hp"] = { static_cast<std::uint8_t>(100 - step * 7) };
    state["world.day"] = { static_cast<std::uint8_t>(step), 0, 0, 0 };
    state["npc.7.mood"] = toBytes(step % 2 == 0 ? "calm" : "angry");
    if (step >= 2) {
        state["door.keep.open"] = { 1 };
    }
    if (step == 3) {
        state.erase("npc.7.mood");
    }
    return state;
}

void buildModel(Model& m)
{
    spawnWell(m.scene);
    for (int i = 0; i < 10; ++i) {
        m.scene.tick(kDt);
    }
    REQUIRE(m.earlierChronicle.add(0, "start", "A campaign begins."));
    m.earlierSceneAndChronicle = sceneAndChronicle(m.scene, m.earlierChronicle);

    for (int i = 0; i < 45; ++i) {
        m.scene.tick(kDt);
    }
    m.chronicle = m.earlierChronicle;
    REQUIRE(m.chronicle.add(120, "quest", "Tabs\tand\nnewlines, and caf\xc3\xa9 text.", { { 9, 3, 3, 12 }, 7, 200 }));
    REQUIRE(m.chronicle.add(900, "death", "Someone fell."));

    REQUIRE(m.ledger.append({ "f1", "npc.7", "killed", "by.player", 100 }) == AppendResult::Ok);
    REQUIRE(m.ledger.append({ "f2", "town:a", "burned", "", 200 }) == AppendResult::Ok);
    REQUIRE(m.ledger.append({ "f3", "npc.7", "mourned", "town:a", 300 }) == AppendResult::Ok);
    REQUIRE(appendNotableEvents(m.chronicle, { { 1500, "world", "The harbour burned.", 90 } }) == 1);

    REQUIRE(m.deaths.add({ 1000, 2000, 300, DeathCause::Fall, 4 }) == DeathAddResult::Added);
    REQUIRE(m.deaths.add({ -50000, 75000, -20, DeathCause::Combat, 9 }) == DeathAddResult::Added);
    REQUIRE(m.deaths.add({ 1010, 2010, 300, DeathCause::Hazard, 11 }) == DeathAddResult::Added);
    REQUIRE(m.deaths.add({ 99999999, -99999999, 0, DeathCause::Drowning, 2 }) == DeathAddResult::Added);

    MapAnnotation mark;
    mark.id = "camp-1";
    mark.kind = AnnotationKind::Mark;
    mark.symbol = AnnotationSymbol::Camp;
    mark.text = "safe";
    mark.points = { { 10.5f, -3.25f } };
    REQUIRE(m.notes.add(mark) == AnnotationResult::Ok);
    MapAnnotation note;
    note.id = "note.2";
    note.kind = AnnotationKind::Note;
    note.text = "caf\xc3\xa9 behind the falls";
    note.points = { { 0.0f, 99.0f } };
    REQUIRE(m.notes.add(note) == AnnotationResult::Ok);
    MapAnnotation path;
    path.id = "route";
    path.kind = AnnotationKind::Path;
    path.symbol = AnnotationSymbol::Direction;
    path.points = { { 0.0f, 0.0f }, { 5.0f, 5.0f }, { 9.0f, 1.5f } };
    REQUIRE(m.notes.add(path) == AnnotationResult::Ok);

    // A friend's map for the same seed, as it arrives on the wire.
    MapAnnotationStore theirs;
    MapAnnotation theirMark = mark;
    theirMark.text = "their camp";
    REQUIRE(theirs.add(theirMark) == AnnotationResult::Ok);
    MapAnnotation theirDanger;
    theirDanger.id = "bog";
    theirDanger.kind = AnnotationKind::Mark;
    theirDanger.symbol = AnnotationSymbol::Danger;
    theirDanger.points = { { -7.0f, 12.0f } };
    REQUIRE(theirs.add(theirDanger) == AnnotationResult::Ok);
    const AnnotationShareEncoded shared = encodeAnnotationShare(theirs, "seed-77", "wanderer");
    REQUIRE(shared.status == AnnotationShareStatus::Ok);
    m.sharedEnvelope = shared.bytes;

    REQUIRE(m.codex.unlock(CodexCategory::Axiom, "gravity.low") == CodexResult::Unlocked);
    REQUIRE(m.codex.unlock(CodexCategory::Faction, "archonate") == CodexResult::Unlocked);
    REQUIRE(m.codex.unlock(CodexCategory::World, "world-77") == CodexResult::Unlocked);
    REQUIRE(m.codex.unlock(CodexCategory::Seed, "seed.77") == CodexResult::Unlocked);

    m.ironman.recordDeath(900);
    m.settings.fovDegrees = 72.5f;
    m.settings.mouseSensitivity = 2.25f;

    auto log = SnapshotTailLog::fromSnapshot(stateStep(0));
    REQUIRE(log.has_value());
    m.tailHistory.push_back(flatten(stateStep(0)));
    for (int step = 1; step <= 3; ++step) {
        REQUIRE(log->append(stateStep(step)));
        m.tailHistory.push_back(flatten(stateStep(step)));
    }
    m.tail = std::move(log);
}

void putAtomic(const fs::path& path, const Bytes& bytes)
{
    fs::create_directories(path.parent_path());
    AtomicWriteOptions options;
    options.backups = kSideBackups;
    REQUIRE(writeAtomic(path, bytes, options).ok());
}

SaveContext contextOf(const Campaign& campaign)
{
    return campaign.slotContext(kDataDigest);
}

// Writes the whole campaign into root/<campaign>. The slot is saved twice (earlier, then now) so the
// newest file always has a real backup behind it.
void saveCampaign(const fs::path& root, Model& m)
{
    const std::vector<WorldBlob> blobs = { { "heightmap.r16", m.height }, { "dungeon_layouts.bin", m.layouts } };
    REQUIRE(Campaign::create(root, kCampaignName, kWorldSeed, kGenVersion, blobs) == CampaignStatus::Ok);
    const auto campaign = Campaign::open(root, kCampaignName);
    REQUIRE(campaign.has_value());

    Scene earlier(kSceneSeed);
    spawnWell(earlier);
    for (int i = 0; i < 10; ++i) {
        earlier.tick(kDt);
    }
    SaveSlots slots = campaign->slots();
    REQUIRE(slots.save(kSlotName, earlier, contextOf(*campaign), m.earlierChronicle));
    REQUIRE(slots.save(kSlotName, m.scene, contextOf(*campaign), m.chronicle));

    const fs::path dir = root / kCampaignName;
    putAtomic(dir / kLedger, m.ledger.serialize());
    putAtomic(dir / kDeaths, encodeDeathMarkers(m.deaths));
    putAtomic(dir / kNotes, encodeAnnotations(m.notes));
    putAtomic(dir / kShare, m.sharedEnvelope);
    putAtomic(dir / kCodex, encodeCodex(m.codex));
    putAtomic(dir / kIronman, toBytes(m.ironman.encode()));
    REQUIRE(saveSettings(dir / kSettings, m.settings));
    const auto image = m.tail->encode();
    REQUIRE(image.has_value());
    putAtomic(dir / kTail, *image);

    fs::create_directories(dir / "forge");
    ForgeCache cache(dir / "forge");
    REQUIRE(cache.put(kKeyA, m.forgeA) == ForgePutStatus::Stored);
    REQUIRE(cache.put(kKeyB, m.forgeB) == ForgePutStatus::Stored);
}

// What a part decoded to, as the canonical bytes of that state. Nullopt means the loader refused it.
using Canon = std::optional<Bytes>;

struct Loaded {
    std::map<std::string, Canon> parts;
};

template <typename Decode>
Canon loadSide(const fs::path& path, Decode decode)
{
    const RecoveredFile file = recoverNewestValid(path, kSideBackups, [&](std::span<const std::uint8_t> b) { return decode(b).has_value(); });
    if (!file.found) {
        return std::nullopt;
    }
    return decode(file.bytes);
}

Loaded loadCampaign(const fs::path& root)
{
    Loaded out;
    const fs::path dir = root / kCampaignName;

    CampaignStatus status = CampaignStatus::Ok;
    const auto campaign = Campaign::open(root, kCampaignName, &status);
    Canon world;
    Canon slot;
    if (campaign && campaign->verifyWorld()) {
        const auto height = campaign->readBlob("heightmap.r16");
        const auto layouts = campaign->readBlob("dungeon_layouts.bin");
        if (height && layouts) {
            world = Bytes{ static_cast<std::uint8_t>(campaign->manifest().worldSeed), static_cast<std::uint8_t>(campaign->manifest().genVersion) };
            world->insert(world->end(), height->begin(), height->end());
            world->insert(world->end(), layouts->begin(), layouts->end());
        }
    }
    if (campaign) {
        Scene scene(kSceneSeed);
        SaveSlots slots = campaign->slots();
        const LoadResult result = slots.load(kSlotName, scene, contextOf(*campaign));
        if (result.status == LoadStatus::Loaded && !result.chronicleDamaged) {
            slot = sceneAndChronicle(scene, result.chronicle);
        }
    }
    out.parts["world"] = world;
    out.parts["slot"] = slot;

    out.parts["ledger"] = loadSide(dir / kLedger, [](std::span<const std::uint8_t> b) -> Canon {
        auto ledger = ConsequenceLedger::parse(b);
        if (!ledger) return std::nullopt;
        return ledger->serialize();
    });
    out.parts["deaths"] = loadSide(dir / kDeaths, [](std::span<const std::uint8_t> b) -> Canon {
        const auto r = decodeDeathMarkers(b);
        if (!r.ok()) return std::nullopt;
        return encodeDeathMarkers(r.set);
    });
    out.parts["notes"] = loadSide(dir / kNotes, [](std::span<const std::uint8_t> b) -> Canon {
        const auto r = decodeAnnotations(b);
        if (!r.ok()) return std::nullopt;
        return encodeAnnotations(r.store);
    });
    out.parts["share"] = loadSide(dir / kShare, [](std::span<const std::uint8_t> b) -> Canon {
        const auto r = decodeAnnotationShare(b);
        if (r.status != AnnotationShareStatus::Ok) return std::nullopt;
        const auto again = encodeAnnotationShare(r.store, r.seed, r.author);
        if (again.status != AnnotationShareStatus::Ok) return std::nullopt;
        return again.bytes;
    });
    out.parts["codex"] = loadSide(dir / kCodex, [](std::span<const std::uint8_t> b) -> Canon {
        const auto r = decodeCodex(b);
        if (!r.ok()) return std::nullopt;
        return encodeCodex(r.store);
    });
    out.parts["ironman"] = loadSide(dir / kIronman, [](std::span<const std::uint8_t> b) -> Canon {
        const auto run = IronmanRun::decode(std::string_view(reinterpret_cast<const char*>(b.data()), b.size()));
        if (!run) return std::nullopt;
        return toBytes(run->encode());
    });
    out.parts["tail"] = loadSide(dir / kTail, [](std::span<const std::uint8_t> b) -> Canon {
        const auto log = SnapshotTailLog::load(Bytes(b.begin(), b.end()));
        if (!log) return std::nullopt;
        return flatten(log->state());
    });

    const Settings settings = loadSettings(dir / kSettings);
    out.parts["settings"] = toBytes(serializeSettings(settings));

    ForgeCache cache(dir / "forge");
    cache.scan();
    for (const auto& [part, key] : { std::pair{ "forgeA", kKeyA }, std::pair{ "forgeB", kKeyB } }) {
        const ForgeGetResult got = cache.get(key);
        out.parts[part] = got.status == ForgeGetStatus::Hit ? Canon(got.payload) : std::nullopt;
    }
    return out;
}

struct Fixture {
    Dir dir;
    Model model;
    Loaded pristine;
    std::set<Bytes> tailStates;
    fs::path campaignDir() const { return dir.path() / kCampaignName; }
    fs::path file(const char* relative) const { return campaignDir() / relative; }

    Fixture()
    {
        buildModel(model);
        saveCampaign(dir.path(), model);
        pristine = loadCampaign(dir.path());
        tailStates.insert(model.tailHistory.begin(), model.tailHistory.end());
    }
};

const char* partOf(const std::string& relative)
{
    if (relative.rfind("world/", 0) == 0) return "world";
    if (relative == kSlotFile) return "slot";
    if (relative == kLedger) return "ledger";
    if (relative == kDeaths) return "deaths";
    if (relative == kNotes) return "notes";
    if (relative == kShare) return "share";
    if (relative == kCodex) return "codex";
    if (relative == kIronman) return "ironman";
    if (relative == kSettings) return "settings";
    if (relative == kTail) return "tail";
    return "";
}

// How a part is allowed to answer a damaged file.
enum class Policy {
    Checksummed, // refuse, or return exactly what was saved
    Slot,        // as above, or the previous save when the newest file is refused
    Canonical,   // refuse, return what was saved, or return a state whose canonical bytes are the file's own
    TailPrefix,  // refuse, or a state the log really passed through
    Tolerant,    // never refuses; the result must still be in range
};

Policy policyOf(const std::string& part)
{
    if (part == "world" || part == "share" || part == "forgeA" || part == "forgeB") return Policy::Checksummed;
    if (part == "slot") return Policy::Slot;
    if (part == "tail") return Policy::TailPrefix;
    if (part == "settings") return Policy::Tolerant;
    return Policy::Canonical;
}

// Empty when the loaded campaign is an acceptable answer to `mutated` having replaced the file of `owner`.
std::string judge(const Fixture& fx, const Loaded& got, const std::string& owner, const Canon& mutated)
{
    for (const auto& [part, expected] : fx.pristine.parts) {
        const Canon& actual = got.parts.at(part);
        const bool isOwner = part == owner || (owner == "world" && part == "slot");
        if (!isOwner) {
            if (actual != expected) return "an untouched part '" + part + "' changed";
            continue;
        }
        if (actual == expected) continue;
        switch (policyOf(part)) {
        case Policy::Checksummed:
            if (actual) return "'" + part + "' loaded different data";
            break;
        case Policy::Slot:
            if (actual && *actual != fx.model.earlierSceneAndChronicle) return "'slot' loaded a state it never saved";
            break;
        case Policy::Canonical:
            if (actual && (!mutated || *actual != *mutated)) return "'" + part + "' loaded a state its own file does not spell";
            break;
        case Policy::TailPrefix:
            if (actual && !fx.tailStates.contains(*actual)) return "'tail' loaded a state the log never had";
            break;
        case Policy::Tolerant: {
            const Settings parsed = parseSettings(std::string(actual->begin(), actual->end()));
            if (!(parsed == parsed.clamped())) return "'settings' came back out of range";
            break;
        }
        }
    }
    return {};
}

std::vector<std::size_t> offsetsFor(std::size_t size, std::size_t budget)
{
    std::vector<std::size_t> offsets;
    if (size <= budget) {
        for (std::size_t i = 0; i < size; ++i) offsets.push_back(i);
        return offsets;
    }
    const std::size_t edge = budget / 4;
    for (std::size_t i = 0; i < edge; ++i) offsets.push_back(i);
    for (std::size_t i = size - edge; i < size; ++i) offsets.push_back(i);
    const std::size_t middle = budget - edge * 2;
    for (std::size_t i = 1; i <= middle; ++i) offsets.push_back(edge + (size - edge * 2) * i / (middle + 1));
    std::sort(offsets.begin(), offsets.end());
    offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
    return offsets;
}

struct Sweep {
    std::size_t cases = 0;
    std::vector<std::string> failures;
};

Sweep sweepFile(Fixture& fx, const char* relative, std::size_t budget = 40)
{
    Sweep sweep;
    const fs::path path = fx.file(relative);
    const Bytes original = readFile(path);
    REQUIRE_FALSE(original.empty());
    const std::string owner = partOf(relative);

    auto attempt = [&](const Bytes& mutated, bool remove, const std::string& what) {
        if (!remove && mutated == original) return;
        if (remove) {
            fs::remove(path);
        } else {
            writeFile(path, mutated);
        }
        ++sweep.cases;
        const Loaded got = loadCampaign(fx.dir.path());
        const std::string problem = owner.empty() ? "unmapped file" : judge(fx, got, owner, Canon(mutated));
        if (!problem.empty() && sweep.failures.size() < 8) {
            sweep.failures.push_back(std::string(relative) + " " + what + ": " + problem);
        }
        writeFile(path, original);
    };

    attempt({}, true, "deleted");
    attempt({}, false, "emptied");
    for (const std::size_t at : offsetsFor(original.size(), budget)) {
        attempt(Bytes(original.begin(), original.begin() + static_cast<std::ptrdiff_t>(at)), false, "truncated to " + std::to_string(at));
        for (const std::uint8_t mask : { std::uint8_t{ 0x01 }, std::uint8_t{ 0x80 }, std::uint8_t{ 0xff } }) {
            Bytes m = original;
            m[at] = static_cast<std::uint8_t>(m[at] ^ mask);
            attempt(m, false, "byte " + std::to_string(at) + " xor " + std::to_string(mask));
        }
    }
    Bytes appended = original;
    appended.push_back(0);
    attempt(appended, false, "one byte appended");
    Bytes doubled = original;
    doubled.insert(doubled.end(), original.begin(), original.end());
    attempt(doubled, false, "doubled");
    return sweep;
}

}

TEST_CASE("a campaign touching every save module reloads to the same bytes", "[campaign_roundtrip]")
{
    Fixture fx;
    REQUIRE(Campaign::list(fx.dir.path()) == std::vector<std::string>{ kCampaignName });

    for (const auto& [part, canon] : fx.pristine.parts) {
        INFO(part);
        REQUIRE(canon.has_value());
    }

    // Every codec file on disk is the canonical encoding of what it decoded to.
    const std::map<std::string, const char*> files = {
        { "ledger", kLedger }, { "deaths", kDeaths }, { "notes", kNotes }, { "share", kShare }, { "codex", kCodex }, { "ironman", kIronman },
        { "settings", kSettings },
    };
    for (const auto& [part, relative] : files) {
        INFO(part);
        REQUIRE(*fx.pristine.parts.at(part) == readFile(fx.file(relative)));
    }
    REQUIRE(fx.pristine.parts.at("slot") == sceneAndChronicle(fx.model.scene, fx.model.chronicle));
    REQUIRE(fx.pristine.parts.at("tail") == flatten(fx.model.tail->state()));
    REQUIRE(*fx.pristine.parts.at("forgeA") == fx.model.forgeA);
    REQUIRE(*fx.pristine.parts.at("forgeB") == fx.model.forgeB);

    // The decoded objects match the model, not just their own re-encoding.
    const auto deaths = decodeDeathMarkers(readFile(fx.file(kDeaths)));
    REQUIRE(deaths.ok());
    REQUIRE(deaths.set == fx.model.deaths);
    const auto notes = decodeAnnotations(readFile(fx.file(kNotes)));
    REQUIRE(notes.ok());
    REQUIRE(notes.store.size() == fx.model.notes.size());
    REQUIRE(std::equal(notes.store.all().begin(), notes.store.all().end(), fx.model.notes.all().begin(),
        [](const MapAnnotation& a, const MapAnnotation& b) {
            return a.id == b.id && a.kind == b.kind && a.symbol == b.symbol && a.text == b.text && a.points.size() == b.points.size();
        }));
    const auto codex = decodeCodex(readFile(fx.file(kCodex)));
    REQUIRE(codex.ok());
    REQUIRE(codex.store.size() == fx.model.codex.size());
    const Bytes ironmanBytes = readFile(fx.file(kIronman));
    const auto ironman = IronmanRun::decode(std::string(ironmanBytes.begin(), ironmanBytes.end()));
    REQUIRE(ironman.has_value());
    REQUIRE_FALSE(ironman->alive());
    REQUIRE(*ironman->deathTick() == 900);
    REQUIRE(loadSettings(fx.file(kSettings)) == fx.model.settings);

    // The slot restores the very same scene and the story up to the save.
    const auto campaign = Campaign::open(fx.dir.path(), kCampaignName);
    REQUIRE(campaign.has_value());
    Scene restored(kSceneSeed);
    SaveSlots slots = campaign->slots();
    const LoadResult loaded = slots.load(kSlotName, restored, contextOf(*campaign));
    REQUIRE(loaded.status == LoadStatus::Loaded);
    REQUIRE_FALSE(loaded.fromBackup);
    REQUIRE_FALSE(loaded.chronicleDamaged);
    REQUIRE(restored.stateHash() == fx.model.scene.stateHash());
    REQUIRE(loaded.chronicle == fx.model.chronicle);

    // The snapshot-plus-tail file replays to every step it was built from.
    TailLoadResult tailResult;
    const auto log = SnapshotTailLog::load(readFile(fx.file(kTail)), &tailResult);
    REQUIRE(log.has_value());
    REQUIRE(tailResult.status == TailLoadStatus::Clean);
    REQUIRE(tailResult.recordsApplied == 3);
    REQUIRE(log->state() == stateStep(3));
    REQUIRE(log->snapshot() == stateStep(0));
}

TEST_CASE("saving the reloaded campaign again writes the same files", "[campaign_roundtrip]")
{
    Fixture fx;
    const auto campaign = Campaign::open(fx.dir.path(), kCampaignName);
    REQUIRE(campaign.has_value());

    Dir second;
    const auto height = campaign->readBlob("heightmap.r16");
    const auto layouts = campaign->readBlob("dungeon_layouts.bin");
    REQUIRE((height && layouts));
    const std::vector<WorldBlob> blobs = { { "heightmap.r16", *height }, { "dungeon_layouts.bin", *layouts } };
    REQUIRE(Campaign::create(second.path(), kCampaignName, campaign->manifest().worldSeed, campaign->manifest().genVersion, blobs) == CampaignStatus::Ok);
    const auto reforged = Campaign::open(second.path(), kCampaignName);
    REQUIRE(reforged.has_value());

    Scene restored(kSceneSeed);
    const LoadResult loaded = campaign->slots().load(kSlotName, restored, contextOf(*campaign));
    REQUIRE(loaded.status == LoadStatus::Loaded);
    SaveSlots again = reforged->slots();
    REQUIRE(again.save(kSlotName, restored, contextOf(*reforged), loaded.chronicle));

    for (const char* relative : { kManifest, kHeight, kLayouts, kSlotFile }) {
        INFO(relative);
        REQUIRE(readFile(second.path() / kCampaignName / relative) == readFile(fx.file(relative)));
    }
}

TEST_CASE("a friend's shared map merges into the saved one and the result saves and reloads", "[campaign_roundtrip]")
{
    Fixture fx;
    const auto theirs = decodeAnnotationShare(readFile(fx.file(kShare)));
    REQUIRE(theirs.status == AnnotationShareStatus::Ok);
    REQUIRE(theirs.seed == "seed-77");

    auto own = decodeAnnotations(readFile(fx.file(kNotes)));
    REQUIRE(own.ok());
    const std::size_t before = own.store.size();
    const AnnotationImportReport report = mergeSharedAnnotations(own.store, theirs.store);
    REQUIRE(report.added == 2);
    REQUIRE(report.renamed == 1);
    REQUIRE(report.dropped == 0);
    REQUIRE(own.store.size() == before + 2);
    REQUIRE(own.store.find("camp-1-2") != nullptr);
    REQUIRE(own.store.find("camp-1")->text == "safe");

    const Bytes merged = encodeAnnotations(own.store);
    putAtomic(fx.file(kNotes), merged);
    const Loaded after = loadCampaign(fx.dir.path());
    REQUIRE(after.parts.at("notes") == Canon(merged));
    REQUIRE(after.parts.at("notes") != fx.pristine.parts.at("notes"));
}

TEST_CASE("a crash at every stage of a side-file write leaves the old state loadable", "[campaign_roundtrip]")
{
    Fixture fx;
    const fs::path path = fx.file(kDeaths);
    const Bytes old = readFile(path);

    DeathMarkerSet newer = fx.model.deaths;
    REQUIRE(newer.add({ 5, 5, 5, DeathCause::Starvation, 20 }) == DeathAddResult::Added);
    const Bytes next = encodeDeathMarkers(newer);

    for (const AtomicStage stage : { AtomicStage::TempPartial, AtomicStage::TempComplete, AtomicStage::BackupsRotated }) {
        AtomicWriteOptions options;
        options.backups = kSideBackups;
        options.interruptAt = stage;
        options.valid = [](std::span<const std::uint8_t> b) { return decodeDeathMarkers(b).ok(); };
        REQUIRE(writeAtomic(path, next, options).status == AtomicWriteStatus::Interrupted);
        const Loaded got = loadCampaign(fx.dir.path());
        INFO(static_cast<int>(stage));
        REQUIRE(got.parts.at("deaths") == fx.pristine.parts.at("deaths"));
        REQUIRE(readFile(path) == old);
    }
    for (const auto& [part, expected] : fx.pristine.parts) {
        REQUIRE(loadCampaign(fx.dir.path()).parts.at(part) == expected);
    }

    putAtomic(path, next);
    REQUIRE(*loadCampaign(fx.dir.path()).parts.at("deaths") == next);
    // Damage the newest file: the loader falls back to the previous good one, never to the damaged bytes.
    Bytes damaged = next;
    damaged[damaged.size() / 2] ^= 0xff;
    damaged.resize(damaged.size() - 3);
    writeFile(path, damaged);
    REQUIRE(*loadCampaign(fx.dir.path()).parts.at("deaths") == old);
}

TEST_CASE("damaging the slot falls back to the previous save, and with no backup it is refused", "[campaign_roundtrip]")
{
    Fixture fx;
    const fs::path slot = fx.file(kSlotFile);
    const fs::path backup = fx.campaignDir() / "slots" / "main.bak1";
    REQUIRE(fs::exists(backup));

    Bytes bytes = readFile(slot);
    bytes[bytes.size() / 2] ^= 0x10;
    writeFile(slot, bytes);

    const auto campaign = Campaign::open(fx.dir.path(), kCampaignName);
    REQUIRE(campaign.has_value());
    Scene scene(kSceneSeed);
    const LoadResult recovered = campaign->slots().load(kSlotName, scene, contextOf(*campaign));
    REQUIRE(recovered.status == LoadStatus::Loaded);
    REQUIRE(recovered.fromBackup);
    REQUIRE(sceneAndChronicle(scene, recovered.chronicle) == fx.model.earlierSceneAndChronicle);

    Bytes older = readFile(backup);
    older.resize(older.size() / 2);
    writeFile(backup, older);
    Scene untouched(kSceneSeed);
    const std::uint64_t before = untouched.stateHash();
    const LoadResult refused = campaign->slots().load(kSlotName, untouched, contextOf(*campaign));
    REQUIRE(refused.status == LoadStatus::Corrupt);
    REQUIRE(untouched.stateHash() == before);
}

TEST_CASE("a slot saved in another world is refused and leaves the scene alone", "[campaign_roundtrip]")
{
    Fixture fx;
    const auto campaign = Campaign::open(fx.dir.path(), kCampaignName);
    REQUIRE(campaign.has_value());
    SaveContext other = contextOf(*campaign);
    other.worldSeed += 1;
    Scene scene(kSceneSeed);
    const std::uint64_t before = scene.stateHash();
    REQUIRE(campaign->slots().load(kSlotName, scene, other).status == LoadStatus::WrongWorld);
    REQUIRE(scene.stateHash() == before);
    other = contextOf(*campaign);
    other.genVersion += 1;
    REQUIRE(campaign->slots().load(kSlotName, scene, other).status == LoadStatus::WrongWorld);
    REQUIRE(scene.stateHash() == before);
}

TEST_CASE("every file truncated, flipped, extended or deleted is refused or recovers to a real state", "[campaign_roundtrip][corruption]")
{
    Fixture fx;
    const std::vector<const char*> files = { kManifest, kHeight, kLayouts, kSlotFile, kLedger, kDeaths, kNotes, kShare, kCodex, kIronman, kSettings, kTail };
    std::size_t total = 0;
    for (const char* relative : files) {
        const Sweep sweep = sweepFile(fx, relative);
        total += sweep.cases;
        INFO(relative << ": " << sweep.cases << " mutations");
        for (const std::string& failure : sweep.failures) {
            INFO(failure);
            CHECK(failure.empty());
        }
        // The sweep restored the file, so the next one starts from the saved campaign.
        REQUIRE(loadCampaign(fx.dir.path()).parts == fx.pristine.parts);
    }
    REQUIRE(total > 1000);
}

TEST_CASE("a damaged forge cache entry is dropped on scan and the other entry survives", "[campaign_roundtrip][corruption]")
{
    Fixture fx;
    ForgeCache probe(fx.campaignDir() / "forge");
    const fs::path pathA = probe.pathFor(kKeyA);
    const Bytes original = readFile(pathA);
    REQUIRE_FALSE(original.empty());

    std::size_t cases = 0;
    std::vector<std::string> failures;
    auto attempt = [&](const Bytes& mutated, bool remove, const std::string& what) {
        if (remove) {
            fs::remove(pathA);
        } else {
            writeFile(pathA, mutated);
        }
        ++cases;
        ForgeCache cache(fx.campaignDir() / "forge");
        const ForgeScanReport report = cache.scan();
        const ForgeGetResult a = cache.get(kKeyA);
        const ForgeGetResult b = cache.get(kKeyB);
        if (b.status != ForgeGetStatus::Hit || b.payload != fx.model.forgeB) failures.push_back(what + ": entry B changed");
        if (a.status == ForgeGetStatus::Hit && a.payload != fx.model.forgeA) failures.push_back(what + ": entry A served other data");
        if (a.status != ForgeGetStatus::Hit && cache.contains(kKeyA)) failures.push_back(what + ": a refused entry is still indexed");
        if (!remove && mutated != original && a.status == ForgeGetStatus::Hit) failures.push_back(what + ": a damaged file was served");
        if (!remove && mutated != original && report.discarded == 0) failures.push_back(what + ": scan kept a damaged file");
        writeFile(pathA, original);
    };
    attempt({}, true, "deleted");
    for (const std::size_t at : offsetsFor(original.size(), 100)) {
        attempt(Bytes(original.begin(), original.begin() + static_cast<std::ptrdiff_t>(at)), false, "truncated to " + std::to_string(at));
        Bytes m = original;
        m[at] ^= 0x04;
        attempt(m, false, "bit flipped at " + std::to_string(at));
    }
    for (std::size_t i = 0; i < std::min<std::size_t>(failures.size(), 8); ++i) {
        INFO(failures[i]);
        CHECK(failures[i].empty());
    }
    REQUIRE(cases > 50);
    REQUIRE(failures.empty());
}

TEST_CASE("fuzzing the bytes read back from disk never throws or over-allocates", "[campaign_roundtrip][fuzz]")
{
    Fixture fx;
    ichor::fuzz::Options options;
    options.randomMutations = 800;

    // A flipped sign bit on a 0.0 coordinate is stored as +0.0 by design (MapAnnotationStore::add).
    ichor::fuzz::Options normalising = options;
    normalising.allowNormalising = true;

    auto expectClean = [](const ichor::fuzz::Report& report) {
        INFO(ichor::fuzz::summary(report));
        CHECK(report.ok());
    };
    using ichor::fuzz::Canonical;
    expectClean(ichor::fuzz::fuzzCodec(readFile(fx.file(kShare)), [](std::span<const std::uint8_t> b) -> Canonical {
        const auto r = decodeAnnotationShare(b);
        if (r.status != AnnotationShareStatus::Ok) return std::nullopt;
        const auto again = encodeAnnotationShare(r.store, r.seed, r.author);
        if (again.status != AnnotationShareStatus::Ok) return std::nullopt;
        return again.bytes;
    }, options));
    expectClean(ichor::fuzz::fuzzCodec(readFile(fx.file(kNotes)), [](std::span<const std::uint8_t> b) -> Canonical {
        const auto r = decodeAnnotations(b);
        if (!r.ok()) return std::nullopt;
        return encodeAnnotations(r.store);
    }, normalising));
    expectClean(ichor::fuzz::fuzzCodec(readFile(fx.file(kCodex)), [](std::span<const std::uint8_t> b) -> Canonical {
        const auto r = decodeCodex(b);
        if (!r.ok()) return std::nullopt;
        return encodeCodex(r.store);
    }, options));
    expectClean(ichor::fuzz::fuzzCodec(readFile(fx.file(kLedger)), [](std::span<const std::uint8_t> b) -> Canonical {
        auto ledger = ConsequenceLedger::parse(b);
        if (!ledger) return std::nullopt;
        return ledger->serialize();
    }, options));
    expectClean(ichor::fuzz::fuzzCodec(readFile(fx.file(kDeaths)), [](std::span<const std::uint8_t> b) -> Canonical {
        const auto r = decodeDeathMarkers(b);
        if (!r.ok()) return std::nullopt;
        return encodeDeathMarkers(r.set);
    }, options));
}
