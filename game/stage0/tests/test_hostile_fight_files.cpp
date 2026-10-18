#include "ichor/stage0/fight_record.h"
#include "ichor/stage0/fight_report.h"
#include "ichor/stage0/fight_review.h"
#include "ichor/stage0/session_ledger.h"
#include "ichor/stage0/visual_telegraph.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ichor::stage0;

// SEED-463: hostile bytes for the stage0 decoders. Mutations are deterministic (fixed seed). A decoder
// must not throw; a crash, overflow or runaway allocation is caught by the ASan and UBSan CI job.

namespace {

using Bytes = std::vector<std::uint8_t>;

struct Outcome {
    std::size_t cases = 0;
    std::size_t accepted = 0;
    std::size_t rejected = 0;
    std::size_t threw = 0;
    std::size_t notCanonical = 0;
};

// `decode` returns the canonical re-encoding, or nullopt when it refused the bytes.
using Decode = std::function<std::optional<Bytes>(const Bytes&)>;

Outcome mutate(const Bytes& valid, const Decode& decode, bool strict)
{
    Outcome out;
    const auto run = [&](const Bytes& input) {
        if (input == valid) return;
        ++out.cases;
        try {
            const std::optional<Bytes> result = decode(input);
            if (!result) {
                ++out.rejected;
                return;
            }
            ++out.accepted;
            if (strict && *result != input) ++out.notCanonical;
        } catch (const std::exception&) {
            ++out.threw;
        }
    };

    for (std::size_t n = 0; n < valid.size(); ++n) run(Bytes(valid.begin(), valid.begin() + static_cast<std::ptrdiff_t>(n)));
    for (std::size_t at = 0; at < valid.size(); ++at) {
        for (int bit = 0; bit < 8; ++bit) {
            Bytes m = valid;
            m[at] ^= static_cast<std::uint8_t>(1u << bit);
            run(m);
        }
        for (const std::uint8_t value : {std::uint8_t{0x00}, std::uint8_t{0xFF}, std::uint8_t{0x7F}}) {
            Bytes m = valid;
            m[at] = value;
            run(m);
        }
        Bytes ins = valid;
        ins.insert(ins.begin() + static_cast<std::ptrdiff_t>(at), 0xFF);
        run(ins);
        Bytes del = valid;
        del.erase(del.begin() + static_cast<std::ptrdiff_t>(at));
        run(del);
    }
    for (std::size_t at = 0; at + 4 <= valid.size(); ++at) {
        Bytes m = valid;
        std::fill(m.begin() + static_cast<std::ptrdiff_t>(at), m.begin() + static_cast<std::ptrdiff_t>(at + 4), std::uint8_t{0xFF});
        run(m);
    }
    Bytes extended = valid;
    extended.insert(extended.end(), valid.begin(), valid.end());
    run(extended);

    std::mt19937_64 rng(0x5eedf00dull);
    for (int i = 0; i < 3000 && !valid.empty(); ++i) {
        Bytes m = valid;
        for (std::uint64_t e = 0, edits = 1 + rng() % 4; e < edits && !m.empty(); ++e) {
            const std::size_t at = rng() % m.size();
            if (rng() % 2 == 0) {
                m[at] = static_cast<std::uint8_t>(rng());
            } else {
                m.insert(m.begin() + static_cast<std::ptrdiff_t>(at), static_cast<std::uint8_t>(rng()));
            }
        }
        run(m);
    }
    return out;
}

FightRecord sample()
{
    FightRecord r;
    r.index = 3;
    r.loser = FightSide::Player;
    r.cause = DeathCause::WhiffPunished;
    r.durationMs = 9000;
    r.events = {
        {0, FightEventKind::Attack, FightSide::Player, 0},
        {400, FightEventKind::Hit, FightSide::Player, 250},
        {1200, FightEventKind::Whiff, FightSide::Foe, 0},
        {2000, FightEventKind::Block, FightSide::Foe, 120},
        {9000, FightEventKind::Hit, FightSide::Foe, 1000},
    };
    return r;
}

Bytes toBytes(const std::string& s) { return Bytes(s.begin(), s.end()); }
std::string toText(const Bytes& b) { return std::string(b.begin(), b.end()); }

std::filesystem::path uniqueDir(const std::string& tag)
{
    std::random_device rd;
    const std::uint64_t id = (static_cast<std::uint64_t>(rd()) << 32) ^ rd() ^ static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("ichor_hostile_" + tag + "_" + std::to_string(id));
    std::filesystem::create_directories(dir);
    return dir;
}

}

TEST_CASE("hostile: fight record bytes are refused or decode to the same record", "[hostile][fight_record]")
{
    const Bytes valid = encode(sample());
    const Outcome out = mutate(valid, [](const Bytes& b) -> std::optional<Bytes> {
        const auto r = decodeFightRecord(b);
        if (!r) return std::nullopt;
        return encode(*r);
    }, true);
    CHECK(out.cases > 200);
    CHECK(out.rejected > 0);
    CHECK(out.threw == 0);
    CHECK(out.notCanonical == 0);
}

TEST_CASE("hostile: a fight record never holds more events than the cap", "[hostile][fight_record]")
{
    const Bytes valid = encode(sample());
    REQUIRE(valid.size() > 12);
    for (std::size_t at = 0; at + 4 <= valid.size(); ++at) {
        Bytes m = valid;
        std::fill(m.begin() + static_cast<std::ptrdiff_t>(at), m.begin() + static_cast<std::ptrdiff_t>(at + 4), std::uint8_t{0xFF});
        const auto r = decodeFightRecord(m);
        if (r) CHECK(r->events.size() <= kMaxFightEvents);
    }
}

TEST_CASE("hostile: fight record file through loadFightRecord", "[hostile][fight_record]")
{
    const std::filesystem::path dir = uniqueDir("fight_file");
    const std::filesystem::path file = dir / "fight_003.ifrd";
    REQUIRE(saveFightRecord(file, sample()));
    std::ifstream in(file, std::ios::binary);
    const Bytes valid((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    const Outcome out = mutate(valid, [&](const Bytes& b) -> std::optional<Bytes> {
        {
            std::ofstream o(file, std::ios::binary | std::ios::trunc);
            o.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
        }
        const auto r = loadFightRecord(file);
        if (!r) return std::nullopt;
        return encode(*r);
    }, true);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    CHECK(out.cases > 200);
    CHECK(out.rejected > 0);
    CHECK(out.threw == 0);
    CHECK(out.notCanonical == 0);
}

TEST_CASE("hostile: fight log csv never throws and keeps only finite rows", "[hostile][fight_report]")
{
    const std::string header = "fight,seconds,gap_to_retry,loser,cause,p_hp,p_attacks,p_hits,p_whiffs,p_blocked,p_dodges,p_staggers,p_dealt,p_taken,"
                               "f_hp,f_attacks,f_hits,f_whiffs,f_blocked,f_dodges,f_staggers,f_dealt,f_taken\n";
    const std::string rows = "1,12.00,2.50,player,stamina,0,5,2,3,1,4,0,60,100,40,6,3,2,0,0,0,100,60\n"
                             "2,8.00,1.50,foe,unreadable,10,5,2,3,1,4,0,60,100,0,6,3,2,0,0,0,100,60\n";
    const Bytes valid = toBytes(header + rows);
    REQUIRE(parseFightLog(toText(valid)).size() == 2);
    const Outcome out = mutate(valid, [](const Bytes& b) -> std::optional<Bytes> {
        const auto parsed = parseFightLog(toText(b));
        for (const FightRow& row : parsed) {
            if (!std::isfinite(row.seconds) || !std::isfinite(row.gapToRetry) || !std::isfinite(row.playerDamageTaken)) throw std::runtime_error("non-finite row");
        }
        return parsed.size() == 2 ? std::optional<Bytes>(b) : std::nullopt;
    }, false);
    CHECK(out.cases > 200);
    CHECK(out.rejected > 0);
    CHECK(out.threw == 0);
}

TEST_CASE("hostile: session ledger text never throws and counts consistently", "[hostile][session_ledger]")
{
    std::string text = ledgerHeader();
    for (int i = 1; i <= 25; ++i) text += ledgerRow(i, {"s1", "poise"});
    const Bytes valid = toBytes(text);
    REQUIRE(readProgress(text).recorded == 25);
    const Outcome out = mutate(valid, [](const Bytes& b) -> std::optional<Bytes> {
        const GateProgress p = readProgress(toText(b));
        if (p.recorded < 0 || p.recorded > 1000 || p.bugFix + p.tuned != p.recorded) throw std::runtime_error("inconsistent progress");
        return p.recorded == 25 ? std::optional<Bytes>(b) : std::nullopt;
    }, false);
    CHECK(out.cases > 200);
    CHECK(out.rejected > 0);
    CHECK(out.threw == 0);
}

TEST_CASE("hostile: telegraph role text never throws", "[hostile][visual_telegraph]")
{
    TelegraphRole role{};
    const Bytes valid = toBytes(name(TelegraphRole::Swarm));
    REQUIRE(parseTelegraphRole(toText(valid), role));
    const Outcome out = mutate(valid, [&](const Bytes& b) -> std::optional<Bytes> {
        if (!parseTelegraphRole(toText(b), role)) return std::nullopt;
        return b;
    }, false);
    CHECK(out.threw == 0);
    CHECK(out.rejected > 0);
}
