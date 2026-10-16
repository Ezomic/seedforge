#include "ichor/ledger/ledger_codec_format.h"

#include <map>
#include <string>
#include <string_view>

namespace ichor {

namespace {

// One fact takes at least a length byte, one id byte, three indices and a tick difference.
constexpr std::size_t kMinFactBytes = 6;

void putVarint(std::vector<std::uint8_t>& out, std::uint64_t v)
{
    while (v >= 0x80) {
        out.push_back(static_cast<std::uint8_t>(v | 0x80));
        v >>= 7;
    }
    out.push_back(static_cast<std::uint8_t>(v));
}

void putString(std::vector<std::uint8_t>& out, std::string_view s)
{
    putVarint(out, s.size());
    out.insert(out.end(), s.begin(), s.end());
}

std::uint64_t zigzag(std::uint64_t delta)
{
    const auto signedDelta = static_cast<std::int64_t>(delta);
    return (delta << 1) ^ static_cast<std::uint64_t>(signedDelta >> 63);
}

std::uint64_t unzigzag(std::uint64_t v)
{
    return (v >> 1) ^ (~(v & 1) + 1);
}

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    std::size_t remaining() const { return bytes_.size() - pos_; }
    bool atEnd() const { return pos_ == bytes_.size(); }

    bool byte(std::uint8_t& out)
    {
        if (atEnd()) return false;
        out = bytes_[pos_++];
        return true;
    }

    // Rejects more than ten bytes, bits past 64 and padded (non-minimal) forms.
    bool varint(std::uint64_t& out)
    {
        out = 0;
        for (int i = 0; i < 10; ++i) {
            std::uint8_t b = 0;
            if (!byte(b)) return false;
            if (i == 9 && b > 1) return false;
            out |= static_cast<std::uint64_t>(b & 0x7F) << (7 * i);
            if ((b & 0x80) == 0) return !(i > 0 && b == 0);
        }
        return false;
    }

    bool string(std::string& out, std::size_t maxBytes)
    {
        std::uint64_t len = 0;
        if (!varint(len) || len > maxBytes || len > remaining()) return false;
        out.assign(reinterpret_cast<const char*>(bytes_.data() + pos_), static_cast<std::size_t>(len));
        pos_ += static_cast<std::size_t>(len);
        return true;
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t pos_ = 0;
};

}

std::vector<std::uint8_t> encodeLedger(const ConsequenceLedger& ledger)
{
    std::map<std::string_view, std::uint64_t> index;
    std::vector<std::string_view> table;
    const auto intern = [&](std::string_view s) {
        const auto [it, added] = index.emplace(s, table.size());
        if (added) table.push_back(s);
        return it->second;
    };

    struct Row {
        std::uint64_t subject, kind, value;
    };
    std::vector<Row> rows;
    rows.reserve(ledger.size());
    for (const Fact& f : ledger.facts()) rows.push_back({intern(f.subject), intern(f.kind), intern(f.value)});

    std::vector<std::uint8_t> out;
    out.push_back(kLedgerCodecVersion);
    putVarint(out, table.size());
    for (std::string_view s : table) putString(out, s);
    putVarint(out, ledger.size());
    std::uint64_t prevTick = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const Fact& f = ledger.facts()[i];
        putString(out, f.id);
        putVarint(out, rows[i].subject);
        putVarint(out, rows[i].kind);
        putVarint(out, rows[i].value);
        putVarint(out, zigzag(f.tick - prevTick));
        prevTick = f.tick;
    }
    return out;
}

std::optional<ConsequenceLedger> decodeLedger(std::span<const std::uint8_t> bytes)
{
    Reader reader(bytes);
    std::uint8_t version = 0;
    if (!reader.byte(version) || version != kLedgerCodecVersion) return std::nullopt;

    std::uint64_t stringCount = 0;
    if (!reader.varint(stringCount) || stringCount > reader.remaining()) return std::nullopt;
    std::vector<std::string> table;
    table.reserve(static_cast<std::size_t>(stringCount));
    for (std::uint64_t i = 0; i < stringCount; ++i) {
        std::string s;
        if (!reader.string(s, ConsequenceLedger::kMaxValueBytes)) return std::nullopt;
        table.push_back(std::move(s));
    }

    std::uint64_t count = 0;
    if (!reader.varint(count) || count > ConsequenceLedger::kMaxFacts || count > reader.remaining() / kMinFactBytes) {
        return std::nullopt;
    }
    ConsequenceLedger ledger;
    std::uint64_t tick = 0;
    for (std::uint64_t i = 0; i < count; ++i) {
        Fact f;
        std::uint64_t subject = 0;
        std::uint64_t kind = 0;
        std::uint64_t value = 0;
        std::uint64_t delta = 0;
        if (!reader.string(f.id, ConsequenceLedger::kMaxKeyBytes) || !reader.varint(subject) || !reader.varint(kind) ||
            !reader.varint(value) || !reader.varint(delta)) {
            return std::nullopt;
        }
        if (subject >= table.size() || kind >= table.size() || value >= table.size()) return std::nullopt;
        tick += unzigzag(delta);
        f.subject = table[static_cast<std::size_t>(subject)];
        f.kind = table[static_cast<std::size_t>(kind)];
        f.value = table[static_cast<std::size_t>(value)];
        f.tick = tick;
        if (ledger.append(std::move(f)) != AppendResult::Ok) return std::nullopt;
    }
    if (!reader.atEnd()) return std::nullopt;
    return ledger;
}

}
