#include "ichor/save/delta_store.h"

#include <algorithm>

namespace ichor {

namespace {

namespace lim = delta_limits;

bool keyValid(const std::string& key)
{
    return !key.empty() && key.size() <= lim::kMaxKeyBytes;
}

void putVarint(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    while (v >= 0x80) {
        out.push_back(static_cast<std::uint8_t>(v | 0x80));
        v >>= 7;
    }
    out.push_back(static_cast<std::uint8_t>(v));
}

class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& bytes)
        : bytes_(bytes)
    {
    }

    bool atEnd() const { return at_ == bytes_.size(); }

    bool byte(std::uint8_t& out)
    {
        if (at_ >= bytes_.size()) {
            return false;
        }
        out = bytes_[at_++];
        return true;
    }

    // Canonical LEB128, at most four bytes, so no overlong forms and no overflow.
    bool varint(std::uint32_t& out)
    {
        out = 0;
        for (int i = 0; i < 4; ++i) {
            std::uint8_t b = 0;
            if (!byte(b)) {
                return false;
            }
            out |= static_cast<std::uint32_t>(b & 0x7F) << (7 * i);
            if ((b & 0x80) == 0) {
                return !(i > 0 && b == 0);
            }
        }
        return false;
    }

    bool take(std::size_t n, const std::uint8_t*& begin)
    {
        if (n > bytes_.size() - at_) {
            return false;
        }
        begin = bytes_.data() + at_;
        at_ += n;
        return true;
    }

private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t at_ = 0;
};

bool readKey(Reader& r, std::string& key)
{
    std::uint32_t len = 0;
    const std::uint8_t* p = nullptr;
    if (!r.varint(len) || len == 0 || len > lim::kMaxKeyBytes || !r.take(len, p)) {
        return false;
    }
    key.assign(reinterpret_cast<const char*>(p), len);
    return true;
}

}

StateDelta computeDelta(const DeltaState& baseline, const DeltaState& current)
{
    StateDelta delta;
    for (const auto& [key, value] : current) {
        const auto it = baseline.find(key);
        if (it == baseline.end() || it->second != value) {
            delta.set.emplace(key, value);
        }
    }
    for (const auto& entry : baseline) {
        if (current.find(entry.first) == current.end()) {
            delta.removed.push_back(entry.first);
        }
    }
    return delta;
}

std::optional<DeltaState> applyDelta(const DeltaState& baseline, const StateDelta& delta)
{
    DeltaState state = baseline;
    for (const auto& key : delta.removed) {
        if (state.erase(key) == 0) {
            return std::nullopt;
        }
    }
    for (const auto& [key, value] : delta.set) {
        if (!keyValid(key) || value.size() > lim::kMaxValueBytes) {
            return std::nullopt;
        }
        state[key] = value;
    }
    return state;
}

std::optional<std::vector<std::uint8_t>> encodeDelta(const StateDelta& delta)
{
    if (delta.set.size() > lim::kMaxEntries || delta.removed.size() > lim::kMaxEntries) {
        return std::nullopt;
    }
    std::vector<std::string> removed = delta.removed;
    std::sort(removed.begin(), removed.end());
    for (std::size_t i = 0; i < removed.size(); ++i) {
        if (!keyValid(removed[i]) || (i > 0 && removed[i] == removed[i - 1]) || delta.set.count(removed[i]) != 0) {
            return std::nullopt;
        }
    }

    std::vector<std::uint8_t> out;
    out.push_back(kDeltaFormatVersion);
    putVarint(out, static_cast<std::uint32_t>(delta.set.size()));
    for (const auto& [key, value] : delta.set) {
        if (!keyValid(key) || value.size() > lim::kMaxValueBytes) {
            return std::nullopt;
        }
        putVarint(out, static_cast<std::uint32_t>(key.size()));
        out.insert(out.end(), key.begin(), key.end());
        putVarint(out, static_cast<std::uint32_t>(value.size()));
        out.insert(out.end(), value.begin(), value.end());
        if (out.size() > lim::kMaxEncodedBytes) {
            return std::nullopt;
        }
    }
    putVarint(out, static_cast<std::uint32_t>(removed.size()));
    for (const auto& key : removed) {
        putVarint(out, static_cast<std::uint32_t>(key.size()));
        out.insert(out.end(), key.begin(), key.end());
    }
    if (out.size() > lim::kMaxEncodedBytes) {
        return std::nullopt;
    }
    return out;
}

std::optional<StateDelta> decodeDelta(const std::vector<std::uint8_t>& bytes)
{
    if (bytes.size() > lim::kMaxEncodedBytes) {
        return std::nullopt;
    }
    Reader r(bytes);
    std::uint8_t version = 0;
    if (!r.byte(version) || version != kDeltaFormatVersion) {
        return std::nullopt;
    }

    StateDelta delta;
    std::uint32_t count = 0;
    if (!r.varint(count) || count > lim::kMaxEntries) {
        return std::nullopt;
    }
    std::string previous;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::string key;
        std::uint32_t len = 0;
        const std::uint8_t* p = nullptr;
        if (!readKey(r, key) || (i > 0 && key <= previous) || !r.varint(len) || len > lim::kMaxValueBytes || !r.take(len, p)) {
            return std::nullopt;
        }
        previous = key;
        delta.set.emplace_hint(delta.set.end(), std::move(key), std::vector<std::uint8_t>(p, p + len));
    }

    if (!r.varint(count) || count > lim::kMaxEntries) {
        return std::nullopt;
    }
    previous.clear();
    for (std::uint32_t i = 0; i < count; ++i) {
        std::string key;
        if (!readKey(r, key) || (i > 0 && key <= previous) || delta.set.count(key) != 0) {
            return std::nullopt;
        }
        previous = key;
        delta.removed.push_back(std::move(key));
    }
    if (!r.atEnd()) {
        return std::nullopt;
    }
    return delta;
}

}
