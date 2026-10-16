#include "ichor/save/snapshot_tail.h"

#include <algorithm>
#include <array>
#include <utility>

namespace ichor {

namespace {

constexpr std::array<std::uint8_t, 4> kMagic{'I', 'S', 'P', 'T'};
constexpr std::size_t kHeaderBytes = kMagic.size() + 1;
constexpr std::size_t kFrameHeadBytes = 8;

void putU32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
}

std::uint32_t getU32(const std::uint8_t* p)
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint32_t frameCrc(std::uint32_t position, const std::uint8_t* payload, std::size_t size)
{
    std::vector<std::uint8_t> pos;
    putU32(pos, position);
    return crc32(payload, size, crc32(pos.data(), pos.size()));
}

std::vector<std::uint8_t> makeFrame(std::uint32_t position, const std::vector<std::uint8_t>& payload)
{
    std::vector<std::uint8_t> out;
    out.reserve(kFrameHeadBytes + payload.size());
    putU32(out, static_cast<std::uint32_t>(payload.size()));
    putU32(out, frameCrc(position, payload.data(), payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

// The payload of the frame at `at`, or nullopt when it is truncated, oversized or fails its
// checksum. Advances `at` past the frame only on success.
std::optional<std::vector<std::uint8_t>> readFrame(const std::vector<std::uint8_t>& bytes, std::size_t& at, std::uint32_t position)
{
    if (bytes.size() - at < kFrameHeadBytes) {
        return std::nullopt;
    }
    const std::uint32_t len = getU32(bytes.data() + at);
    const std::uint32_t crc = getU32(bytes.data() + at + 4);
    if (len > delta_limits::kMaxEncodedBytes || len > bytes.size() - at - kFrameHeadBytes) {
        return std::nullopt;
    }
    const std::uint8_t* payload = bytes.data() + at + kFrameHeadBytes;
    if (frameCrc(position, payload, len) != crc) {
        return std::nullopt;
    }
    at += kFrameHeadBytes + len;
    return std::vector<std::uint8_t>(payload, payload + len);
}

std::optional<std::vector<std::uint8_t>> snapshotFrame(const DeltaState& snapshot)
{
    const auto payload = encodeDelta(computeDelta({}, snapshot));
    if (!payload) {
        return std::nullopt;
    }
    return makeFrame(0, *payload);
}

void fail(TailLoadResult* result, std::size_t discarded)
{
    if (result != nullptr) {
        *result = {TailLoadStatus::Unreadable, 0, discarded};
    }
}

}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size, std::uint32_t crc)
{
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

std::optional<SnapshotTailLog> SnapshotTailLog::fromSnapshot(DeltaState snapshot)
{
    const auto frame = snapshotFrame(snapshot);
    if (!frame || kHeaderBytes + frame->size() > snapshot_limits::kMaxImageBytes) {
        return std::nullopt;
    }
    SnapshotTailLog log;
    log.imageBytes_ = kHeaderBytes + frame->size();
    log.state_ = snapshot;
    log.snapshot_ = std::move(snapshot);
    return log;
}

bool SnapshotTailLog::append(const DeltaState& next)
{
    StateDelta delta = computeDelta(state_, next);
    if (delta.empty()) {
        return true;
    }
    if (tail_.size() >= snapshot_limits::kMaxTailRecords) {
        return false;
    }
    const auto payload = encodeDelta(delta);
    if (!payload || imageBytes_ + kFrameHeadBytes + payload->size() > snapshot_limits::kMaxImageBytes || !applyDelta(state_, delta)) {
        return false;
    }
    imageBytes_ += kFrameHeadBytes + payload->size();
    state_ = next;
    tail_.push_back(std::move(delta));
    return true;
}

bool SnapshotTailLog::compact()
{
    const auto frame = snapshotFrame(state_);
    if (!frame) {
        return false;
    }
    snapshot_ = state_;
    tail_.clear();
    imageBytes_ = kHeaderBytes + frame->size();
    return true;
}

std::optional<std::vector<std::uint8_t>> SnapshotTailLog::encode() const
{
    const auto head = snapshotFrame(snapshot_);
    if (!head) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> out(kMagic.begin(), kMagic.end());
    out.push_back(kSnapshotTailVersion);
    out.insert(out.end(), head->begin(), head->end());
    for (std::size_t i = 0; i < tail_.size(); ++i) {
        const auto frame = encodeTailFrame(i);
        if (!frame) {
            return std::nullopt;
        }
        out.insert(out.end(), frame->begin(), frame->end());
    }
    if (out.size() > snapshot_limits::kMaxImageBytes) {
        return std::nullopt;
    }
    return out;
}

std::optional<std::vector<std::uint8_t>> SnapshotTailLog::encodeTailFrame(std::size_t index) const
{
    if (index >= tail_.size()) {
        return std::nullopt;
    }
    const auto payload = encodeDelta(tail_[index]);
    if (!payload) {
        return std::nullopt;
    }
    return makeFrame(static_cast<std::uint32_t>(index + 1), *payload);
}

std::optional<SnapshotTailLog> SnapshotTailLog::load(const std::vector<std::uint8_t>& bytes, TailLoadResult* result)
{
    if (bytes.size() > snapshot_limits::kMaxImageBytes || bytes.size() < kHeaderBytes ||
        !std::equal(kMagic.begin(), kMagic.end(), bytes.begin()) || bytes[kMagic.size()] != kSnapshotTailVersion) {
        fail(result, bytes.size());
        return std::nullopt;
    }

    std::size_t at = kHeaderBytes;
    const auto head = readFrame(bytes, at, 0);
    const auto snapshotDelta = head ? decodeDelta(*head) : std::nullopt;
    const auto snapshot = (snapshotDelta && snapshotDelta->removed.empty()) ? applyDelta({}, *snapshotDelta) : std::nullopt;
    if (!snapshot) {
        fail(result, bytes.size());
        return std::nullopt;
    }

    auto log = fromSnapshot(*snapshot);
    if (!log) {
        fail(result, bytes.size());
        return std::nullopt;
    }
    while (at < bytes.size() && log->tail_.size() < snapshot_limits::kMaxTailRecords) {
        std::size_t next = at;
        const auto payload = readFrame(bytes, next, static_cast<std::uint32_t>(log->tail_.size() + 1));
        auto delta = payload ? decodeDelta(*payload) : std::nullopt;
        const auto state = (delta && !delta->empty()) ? applyDelta(log->state_, *delta) : std::nullopt;
        if (!state) {
            break;
        }
        log->state_ = *state;
        log->tail_.push_back(std::move(*delta));
        log->imageBytes_ += next - at;
        at = next;
    }

    if (result != nullptr) {
        result->status = at == bytes.size() ? TailLoadStatus::Clean : TailLoadStatus::RecoveredTornTail;
        result->recordsApplied = log->tail_.size();
        result->bytesDiscarded = bytes.size() - at;
    }
    return log;
}

}
