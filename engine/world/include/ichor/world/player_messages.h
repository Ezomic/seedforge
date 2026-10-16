#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ichor {

// In-world player messages: a note left at an exact spot, built only from a fixed
// phrase vocabulary (a template plus an optional word), never free text. Pure
// logic: nothing here draws, networks or reads a clock. Times are whole seconds on
// a caller-supplied monotonic clock.

constexpr std::uint8_t kNoMessageWord = 0xFF;

std::size_t messageTemplateCount();
std::size_t messageWordCount();
bool messageTemplateTakesWord(std::uint8_t templateId);
// Empty when the pair is not a valid phrase.
std::string renderMessageText(std::uint8_t templateId, std::uint8_t wordId);

struct MessageLimits {
    std::uint32_t maxTotal = 4096;
    std::uint32_t maxPerAuthor = 8;
    std::uint32_t lifetimeSeconds = 7 * 24 * 3600;
    std::uint32_t minIntervalSeconds = 10; // between two placements by one author
    std::uint32_t windowSeconds = 600;     // rate window
    std::uint32_t maxPerWindow = 5;        // placements by one author inside the window
    std::uint32_t crowdRadiusMm = 2000;    // no more than maxInCrowd messages within this radius
    std::uint32_t maxInCrowd = 3;
    std::int32_t maxHorizontalMm = 1 << 28; // x and z must lie within +-this
    std::int32_t maxVerticalMm = 1 << 20;   // y likewise
};

// Position is fixed point millimetres so it is identical on every machine.
struct PlayerMessage {
    std::uint32_t id = 0;
    std::uint64_t author = 0; // opaque, never 0
    std::int32_t xMm = 0;
    std::int32_t yMm = 0; // up
    std::int32_t zMm = 0;
    std::uint16_t yaw = 0; // 65536 steps per turn
    std::uint8_t templateId = 0;
    std::uint8_t wordId = kNoMessageWord;
    std::uint32_t createdAt = 0;
    std::uint32_t expiresAt = 0;
};

struct MessageDraft {
    std::uint64_t author = 0;
    std::int32_t xMm = 0;
    std::int32_t yMm = 0;
    std::int32_t zMm = 0;
    std::uint16_t yaw = 0;
    std::uint8_t templateId = 0;
    std::uint8_t wordId = kNoMessageWord;
};

enum class PlaceStatus {
    Ok,
    BadAuthor,
    BadPhrase,
    OutOfRange,
    TooSoon,     // inside the minimum interval, or the clock went backwards
    RateLimited, // too many placements inside the window
    AuthorFull,
    BoardFull,
    Crowded,
};

struct PlaceResult {
    PlaceStatus status = PlaceStatus::BadAuthor;
    std::uint32_t id = 0; // valid when status is Ok
    bool ok() const { return status == PlaceStatus::Ok; }
};

class MessageBoard {
public:
    explicit MessageBoard(MessageLimits limits = {});

    PlaceResult place(const MessageDraft& draft, std::uint32_t now);
    // Removes every message whose expiry is at or before now.
    std::size_t expire(std::uint32_t now);
    bool remove(std::uint32_t id);

    // Live messages within radiusMm (3D) of the point, ordered by id.
    std::vector<PlayerMessage> near(std::int32_t xMm, std::int32_t yMm, std::int32_t zMm, std::uint32_t radiusMm,
                                    std::uint32_t now) const;

    std::size_t size() const { return messages_.size(); }
    const std::vector<PlayerMessage>& all() const { return messages_; } // ordered by id
    std::uint32_t nextId() const { return nextId_; }
    const MessageLimits& limits() const { return limits_; }

    // Validates and loads records; used by the decoder. False leaves the board unchanged.
    bool restore(std::vector<PlayerMessage> messages, std::uint32_t nextId);

private:
    struct AuthorHistory {
        std::uint64_t author = 0;
        std::vector<std::uint32_t> placedAt; // at most maxPerWindow entries
    };

    AuthorHistory* history(std::uint64_t author, bool create);

    MessageLimits limits_;
    std::vector<PlayerMessage> messages_;
    std::vector<AuthorHistory> histories_; // ordered by author
    std::uint32_t nextId_ = 1;
};

// Layout, little-endian:
//   "ICPM" | u8 version (1) | u32 nextId | u32 count |
//   per message: u32 id | u64 author | i32 x | i32 y | i32 z | u16 yaw |
//                u8 template | u8 word | u32 createdAt | u32 expiresAt   (36 bytes)
// Rate-limit history is session state and is not encoded.
std::vector<std::uint8_t> encodeMessages(const MessageBoard& board);

enum class MessageDecodeStatus {
    Ok,
    BadMagic,
    BadVersion,
    Truncated,
    TrailingBytes,
    TooMany,
    InvalidRecord,
};

struct MessageDecodeResult {
    MessageDecodeStatus status = MessageDecodeStatus::Truncated;
    MessageBoard board;
    bool ok() const { return status == MessageDecodeStatus::Ok; }
};

// Never throws or crashes on any input; on failure the returned board is empty.
MessageDecodeResult decodeMessages(std::span<const std::uint8_t> bytes, const MessageLimits& limits = {});

} // namespace ichor
