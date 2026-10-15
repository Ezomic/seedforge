#include "ichor/data/data_set.h"
#include "ichor/economy/wallet.h"
#include "ichor/factions/standing.h"
#include "ichor/ledger/consequence_ledger.h"
#include "ichor/progression/capabilities.h"
#include "ichor/save/chronicle.h"
#include "ichor/save/save_slots.h"
#include "ichor/save/settings.h"
#include "ichor/sim/scene.h"
#include "test_temp.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace ichor;

namespace {

constexpr double kDt = 1.0 / 60.0;
constexpr std::uint64_t kSeed = 5;
const SaveContext kContext{77, 1, 1234};

using Bytes = std::vector<std::uint8_t>;

class Dir {
public:
    Dir()
    {
        static int counter = 0;
        path_ = test::tempPath("ichor_roundtrip_" + std::to_string(counter++));
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }
    ~Dir() { std::filesystem::remove_all(path_); }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

Bytes readBytes(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeBytes(const std::filesystem::path& path, const Bytes& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

const std::vector<Faction> kFactions = {
    {"archonate", "The Archonate", FactionRole::Incumbent},
    {"archaists", "The Archaists", FactionRole::Challenger},
    {"mnemones", "The Mnemones", FactionRole::Custodian},
};

std::vector<StandingTier> tiers()
{
    std::vector<StandingTier> out;
    std::vector<std::string> problems;
    REQUIRE(parseStandingTiers("t.json", R"([
        {"id": "hostile", "name": "Hostile", "threshold": -1000},
        {"id": "neutral", "name": "Neutral", "threshold": 0},
        {"id": "allied", "name": "Allied", "threshold": 750}])", out, problems));
    return out;
}

CurrencyTable currencies()
{
    DataSet data;
    REQUIRE(data.loadDirectory(ICHOR_GAME_DATA_DIR));
    const DataTable* table = data.table("currency");
    REQUIRE(table != nullptr);
    std::string error;
    auto result = CurrencyTable::fromTable(*table, error);
    INFO(error);
    REQUIRE(result.has_value());
    return *result;
}

std::vector<Capability> capabilityTable()
{
    std::ifstream file(std::filesystem::path(ICHOR_GAME_DATA_DIR) / "progression" / "capabilities.json", std::ios::binary);
    REQUIRE(file.good());
    const std::string text((std::istreambuf_iterator<char>(file)), {});
    std::vector<Capability> table;
    std::vector<std::string> problems;
    REQUIRE(parseCapabilities("c.json", text, table, problems));
    return table;
}

// Everything the game keeps between sessions that exists on main today.
struct Campaign {
    Scene scene{kSeed};
    Chronicle chronicle;
    ConsequenceLedger ledger;
    FactionStanding standing{kFactions, tiers(), {}};
    Wallet wallet;
    CapabilityUnlocks capabilities{capabilityTable()};
    Settings settings;
};

void play(Campaign& c)
{
    SpawnCommand command;
    command.id = StableId::of("well");
    command.name.value = "well";
    command.transform.position = {1.0, 0.0, 0.0};
    c.scene.enqueue(command);
    for (int i = 0; i < 45; ++i) {
        c.scene.tick(kDt);
    }

    REQUIRE(c.chronicle.add(0, "start", "A campaign begins."));
    REQUIRE(c.chronicle.add(120, "quest", "Tabs\tand\nnewlines, and caf\xc3\xa9 text.", {{9, 3, 3, 12}, 7, 200}));
    REQUIRE(c.chronicle.add(900, "death", "Someone fell."));

    REQUIRE(c.ledger.append({"f1", "npc.7", "killed", "by.player", 100}) == AppendResult::Ok);
    REQUIRE(c.ledger.append({"f2", "town:a", "burned", "", 200}) == AppendResult::Ok);
    REQUIRE(c.ledger.append({"f3", "npc.7", "mourned", "town:a", 300}) == AppendResult::Ok);

    REQUIRE(c.standing.change("archonate", -400).has_value());
    REQUIRE(c.standing.change("archaists", 5000).has_value()); // saturates
    REQUIRE(c.standing.change("mnemones", 1).has_value());

    const CurrencyTable table = currencies();
    REQUIRE(c.wallet.earn(table, CurrencySlot::Coin, 12345, "loot").status == TxStatus::Ok);
    REQUIRE(c.wallet.earn(table, CurrencySlot::Hard, 3, "loot").status == TxStatus::Ok);

    REQUIRE(c.capabilities.unlock("swim") == UnlockResult::Unlocked);
    REQUIRE(c.capabilities.unlock("dive") == UnlockResult::Unlocked);
    REQUIRE(c.capabilities.unlock("climb") == UnlockResult::Unlocked);

    c.settings.fovDegrees = 72.5f;
    c.settings.mouseSensitivity = 2.25f;
}

struct Digests {
    std::uint64_t scene = 0, ledger = 0, standing = 0, wallet = 0, capabilities = 0;
    bool operator==(const Digests&) const = default;
};

Digests digestsOf(const Campaign& c)
{
    return {c.scene.stateHash(), c.ledger.digest(), c.standing.digest(), c.wallet.digest(), c.capabilities.digest()};
}

constexpr const char* kSlot = "campaign";

bool saveCampaign(const std::filesystem::path& dir, const Campaign& c, SavePolicy policy = {})
{
    std::filesystem::create_directories(dir);
    SaveSlots slots(dir, policy);
    if (!slots.save(kSlot, c.scene, kContext, c.chronicle)) {
        return false;
    }
    writeBytes(dir / "ledger.bin", c.ledger.serialize());
    writeBytes(dir / "standing.bin", c.standing.serialise());
    writeBytes(dir / "wallet.bin", c.wallet.serialise());
    writeBytes(dir / "capabilities.bin", c.capabilities.serialise());
    return saveSettings(dir / "settings.txt", c.settings);
}

// Loads every part into a fresh campaign. The slot status is returned; the side files must load or the test fails.
LoadResult loadCampaign(const std::filesystem::path& dir, Campaign& out, SavePolicy policy = {})
{
    SaveSlots slots(dir, policy);
    LoadResult result = slots.load(kSlot, out.scene, kContext);
    if (result.status != LoadStatus::Loaded) {
        return result;
    }
    out.chronicle = result.chronicle;

    const Bytes ledgerBytes = readBytes(dir / "ledger.bin");
    auto ledger = ConsequenceLedger::parse(ledgerBytes);
    REQUIRE(ledger.has_value());
    out.ledger = std::move(*ledger);

    REQUIRE(out.standing.deserialise(readBytes(dir / "standing.bin")));

    auto wallet = Wallet::deserialise(readBytes(dir / "wallet.bin"), currencies());
    REQUIRE(wallet.has_value());
    out.wallet = *wallet;

    REQUIRE(out.capabilities.deserialise(readBytes(dir / "capabilities.bin")));
    out.settings = loadSettings(dir / "settings.txt");
    return result;
}

void requireSameFiles(const std::filesystem::path& a, const std::filesystem::path& b)
{
    for (const char* name : {"campaign.sav", "ledger.bin", "standing.bin", "wallet.bin", "capabilities.bin", "settings.txt"}) {
        INFO(name);
        const Bytes first = readBytes(a / name);
        REQUIRE_FALSE(first.empty());
        REQUIRE(first == readBytes(b / name));
    }
}

// Whatever a mutated blob does, it must not crash, and when it is accepted it must be the canonical form of
// what it decoded to, so no two byte strings stand for one state.
template <typename Accept, typename Reserialise>
void mutateAll(const Bytes& good, Accept accept, Reserialise reserialise)
{
    for (std::size_t length = 0; length < good.size(); ++length) {
        INFO("truncated to " << length);
        CHECK_FALSE(accept(Bytes(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(length))));
    }
    Bytes padded = good;
    padded.push_back(0);
    CHECK_FALSE(accept(padded));

    for (std::size_t i = 0; i < good.size(); ++i) {
        for (const std::uint8_t mask : {std::uint8_t{0x01}, std::uint8_t{0x80}, std::uint8_t{0xff}}) {
            Bytes bytes = good;
            bytes[i] = static_cast<std::uint8_t>(bytes[i] ^ mask);
            INFO("byte " << i << " xor " << static_cast<int>(mask));
            if (accept(bytes)) {
                CHECK(reserialise() == bytes);
            }
        }
    }
}

}

TEST_CASE("save, load, save is byte-identical for everything saved, and the digests do not move", "[save_roundtrip]")
{
    const Dir first;
    const Dir second;
    Campaign original;
    play(original);
    REQUIRE(saveCampaign(first.path(), original));

    Campaign restored;
    const LoadResult result = loadCampaign(first.path(), restored);
    REQUIRE(result.status == LoadStatus::Loaded);
    REQUIRE_FALSE(result.fromBackup);
    REQUIRE_FALSE(result.chronicleDamaged);
    REQUIRE(result.info.tick == 45);

    CHECK(digestsOf(restored) == digestsOf(original));
    CHECK(restored.chronicle == original.chronicle);
    CHECK(restored.chronicle.serialize() == original.chronicle.serialize());
    CHECK(restored.ledger.facts() == original.ledger.facts());
    CHECK(restored.wallet == original.wallet);
    CHECK(restored.capabilities.unlocked() == original.capabilities.unlocked());
    CHECK(restored.settings == original.settings);

    REQUIRE(saveCampaign(second.path(), restored));
    requireSameFiles(first.path(), second.path());
}

TEST_CASE("the digests are not trivially constant: each moves when its state does", "[save_roundtrip]")
{
    Campaign a;
    play(a);
    Campaign b;
    play(b);
    REQUIRE(digestsOf(a) == digestsOf(b));

    a.scene.tick(kDt);
    REQUIRE(a.ledger.append({"f4", "x", "y", "z", 1}) == AppendResult::Ok);
    REQUIRE(a.standing.change("mnemones", 10).has_value());
    REQUIRE(a.wallet.earn(currencies(), CurrencySlot::Coin, 1, "loot").status == TxStatus::Ok);
    REQUIRE(a.capabilities.unlock("boat") == UnlockResult::Unlocked);

    const Digests da = digestsOf(a);
    const Digests db = digestsOf(b);
    CHECK(da.scene != db.scene);
    CHECK(da.ledger != db.ledger);
    CHECK(da.standing != db.standing);
    CHECK(da.wallet != db.wallet);
    CHECK(da.capabilities != db.capabilities);
}

TEST_CASE("a second round trip after more play is still identical, and the first save stays loadable", "[save_roundtrip]")
{
    const Dir dir;
    const Dir copy;
    Campaign original;
    play(original);
    REQUIRE(saveCampaign(dir.path(), original));

    Campaign restored;
    REQUIRE(loadCampaign(dir.path(), restored).status == LoadStatus::Loaded);
    for (int i = 0; i < 20; ++i) {
        restored.scene.tick(kDt);
        original.scene.tick(kDt);
    }
    REQUIRE(restored.scene.stateHash() == original.scene.stateHash());

    REQUIRE(saveCampaign(dir.path(), restored));
    Campaign again;
    const LoadResult result = loadCampaign(dir.path(), again);
    REQUIRE(result.status == LoadStatus::Loaded);
    REQUIRE(result.info.tick == 65);
    REQUIRE(again.scene.stateHash() == original.scene.stateHash());

    REQUIRE(saveCampaign(copy.path(), again));
    requireSameFiles(dir.path(), copy.path());
}

TEST_CASE("an empty campaign round-trips", "[save_roundtrip]")
{
    const Dir first;
    const Dir second;
    Campaign original;
    REQUIRE(saveCampaign(first.path(), original));
    Campaign restored;
    REQUIRE(loadCampaign(first.path(), restored).status == LoadStatus::Loaded);
    CHECK(digestsOf(restored) == digestsOf(original));
    CHECK(restored.chronicle.empty());
    REQUIRE(saveCampaign(second.path(), restored));
    requireSameFiles(first.path(), second.path());
}

TEST_CASE("a campaign at its limits round-trips: maximum standing, wallet at the cap, every capability", "[save_roundtrip]")
{
    const Dir first;
    const Dir second;
    Campaign original;
    REQUIRE(original.standing.change("archonate", 1000000).has_value());
    REQUIRE(original.standing.change("archaists", -1000000).has_value());
    const CurrencyTable table = currencies();
    (void)original.wallet.earn(table, CurrencySlot::Coin, std::numeric_limits<std::uint64_t>::max(), "loot");
    (void)original.wallet.earn(table, CurrencySlot::Hard, std::numeric_limits<std::uint64_t>::max(), "loot");
    for (const Capability& capability : capabilityTable()) {
        REQUIRE(original.capabilities.unlock(capability.id) != UnlockResult::UnknownCapability);
    }
    original.settings.fovDegrees = Settings::kMaxFovDegrees;
    original.settings.mouseSensitivity = Settings::kMinMouseSensitivity;

    REQUIRE(saveCampaign(first.path(), original));
    Campaign restored;
    REQUIRE(loadCampaign(first.path(), restored).status == LoadStatus::Loaded);
    CHECK(digestsOf(restored) == digestsOf(original));
    CHECK(restored.standing.standing("archaists") == kStandingMin);
    CHECK(restored.settings == original.settings);
    REQUIRE(saveCampaign(second.path(), restored));
    requireSameFiles(first.path(), second.path());
}

TEST_CASE("a slot carries the world seed it was saved under, and another seed is refused", "[save_roundtrip]")
{
    const Dir dir;
    Campaign original;
    play(original);
    REQUIRE(saveCampaign(dir.path(), original));

    SaveSlots slots(dir.path());
    Scene scene(kSeed);
    SaveContext other = kContext;
    other.worldSeed = kContext.worldSeed + 1;
    CHECK(slots.load(kSlot, scene, other).status == LoadStatus::WrongWorld);
    other = kContext;
    other.genVersion += 1;
    CHECK(slots.load(kSlot, scene, other).status == LoadStatus::WrongWorld);
    other = kContext;
    other.dataDigest += 1;
    const LoadResult changed = slots.load(kSlot, scene, other);
    CHECK(changed.status == LoadStatus::Loaded);
    CHECK(changed.dataChanged);
    CHECK(scene.stateHash() == original.scene.stateHash());
}

TEST_CASE("a slot with no backup is corrupt after any truncation or any single flipped byte", "[save_roundtrip]")
{
    const Dir dir;
    Campaign original;
    play(original);
    REQUIRE(saveCampaign(dir.path(), original));
    const Bytes good = readBytes(dir.path() / "campaign.sav");
    REQUIRE(good.size() > 64);

    const auto statusOf = [&](const Bytes& bytes) {
        writeBytes(dir.path() / "campaign.sav", bytes);
        SaveSlots slots(dir.path());
        Scene scene(kSeed);
        return slots.load(kSlot, scene, kContext).status;
    };

    for (std::size_t length = 0; length < good.size(); ++length) {
        INFO("truncated to " << length);
        CHECK(statusOf(Bytes(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(length))) != LoadStatus::Loaded);
    }
    Bytes padded = good;
    padded.push_back(0);
    CHECK(statusOf(padded) != LoadStatus::Loaded);

    for (std::size_t i = 0; i < good.size(); ++i) {
        Bytes bytes = good;
        bytes[i] = static_cast<std::uint8_t>(bytes[i] ^ 0x10);
        INFO("flipped byte " << i);
        CHECK(statusOf(bytes) == LoadStatus::Corrupt);
    }

    CHECK(statusOf(good) == LoadStatus::Loaded);
}

TEST_CASE("a corrupt newest slot falls back to the previous save and the whole campaign still round-trips", "[save_roundtrip]")
{
    const Dir dir;
    const Dir copy;
    Campaign original;
    play(original);
    REQUIRE(saveCampaign(dir.path(), original));
    const Digests atFirstSave = digestsOf(original);

    original.scene.tick(kDt);
    REQUIRE(saveCampaign(dir.path(), original));

    Bytes damaged = readBytes(dir.path() / "campaign.sav");
    damaged[damaged.size() / 2] = static_cast<std::uint8_t>(damaged[damaged.size() / 2] ^ 0xff);
    writeBytes(dir.path() / "campaign.sav", damaged);

    Campaign restored;
    const LoadResult result = loadCampaign(dir.path(), restored);
    REQUIRE(result.status == LoadStatus::Loaded);
    CHECK(result.fromBackup);
    CHECK(result.info.tick == 45);
    CHECK(restored.scene.stateHash() == atFirstSave.scene);
    CHECK(restored.chronicle == original.chronicle);

    REQUIRE(saveCampaign(copy.path(), restored));
    Campaign again;
    REQUIRE(loadCampaign(copy.path(), again).status == LoadStatus::Loaded);
    CHECK(again.scene.stateHash() == atFirstSave.scene);
}

TEST_CASE("a slot with a wrong version is refused and left byte-identical on disk", "[save_roundtrip]")
{
    const Dir dir;
    Campaign original;
    play(original);
    REQUIRE(saveCampaign(dir.path(), original));

    const Bytes good = readBytes(dir.path() / "campaign.sav");
    for (const std::uint32_t format : {0u, MigrationRegistry::standard().current() + 1, 0xffffffffu}) {
        Bytes bytes = good;
        for (int i = 0; i < 4; ++i) {
            bytes[4 + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(format >> (8 * i));
        }
        writeBytes(dir.path() / "campaign.sav", bytes);

        SaveSlots slots(dir.path());
        Scene scene(kSeed);
        INFO("format " << format);
        // The checksum covers the header, so a rewritten version is corrupt, never silently accepted.
        CHECK(slots.load(kSlot, scene, kContext).status != LoadStatus::Loaded);
        CHECK(readBytes(dir.path() / "campaign.sav") == bytes);
    }
}

TEST_CASE("a slot written under an older format migrates, and saving the migrated campaign round-trips", "[save_roundtrip]")
{
    static const MigrationRegistry kOld(1, {});
    static const MigrationRegistry kNew(2, {{1, [](SlotBody&) { return true; }}});
    SavePolicy oldPolicy;
    oldPolicy.migrations = &kOld;
    SavePolicy newPolicy;
    newPolicy.migrations = &kNew;

    const Dir dir;
    const Dir copy;
    Campaign original;
    play(original);
    REQUIRE(saveCampaign(dir.path(), original, oldPolicy));

    Campaign restored;
    const LoadResult result = loadCampaign(dir.path(), restored, newPolicy);
    REQUIRE(result.status == LoadStatus::Loaded);
    CHECK(result.migratedFrom == 1);
    CHECK(digestsOf(restored) == digestsOf(original));
    CHECK(restored.chronicle == original.chronicle);

    REQUIRE(saveCampaign(copy.path(), restored, newPolicy));
    Campaign again;
    const LoadResult second = loadCampaign(copy.path(), again, newPolicy);
    REQUIRE(second.status == LoadStatus::Loaded);
    CHECK(second.migratedFrom == 0);
    CHECK(digestsOf(again) == digestsOf(original));

    // An old build must not open what the new one wrote.
    SaveSlots oldSlots(copy.path(), oldPolicy);
    Scene scene(kSeed);
    CHECK(oldSlots.load(kSlot, scene, kContext).status == LoadStatus::UnsupportedVersion);
}

TEST_CASE("a refused migration changes nothing on disk and nothing in the scene", "[save_roundtrip]")
{
    static const MigrationRegistry kOld(1, {});
    static const MigrationRegistry kRefusing(2, {{1, [](SlotBody&) { return false; }}});
    SavePolicy oldPolicy;
    oldPolicy.migrations = &kOld;
    SavePolicy refusing;
    refusing.migrations = &kRefusing;

    const Dir dir;
    Campaign original;
    play(original);
    REQUIRE(saveCampaign(dir.path(), original, oldPolicy));
    const Bytes before = readBytes(dir.path() / "campaign.sav");

    SaveSlots slots(dir.path(), refusing);
    Scene scene(kSeed);
    const std::uint64_t pristine = scene.stateHash();
    CHECK(slots.load(kSlot, scene, kContext).status == LoadStatus::MigrationFailed);
    CHECK(readBytes(dir.path() / "campaign.sav") == before);
    CHECK(scene.stateHash() == pristine);
}

TEST_CASE("the consequence ledger rejects every truncation and keeps a one-to-one byte form", "[save_roundtrip]")
{
    Campaign c;
    play(c);
    const Bytes good = c.ledger.serialize();
    std::optional<ConsequenceLedger> last;
    mutateAll(
        good,
        [&](const Bytes& bytes) {
            last = ConsequenceLedger::parse(bytes);
            return last.has_value();
        },
        [&] { return last->serialize(); });
}

TEST_CASE("faction standing rejects every truncation and keeps a one-to-one byte form", "[save_roundtrip]")
{
    Campaign c;
    play(c);
    const Bytes good = c.standing.serialise();
    FactionStanding probe(kFactions, tiers(), {});
    mutateAll(
        good, [&](const Bytes& bytes) { return probe.deserialise(bytes); }, [&] { return probe.serialise(); });
}

TEST_CASE("the wallet rejects every truncation and keeps a one-to-one byte form", "[save_roundtrip]")
{
    Campaign c;
    play(c);
    const CurrencyTable table = currencies();
    const Bytes good = c.wallet.serialise();
    std::optional<Wallet> last;
    mutateAll(
        good,
        [&](const Bytes& bytes) {
            last = Wallet::deserialise(bytes, table);
            return last.has_value();
        },
        [&] { return last->serialise(); });
}

TEST_CASE("capability unlocks reject every truncation and keep a one-to-one byte form", "[save_roundtrip]")
{
    Campaign c;
    play(c);
    const Bytes good = c.capabilities.serialise();
    CapabilityUnlocks probe(capabilityTable());
    mutateAll(
        good, [&](const Bytes& bytes) { return probe.deserialise(bytes); }, [&] { return probe.serialise(); });
}

TEST_CASE("a rejected load leaves standing and capabilities exactly as they were", "[save_roundtrip]")
{
    Campaign c;
    play(c);
    const std::uint64_t standingBefore = c.standing.digest();
    const std::uint64_t capabilitiesBefore = c.capabilities.digest();

    Bytes standing = c.standing.serialise();
    standing.pop_back();
    CHECK_FALSE(c.standing.deserialise(standing));
    Bytes capabilities = c.capabilities.serialise();
    capabilities.pop_back();
    CHECK_FALSE(c.capabilities.deserialise(capabilities));

    CHECK(c.standing.digest() == standingBefore);
    CHECK(c.capabilities.digest() == capabilitiesBefore);
}

TEST_CASE("the chronicle round-trips through text and through a slot, and damaged text never loads half", "[save_roundtrip]")
{
    Campaign c;
    play(c);
    const std::string text = c.chronicle.serialize();

    Chronicle back;
    REQUIRE(Chronicle::parse(text, back));
    CHECK(back == c.chronicle);
    CHECK(back.serialize() == text);

    for (std::size_t length = 0; length < text.size(); ++length) {
        Chronicle probe;
        if (Chronicle::parse(std::string_view(text).substr(0, length), probe)) {
            // A cut on a line boundary is a shorter chronicle; it must still be self-consistent.
            Chronicle again;
            REQUIRE(Chronicle::parse(probe.serialize(), again));
            CHECK(again == probe);
        } else {
            CHECK(probe.empty());
        }
    }
}

TEST_CASE("settings round-trip byte-identically and hostile files give defaults", "[save_roundtrip]")
{
    const Dir dir;
    Settings settings;
    settings.fovDegrees = 88.0f;
    settings.mouseSensitivity = 3.5f;

    const std::string text = serializeSettings(settings);
    CHECK(parseSettings(text) == settings);
    CHECK(serializeSettings(parseSettings(text)) == text);

    const auto path = dir.path() / "settings.txt";
    REQUIRE(saveSettings(path, settings));
    const Bytes first = readBytes(path);
    REQUIRE(saveSettings(path, loadSettings(path)));
    CHECK(readBytes(path) == first);

    for (std::size_t length = 0; length < text.size(); ++length) {
        const Settings cut = parseSettings(std::string_view(text).substr(0, length));
        CHECK(cut == cut.clamped());
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        std::string flipped = text;
        flipped[i] = static_cast<char>(flipped[i] ^ 0x20);
        const Settings parsed = parseSettings(flipped);
        CHECK(parsed == parsed.clamped());
    }
    CHECK(parseSettings("\xff\xfe\x00garbage") == Settings{});
}
