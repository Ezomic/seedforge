#include "ichor/world/player_messages.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <limits>
#include <utility>

namespace ichor {

namespace {

struct Template {
    const char* text;
    bool takesWord;
};

constexpr std::array<Template, 12> kTemplates{{
    {"Try {}", true},
    {"Beware of {}", true},
    {"Ahead, {}", true},
    {"Behind, {}", true},
    {"Look for {}", true},
    {"Seek {}", true},
    {"Wait for {}", true},
    {"Safe camp nearby", false},
    {"Danger ahead", false},
    {"Dead end", false},
    {"Secret here", false},
    {"Rest here", false},
}};

constexpr std::array<const char*, 16> kWords{{
    "a hidden path", "a loot cache", "an ambush", "a merchant", "fresh water", "a steep drop", "a trap", "a shortcut",
    "shelter", "a patrol", "a beast", "a ford", "a ruin", "a shrine", "a thief", "a wolf",
}};

constexpr std::uint8_t kMagic[4] = {'I', 'C', 'P', 'M'};
constexpr std::uint8_t kVersion = 1;
constexpr std::size_t kHeaderBytes = 13;
constexpr std::size_t kRecordBytes = 36;
constexpr std::uint32_t kMaxTotalCap = 1u << 20;
constexpr std::uint32_t kMaxWindowCap = 1024;

bool validPhrase(std::uint8_t templateId, std::uint8_t wordId)
{
    if (templateId >= kTemplates.size()) {
        return false;
    }
    if (!kTemplates[templateId].takesWord) {
        return wordId == kNoMessageWord;
    }
    return wordId < kWords.size();
}

bool inRange(std::int32_t x, std::int32_t y, std::int32_t z, const MessageLimits& l)
{
    const std::int64_t h = l.maxHorizontalMm;
    const std::int64_t v = l.maxVerticalMm;
    return x >= -h && x <= h && z >= -h && z <= h && y >= -v && y <= v;
}

std::uint64_t distSq(std::int32_t ax, std::int32_t ay, std::int32_t az, std::int32_t bx, std::int32_t by,
                     std::int32_t bz)
{
    // Each axis difference is below 2^32, so its square fits in 64 bits; the sum saturates.
    const auto sq = [](std::int32_t a, std::int32_t b) {
        const std::int64_t diff = static_cast<std::int64_t>(a) - b;
        const auto mag = static_cast<std::uint64_t>(diff < 0 ? -diff : diff);
        return mag * mag;
    };
    const std::uint64_t parts[3] = {sq(ax, bx), sq(ay, by), sq(az, bz)};
    std::uint64_t total = 0;
    for (const std::uint64_t part : parts) {
        total = part > std::numeric_limits<std::uint64_t>::max() - total ? std::numeric_limits<std::uint64_t>::max()
                                                                          : total + part;
    }
    return total;
}

void putN(std::vector<std::uint8_t>& out, std::uint64_t v, int bytes)
{
    for (int i = 0; i < bytes; ++i) {
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
}

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes)
        : bytes_(bytes)
    {
    }
    std::size_t remaining() const { return bytes_.size() - pos_; }
    std::uint64_t get(int bytes)
    {
        std::uint64_t v = 0;
        for (int i = 0; i < bytes && pos_ < bytes_.size(); ++i) {
            v |= static_cast<std::uint64_t>(bytes_[pos_++]) << (8 * i);
        }
        return v;
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t pos_ = 0;
};

MessageDecodeResult fail(MessageDecodeStatus status, const MessageLimits& limits)
{
    return {status, MessageBoard(limits)};
}

} // namespace

std::size_t messageTemplateCount() { return kTemplates.size(); }
std::size_t messageWordCount() { return kWords.size(); }

bool messageTemplateTakesWord(std::uint8_t templateId)
{
    return templateId < kTemplates.size() && kTemplates[templateId].takesWord;
}

std::string renderMessageText(std::uint8_t templateId, std::uint8_t wordId)
{
    if (!validPhrase(templateId, wordId)) {
        return {};
    }
    std::string text = kTemplates[templateId].text;
    if (kTemplates[templateId].takesWord) {
        text.replace(text.find("{}"), 2, kWords[wordId]);
    }
    return text;
}

MessageBoard::MessageBoard(MessageLimits limits)
    : limits_(limits)
{
    limits_.lifetimeSeconds = std::max<std::uint32_t>(limits_.lifetimeSeconds, 1);
    limits_.maxTotal = std::min(limits_.maxTotal, kMaxTotalCap);
    limits_.maxPerWindow = std::min(limits_.maxPerWindow, kMaxWindowCap);
    limits_.maxHorizontalMm = std::max<std::int32_t>(limits_.maxHorizontalMm, 0);
    limits_.maxVerticalMm = std::max<std::int32_t>(limits_.maxVerticalMm, 0);
}

MessageBoard::AuthorHistory* MessageBoard::history(std::uint64_t author, bool create)
{
    auto it = std::lower_bound(histories_.begin(), histories_.end(), author,
                               [](const AuthorHistory& h, std::uint64_t a) { return h.author < a; });
    if (it != histories_.end() && it->author == author) {
        return &*it;
    }
    if (!create) {
        return nullptr;
    }
    it = histories_.insert(it, AuthorHistory{author, {}});
    return &*it;
}

PlaceResult MessageBoard::place(const MessageDraft& d, std::uint32_t now)
{
    const auto reject = [](PlaceStatus s) { return PlaceResult{s, 0}; };
    if (d.author == 0) {
        return reject(PlaceStatus::BadAuthor);
    }
    if (!validPhrase(d.templateId, d.wordId)) {
        return reject(PlaceStatus::BadPhrase);
    }
    if (!inRange(d.xMm, d.yMm, d.zMm, limits_)) {
        return reject(PlaceStatus::OutOfRange);
    }
    if (limits_.maxPerWindow == 0) {
        return reject(PlaceStatus::RateLimited);
    }

    const std::uint32_t keepFor = std::max(limits_.windowSeconds, limits_.minIntervalSeconds);
    std::erase_if(histories_, [&](const AuthorHistory& h) { return now >= h.placedAt.back() && now - h.placedAt.back() >= keepFor; });

    const AuthorHistory* hist = history(d.author, false);
    if (hist != nullptr && !hist->placedAt.empty()) {
        const std::uint32_t last = hist->placedAt.back();
        if (now < last || now - last < limits_.minIntervalSeconds) {
            return reject(PlaceStatus::TooSoon);
        }
        const auto inWindow = std::count_if(hist->placedAt.begin(), hist->placedAt.end(),
                                            [&](std::uint32_t t) { return now - t < limits_.windowSeconds; });
        if (static_cast<std::uint64_t>(inWindow) >= limits_.maxPerWindow) {
            return reject(PlaceStatus::RateLimited);
        }
    }

    expire(now);
    const auto mine = std::count_if(messages_.begin(), messages_.end(),
                                    [&](const PlayerMessage& m) { return m.author == d.author; });
    if (static_cast<std::uint64_t>(mine) >= limits_.maxPerAuthor) {
        return reject(PlaceStatus::AuthorFull);
    }
    const std::uint64_t expiresAt = static_cast<std::uint64_t>(now) + limits_.lifetimeSeconds;
    if (messages_.size() >= limits_.maxTotal || nextId_ == std::numeric_limits<std::uint32_t>::max() ||
        expiresAt > std::numeric_limits<std::uint32_t>::max()) {
        return reject(PlaceStatus::BoardFull);
    }
    const std::uint64_t radiusSq = static_cast<std::uint64_t>(limits_.crowdRadiusMm) * limits_.crowdRadiusMm;
    const auto crowd = std::count_if(messages_.begin(), messages_.end(), [&](const PlayerMessage& m) {
        return distSq(m.xMm, m.yMm, m.zMm, d.xMm, d.yMm, d.zMm) <= radiusSq;
    });
    if (static_cast<std::uint64_t>(crowd) >= limits_.maxInCrowd) {
        return reject(PlaceStatus::Crowded);
    }

    PlayerMessage m;
    m.id = nextId_++;
    m.author = d.author;
    m.xMm = d.xMm;
    m.yMm = d.yMm;
    m.zMm = d.zMm;
    m.yaw = d.yaw;
    m.templateId = d.templateId;
    m.wordId = d.wordId;
    m.createdAt = now;
    m.expiresAt = static_cast<std::uint32_t>(expiresAt);
    messages_.push_back(m);

    AuthorHistory* record = history(d.author, true);
    record->placedAt.push_back(now);
    if (record->placedAt.size() > limits_.maxPerWindow) {
        record->placedAt.erase(record->placedAt.begin());
    }
    return {PlaceStatus::Ok, m.id};
}

std::size_t MessageBoard::expire(std::uint32_t now)
{
    return std::erase_if(messages_, [now](const PlayerMessage& m) { return m.expiresAt <= now; });
}

bool MessageBoard::remove(std::uint32_t id)
{
    return std::erase_if(messages_, [id](const PlayerMessage& m) { return m.id == id; }) != 0;
}

std::vector<PlayerMessage> MessageBoard::near(std::int32_t x, std::int32_t y, std::int32_t z, std::uint32_t radiusMm,
                                              std::uint32_t now) const
{
    const std::uint64_t radiusSq = static_cast<std::uint64_t>(radiusMm) * radiusMm;
    std::vector<PlayerMessage> out;
    for (const auto& m : messages_) {
        if (m.expiresAt > now && distSq(m.xMm, m.yMm, m.zMm, x, y, z) <= radiusSq) {
            out.push_back(m);
        }
    }
    return out;
}

bool MessageBoard::restore(std::vector<PlayerMessage> messages, std::uint32_t nextId)
{
    if (messages.size() > limits_.maxTotal) {
        return false;
    }
    if (nextId == 0) {
        return false;
    }
    std::vector<std::uint64_t> authors;
    authors.reserve(messages.size());
    std::uint32_t prevId = 0;
    for (const auto& m : messages) {
        if (m.id == 0 || m.id <= prevId || m.id >= nextId || m.author == 0) {
            return false;
        }
        prevId = m.id;
        if (!validPhrase(m.templateId, m.wordId) || !inRange(m.xMm, m.yMm, m.zMm, limits_)) {
            return false;
        }
        if (m.expiresAt <= m.createdAt || m.expiresAt - m.createdAt > limits_.lifetimeSeconds) {
            return false;
        }
        authors.push_back(m.author);
    }
    std::sort(authors.begin(), authors.end());
    for (std::size_t i = 0, run = 0; i < authors.size(); ++i) {
        run = i > 0 && authors[i] == authors[i - 1] ? run + 1 : 1;
        if (run > limits_.maxPerAuthor) {
            return false;
        }
    }
    messages_ = std::move(messages);
    histories_.clear();
    nextId_ = nextId;
    return true;
}

std::vector<std::uint8_t> encodeMessages(const MessageBoard& board)
{
    std::vector<std::uint8_t> out(std::begin(kMagic), std::end(kMagic));
    out.push_back(kVersion);
    putN(out, board.nextId(), 4);
    putN(out, board.size(), 4);
    for (const auto& m : board.all()) {
        putN(out, m.id, 4);
        putN(out, m.author, 8);
        putN(out, static_cast<std::uint32_t>(m.xMm), 4);
        putN(out, static_cast<std::uint32_t>(m.yMm), 4);
        putN(out, static_cast<std::uint32_t>(m.zMm), 4);
        putN(out, m.yaw, 2);
        putN(out, m.templateId, 1);
        putN(out, m.wordId, 1);
        putN(out, m.createdAt, 4);
        putN(out, m.expiresAt, 4);
    }
    return out;
}

MessageDecodeResult decodeMessages(std::span<const std::uint8_t> bytes, const MessageLimits& limits)
{
    if (bytes.size() < kHeaderBytes) {
        return fail(MessageDecodeStatus::Truncated, limits);
    }
    if (std::memcmp(bytes.data(), kMagic, sizeof kMagic) != 0) {
        return fail(MessageDecodeStatus::BadMagic, limits);
    }
    if (bytes[4] != kVersion) {
        return fail(MessageDecodeStatus::BadVersion, limits);
    }
    MessageBoard board(limits);
    Reader r(bytes.subspan(5));
    const auto nextId = static_cast<std::uint32_t>(r.get(4));
    const std::uint64_t count = r.get(4);
    if (count > board.limits().maxTotal) {
        return fail(MessageDecodeStatus::TooMany, limits);
    }
    if (count * kRecordBytes > r.remaining()) {
        return fail(MessageDecodeStatus::Truncated, limits);
    }
    std::vector<PlayerMessage> messages(static_cast<std::size_t>(count));
    for (auto& m : messages) {
        m.id = static_cast<std::uint32_t>(r.get(4));
        m.author = r.get(8);
        m.xMm = static_cast<std::int32_t>(static_cast<std::uint32_t>(r.get(4)));
        m.yMm = static_cast<std::int32_t>(static_cast<std::uint32_t>(r.get(4)));
        m.zMm = static_cast<std::int32_t>(static_cast<std::uint32_t>(r.get(4)));
        m.yaw = static_cast<std::uint16_t>(r.get(2));
        m.templateId = static_cast<std::uint8_t>(r.get(1));
        m.wordId = static_cast<std::uint8_t>(r.get(1));
        m.createdAt = static_cast<std::uint32_t>(r.get(4));
        m.expiresAt = static_cast<std::uint32_t>(r.get(4));
    }
    if (r.remaining() != 0) {
        return fail(MessageDecodeStatus::TrailingBytes, limits);
    }
    if (!board.restore(std::move(messages), nextId)) {
        return fail(MessageDecodeStatus::InvalidRecord, limits);
    }
    return {MessageDecodeStatus::Ok, std::move(board)};
}

} // namespace ichor
