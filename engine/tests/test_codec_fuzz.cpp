#include "test_temp.h"
#include "codec_fuzz.h"

#include "ichor/graph/dungeon_forge.h"
#include "ichor/ledger/ledger_codec_format.h"
#include "ichor/net/coop_message.h"
#include "ichor/save/campaign.h"
#include "ichor/save/codex_codec.h"
#include "ichor/save/death_markers.h"
#include "ichor/save/delta_store.h"
#include "ichor/save/forge_cache.h"
#include "ichor/save/snapshot_tail.h"
#include "ichor/ui/map_annotation_codec.h"
#include "ichor/ui/map_annotation_share.h"
#include "ichor/world/player_messages.h"

#include <worldforge/content_manifest.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace ichor;
using ichor::fuzz::Bytes;
using ichor::fuzz::Canonical;
using ichor::fuzz::Options;

namespace {

Bytes toBytes(std::span<const std::uint8_t> s) { return Bytes(s.begin(), s.end()); }

void expectClean(const ichor::fuzz::Report& report)
{
    INFO(ichor::fuzz::summary(report));
    CHECK(report.cases > 100);
    CHECK(report.rejected > 0);
    CHECK(report.ok());
}

// ---- samples ----

MapAnnotationStore sampleAnnotations()
{
    MapAnnotationStore store;
    REQUIRE(store.add({"camp-1", AnnotationKind::Mark, AnnotationSymbol::Camp, "Dry cave", {{10.5f, -3.25f}}}) == AnnotationResult::Ok);
    REQUIRE(store.add({"note.2", AnnotationKind::Note, AnnotationSymbol::Other, "Beware the bridge", {{-200.0f, 40.0f}}}) == AnnotationResult::Ok);
    REQUIRE(store.add({"road", AnnotationKind::Path, AnnotationSymbol::Direction, "", {{0.0f, 0.0f}, {5.0f, 5.0f}, {9.0f, 1.5f}}}) == AnnotationResult::Ok);
    return store;
}

DeathMarkerSet sampleDeaths()
{
    DeathMarkerSet set(0xC0FFEE);
    set.add({100, 0, 200, DeathCause::Fall, 3});
    set.add({5000, -20, 7000, DeathCause::Combat, 9});
    set.add({-90000, 10, 12000, DeathCause::Drowning, 12});
    set.add({150, 5, 260, DeathCause::Hazard, 4});
    return set;
}

MessageBoard sampleBoard()
{
    MessageBoard board;
    for (std::uint64_t author = 1; author <= 4; ++author) {
        MessageDraft d;
        d.author = author;
        d.xMm = static_cast<std::int32_t>(author) * 100000;
        d.zMm = -static_cast<std::int32_t>(author) * 50000;
        d.templateId = 0;
        d.wordId = static_cast<std::uint8_t>(author % 2);
        REQUIRE(board.place(d, static_cast<std::uint32_t>(author) * 1000).ok());
    }
    return board;
}

ConsequenceLedger sampleLedger()
{
    ConsequenceLedger ledger;
    const char* subjects[] = {"npc.smith", "npc.priest", "world"};
    for (int i = 0; i < 6; ++i) {
        Fact f;
        f.id = "fact-" + std::to_string(i);
        f.subject = subjects[i % 3];
        f.kind = i % 2 ? "killed" : "helped";
        f.value = "target-" + std::to_string(i / 2);
        f.tick = static_cast<std::uint64_t>(i) * 100 + (i % 3);
        REQUIRE(ledger.append(f) == AppendResult::Ok);
    }
    return ledger;
}

CodexStore sampleCodex()
{
    CodexStore store;
    store.unlock(CodexCategory::Axiom, "gravity.low");
    store.unlock(CodexCategory::Synergy, "unbinding.temenos");
    store.unlock(CodexCategory::Faction, "archaists");
    store.unlock(CodexCategory::World, "world-77");
    store.unlock(CodexCategory::Seed, "seed-alpha");
    return store;
}

StateDelta sampleDelta()
{
    StateDelta d;
    d.set["chest/1"] = Bytes{1, 2, 3};
    d.set["npc/7"] = Bytes{};
    d.set["world/clock"] = Bytes{9, 9, 9, 9, 9};
    d.removed = {"npc/2", "zone/old"};
    return d;
}

worldforge::ContentManifest sampleManifest()
{
    worldforge::BindingInputs in;
    in.worldSeed = 0x123456789abcdef0ull;
    for (int i = 0; i < 3; ++i) in.pois.push_back({"town-" + std::to_string(i), worldforge::PoiKind::Settlement, ""});
    for (int i = 0; i < 2; ++i) in.pois.push_back({"crypt-" + std::to_string(i), worldforge::PoiKind::Dungeon, i ? "crypt" : "mine"});
    in.pois.push_back({"ruin-0", worldforge::PoiKind::Ruin, ""});
    for (int i = 0; i < 5; ++i) in.itemIds.push_back("item-" + std::to_string(i));
    in.npcArchetypes = {"smith", "priest"};
    worldforge::BindingParams params;
    params.castPerArchetype = 2;
    params.tensionCount = 3;
    auto manifest = worldforge::bindContent(in, params);
    REQUIRE(manifest.has_value());
    return *manifest;
}

}

// ---- the helper itself: each fault below must be caught ----

TEST_CASE("the fuzz helper reports a decoder that trusts a hostile length", "[fuzz][codec_fuzz]")
{
    const Bytes valid = {0x02, 'a', 'b'};
    const auto report = ichor::fuzz::fuzzCodec(valid, [](std::span<const std::uint8_t> b) -> Canonical {
        if (b.empty()) return std::nullopt;
        std::vector<std::uint8_t> out;
        out.resize(std::size_t{b[0]} << 24);
        return std::nullopt;
    });
    CHECK_FALSE(report.ok());
}

TEST_CASE("the fuzz helper reports a decoder that accepts a different value without a matching re-encode", "[fuzz][codec_fuzz]")
{
    const Bytes valid = {1, 2, 3, 4};
    const auto report = ichor::fuzz::fuzzCodec(valid, [](std::span<const std::uint8_t> b) -> Canonical {
        if (b.size() != 4) return std::nullopt;
        return Bytes{b[0], b[1], b[2], 0};
    });
    CHECK_FALSE(report.ok());
}

TEST_CASE("the fuzz helper reports a decoder that throws", "[fuzz][codec_fuzz]")
{
    const Bytes valid = {1, 2, 3, 4};
    const auto report = ichor::fuzz::fuzzCodec(valid, [&valid](std::span<const std::uint8_t> b) -> Canonical {
        if (b.size() == 3) throw std::runtime_error("boom");
        if (b.size() == valid.size() && std::equal(b.begin(), b.end(), valid.begin())) return valid;
        return std::nullopt;
    });
    CHECK_FALSE(report.ok());
}

TEST_CASE("the fuzz helper reports a decoder that ignores trailing bytes unless normalising is allowed", "[fuzz][codec_fuzz]")
{
    const Bytes valid = {7, 8, 9};
    const ichor::fuzz::DecodeFn lenient = [&valid](std::span<const std::uint8_t> b) -> Canonical {
        if (b.size() < valid.size() || !std::equal(valid.begin(), valid.end(), b.begin())) return std::nullopt;
        return valid;
    };
    CHECK_FALSE(ichor::fuzz::fuzzCodec(valid, lenient).ok());
    Options normalising;
    normalising.allowNormalising = true;
    CHECK(ichor::fuzz::fuzzCodec(valid, lenient, normalising).ok());
}

TEST_CASE("the fuzz helper refuses a valid buffer that does not round trip", "[fuzz][codec_fuzz]")
{
    const auto report = ichor::fuzz::fuzzCodec({1, 2}, [](std::span<const std::uint8_t>) -> Canonical { return std::nullopt; });
    CHECK_FALSE(report.ok());
}

TEST_CASE("the fuzz helper passes a strict checksummed codec", "[fuzz][codec_fuzz]")
{
    Bytes valid = {5, 6, 7};
    valid.push_back(static_cast<std::uint8_t>(5 + 6 + 7));
    const auto report = ichor::fuzz::fuzzCodec(valid, [](std::span<const std::uint8_t> b) -> Canonical {
        if (b.size() != 4 || static_cast<std::uint8_t>(b[0] + b[1] + b[2]) != b[3]) return std::nullopt;
        return toBytes(b);
    });
    CHECK(report.ok());
}

// ---- every codec on main ----

TEST_CASE("fuzz: forge cache record", "[fuzz][forge_cache]")
{
    ForgeCacheRecord record;
    record.sequence = 5;
    record.key = {0xABCDEF, 3, 0x1122334455667788ull};
    for (int i = 0; i < 40; ++i) record.payload.push_back(static_cast<std::uint8_t>(i * 7));
    expectClean(ichor::fuzz::fuzzCodec(encodeForgeRecord(record), [](std::span<const std::uint8_t> b) -> Canonical {
        const auto r = decodeForgeRecord(b);
        if (!r.ok()) return std::nullopt;
        return encodeForgeRecord(r.record);
    }));
}

TEST_CASE("fuzz: death markers", "[fuzz][death_markers]")
{
    expectClean(ichor::fuzz::fuzzCodec(encodeDeathMarkers(sampleDeaths()), [](std::span<const std::uint8_t> b) -> Canonical {
        const auto r = decodeDeathMarkers(b);
        if (!r.ok()) return std::nullopt;
        return encodeDeathMarkers(r.set);
    }));
}

TEST_CASE("fuzz: player messages", "[fuzz][player_messages]")
{
    expectClean(ichor::fuzz::fuzzCodec(encodeMessages(sampleBoard()), [](std::span<const std::uint8_t> b) -> Canonical {
        const auto r = decodeMessages(b);
        if (!r.ok()) return std::nullopt;
        return encodeMessages(r.board);
    }));
}

TEST_CASE("fuzz: co-op client messages", "[fuzz][coop_message]")
{
    using namespace ichor::net;
    const std::vector<ClientMessage> samples = {
        HelloMsg{.worldSeed = 99, .genVersion = 2, .name = "Ada", .inventory = {{"iron_key", 2}, {"bread", 9}}},
        LeaveMsg{},
        InputMsg{.tick = 77, .moveX = 0.5f, .moveY = -0.5f, .facing = 3.0f},
        CommandMsg{.kind = CommandKind::UseItem, .tick = 12, .target = 0xDEADBEEFull, .arg = 3},
        PingMsg{.token = 123456},
    };
    for (const ClientMessage& message : samples) {
        const auto encoded = encodeClient(message);
        REQUIRE(encoded.has_value());
        INFO("client message index " << message.index());
        expectClean(ichor::fuzz::fuzzCodec(*encoded, [](std::span<const std::uint8_t> b) -> Canonical {
            const auto d = decodeClient(b);
            if (!d.message) return std::nullopt;
            return encodeClient(*d.message);
        }));
    }
}

TEST_CASE("fuzz: co-op host messages", "[fuzz][coop_message]")
{
    using namespace ichor::net;
    const std::vector<HostMessage> samples = {
        WelcomeMsg{.playerId = 2, .worldSeed = 5, .genVersion = 1, .revision = 40, .hostTick = 900, .roster = {{0, "Host"}, {2, "Ada"}}},
        CloseMsg{.reason = CloseReason::Desync},
        PlayerJoinedMsg{.player = {3, "Bo"}},
        PlayerLeftMsg{.id = 1},
        WorldDeltaMsg{.revision = 8, .entity = 0x1234567890ull, .kind = 4, .value = -17},
        PongMsg{.token = 5, .hostTick = 6000},
    };
    for (const HostMessage& message : samples) {
        const auto encoded = encodeHost(message);
        REQUIRE(encoded.has_value());
        INFO("host message index " << message.index());
        expectClean(ichor::fuzz::fuzzCodec(*encoded, [](std::span<const std::uint8_t> b) -> Canonical {
            const auto d = decodeHost(b);
            if (!d.message) return std::nullopt;
            return encodeHost(*d.message);
        }));
    }
}

TEST_CASE("fuzz: map annotations", "[fuzz][map_annotation]")
{
    Options options;
    options.allowNonCanonical = true; // a flipped sign bit on 0.0 is stored as +0.0, by design
    expectClean(ichor::fuzz::fuzzCodec(encodeAnnotations(sampleAnnotations()), [](std::span<const std::uint8_t> b) -> Canonical {
        const auto r = decodeAnnotations(b);
        if (!r.ok()) return std::nullopt;
        return encodeAnnotations(r.store);
    }, options));
}

TEST_CASE("fuzz: map annotation share envelope", "[fuzz][map_annotation]")
{
    const auto encoded = encodeAnnotationShare(sampleAnnotations(), "seed-abc", "Ada");
    REQUIRE(encoded.status == AnnotationShareStatus::Ok);
    expectClean(ichor::fuzz::fuzzCodec(encoded.bytes, [](std::span<const std::uint8_t> b) -> Canonical {
        const auto r = decodeAnnotationShare(b);
        if (r.status != AnnotationShareStatus::Ok) return std::nullopt;
        const auto again = encodeAnnotationShare(r.store, r.seed, r.author);
        if (again.status != AnnotationShareStatus::Ok) return Bytes{};
        return again.bytes;
    }));
}

TEST_CASE("fuzz: dungeon forge record", "[fuzz][dungeon_forge]")
{
    graph::forge::Record record;
    record.seed = 0x1122334455667788ull;
    record.rooms = 14;
    record.layoutHash = 0x99AABBCCDDEEFF00ull;
    expectClean(ichor::fuzz::fuzzCodec(graph::forge::encode(record), [](std::span<const std::uint8_t> b) -> Canonical {
        const auto r = graph::forge::decode(b);
        if (!r) return std::nullopt;
        return graph::forge::encode(*r);
    }));
}

TEST_CASE("fuzz: content manifest", "[fuzz][content_manifest]")
{
    expectClean(ichor::fuzz::fuzzCodec(worldforge::encodeManifest(sampleManifest()), [](std::span<const std::uint8_t> b) -> Canonical {
        const auto r = worldforge::decodeManifest(b);
        if (!r) return std::nullopt;
        return worldforge::encodeManifest(*r);
    }));
}

TEST_CASE("fuzz: snapshot tail log", "[fuzz][snapshot_tail]")
{
    DeltaState state = {{"world/seed", Bytes{1, 2, 3}}, {"npc/1", Bytes{9}}};
    auto log = SnapshotTailLog::fromSnapshot(state);
    REQUIRE(log.has_value());
    for (int n = 1; n <= 5; ++n) {
        state["chest/" + std::to_string(n % 3)] = Bytes{static_cast<std::uint8_t>(n), static_cast<std::uint8_t>(n * 7)};
        state["clock"] = Bytes{static_cast<std::uint8_t>(n)};
        REQUIRE(log->append(state));
    }
    const auto image = log->encode();
    REQUIRE(image.has_value());
    Options options;
    options.allowTornPrefix = true;
    expectClean(ichor::fuzz::fuzzCodec(*image, [](std::span<const std::uint8_t> b) -> Canonical {
        const auto loaded = SnapshotTailLog::load(toBytes(b));
        if (!loaded) return std::nullopt;
        return loaded->encode();
    }, options));
}

TEST_CASE("fuzz: state delta", "[fuzz][delta_store]")
{
    const auto encoded = encodeDelta(sampleDelta());
    REQUIRE(encoded.has_value());
    expectClean(ichor::fuzz::fuzzCodec(*encoded, [](std::span<const std::uint8_t> b) -> Canonical {
        const auto d = decodeDelta(toBytes(b));
        if (!d) return std::nullopt;
        return encodeDelta(*d);
    }));
}

TEST_CASE("fuzz: consequence ledger codec", "[fuzz][ledger]")
{
    Options options;
    options.allowNonCanonical = true; // known: decodeLedger accepts a string table with repeated or unused strings
    expectClean(ichor::fuzz::fuzzCodec(encodeLedger(sampleLedger()), [](std::span<const std::uint8_t> b) -> Canonical {
        const auto l = decodeLedger(b);
        if (!l) return std::nullopt;
        return encodeLedger(*l);
    }, options));
}

TEST_CASE("fuzz: consequence ledger serialize", "[fuzz][ledger]")
{
    expectClean(ichor::fuzz::fuzzCodec(sampleLedger().serialize(), [](std::span<const std::uint8_t> b) -> Canonical {
        const auto l = ConsequenceLedger::parse(b);
        if (!l) return std::nullopt;
        return l->serialize();
    }));
}

TEST_CASE("fuzz: codex", "[fuzz][codex]")
{
    expectClean(ichor::fuzz::fuzzCodec(encodeCodex(sampleCodex()), [](std::span<const std::uint8_t> b) -> Canonical {
        const auto r = decodeCodex(b);
        if (!r.ok()) return std::nullopt;
        return encodeCodex(r.store);
    }));
}

TEST_CASE("fuzz: campaign manifest through Campaign::open", "[fuzz][campaign]")
{
    namespace fs = std::filesystem;
    const fs::path root = ichor::test::outputPath(ICHOR_TEST_OUTPUT, "codec_fuzz_campaign");
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);

    const Bytes blobA = {1, 2, 3, 4};
    const Bytes blobB = {9, 8, 7};
    const std::vector<WorldBlob> blobs = {{"heightfield.bin", blobA}, {"rivers.bin", blobB}};
    REQUIRE(Campaign::create(root, "fuzzed", 0xFEEDFACEull, 4, blobs) == CampaignStatus::Ok);

    const fs::path manifestPath = root / "fuzzed" / "world" / Campaign::kManifestFile;
    Bytes valid;
    {
        std::ifstream in(manifestPath, std::ios::binary);
        valid.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    REQUIRE(valid.size() > 30);
    const auto original = Campaign::open(root, "fuzzed");
    REQUIRE(original.has_value());
    const WorldManifest expected = original->manifest();

    Options options;
    options.randomMutations = 400;
    const auto report = ichor::fuzz::fuzzCodec(valid, [&](std::span<const std::uint8_t> b) -> Canonical {
        {
            std::ofstream out(manifestPath, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
        }
        const auto opened = Campaign::open(root, "fuzzed");
        if (!opened) return std::nullopt;
        const WorldManifest& got = opened->manifest();
        const bool same = got.worldSeed == expected.worldSeed && got.genVersion == expected.genVersion && got.files.size() == expected.files.size()
            && std::equal(got.files.begin(), got.files.end(), expected.files.begin(), [](const WorldFileInfo& x, const WorldFileInfo& y) {
                   return x.name == y.name && x.size == y.size && x.hash == y.hash;
               });
        return same ? Canonical(valid) : Canonical(Bytes{'f', 'i', 'e', 'l', 'd', 's', ' ', 'd', 'i', 'f', 'f', 'e', 'r'});
    }, options);
    expectClean(report);
    fs::remove_all(root, ec);
}
