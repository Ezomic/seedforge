#include "ichor/stage0/fight_review.h"

#include "ichor/core/log.h"

#include <format>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

namespace ichor::stage0 {

namespace {

constexpr std::uintmax_t kMaxRecordFileBytes = 20 + kMaxFightEvents * 8;

const char* kindName(FightEventKind kind)
{
    switch (kind) {
    case FightEventKind::Attack: return "swings";
    case FightEventKind::Hit: return "lands a hit";
    case FightEventKind::Block: return "hits a raised guard";
    case FightEventKind::Whiff: return "whiffs";
    case FightEventKind::Dodge: return "dodges";
    case FightEventKind::Stagger: return "is staggered";
    }
    return "?";
}

const char* sideName(FightSide side)
{
    return side == FightSide::Player ? "player" : "foe";
}

}

std::filesystem::path fightRecordDirectory(const std::filesystem::path& csv)
{
    std::filesystem::path dir = csv;
    dir.replace_extension(".fights");
    return dir;
}

std::filesystem::path fightRecordPath(const std::filesystem::path& directory, std::uint32_t index)
{
    return directory / std::format("fight_{:03}.ifrd", index);
}

bool saveFightRecord(const std::filesystem::path& path, const FightRecord& record)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::path temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        const std::vector<std::uint8_t> bytes = encode(record);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out) {
            std::filesystem::remove(temp, ec);
            return false;
        }
    }
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

std::optional<FightRecord> loadFightRecord(const std::filesystem::path& path)
{
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec || size > kMaxRecordFileBytes) {
        return std::nullopt;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return decodeFightRecord(bytes);
}

std::string describeFight(const FightRecord& record)
{
    const FightRecordSummary s = summarize(record);
    std::string out = std::format("fight #{}: {} died ({}) after {:.1f}s{}\n", record.index, sideName(record.loser), name(record.cause),
        record.durationMs / 1000.0, record.truncated ? ", event list truncated" : "");
    out += std::format("player: {} hits ({} blocked by foe), {} whiffs, {:.1f} damage dealt\n", s.playerHits, s.foeBlocks, s.playerWhiffs,
        static_cast<double>(s.playerDamageTenths) / 10.0);
    out += std::format("foe: {} hits ({} blocked by player), {} whiffs, {:.1f} damage dealt\n", s.foeHits, s.playerBlocks, s.foeWhiffs,
        static_cast<double>(s.foeDamageTenths) / 10.0);
    for (const FightEvent& e : record.events) {
        out += std::format("{:8.3f}s  {} {}", e.atMs / 1000.0, sideName(e.actor), kindName(e.kind));
        if (e.kind == FightEventKind::Hit || e.kind == FightEventKind::Block) {
            out += std::format(" for {:.1f}", e.damageTenths / 10.0);
        }
        out += '\n';
    }
    return out;
}

int reviewFight(const std::filesystem::path& path)
{
    const std::optional<FightRecord> record = loadFightRecord(path);
    if (!record) {
        logf(LogLevel::Error, "not a readable fight record: {}", path.string());
        return 1;
    }
    log(LogLevel::Info, describeFight(*record));
    return 0;
}

}
