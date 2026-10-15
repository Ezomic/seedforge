#pragma once

#include "ichor/core/reflect.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// One thing that happened to the world, recorded once and never changed.
// id, subject and kind use lowercase letters, digits and the characters - _ . :
struct Fact {
    std::string id;
    std::string subject;
    std::string kind;
    std::string value;
    std::uint64_t tick = 0;
    bool operator==(const Fact&) const = default;
};

template <>
struct Reflect<Fact> {
    static constexpr auto fields()
    {
        return std::make_tuple(field("id", &Fact::id), field("subject", &Fact::subject), field("kind", &Fact::kind),
            field("value", &Fact::value), field("tick", &Fact::tick));
    }
};

enum class AppendResult { Ok, InvalidField, DuplicateId, Full };

// The world's consequences in the order they happened. Facts can be added and
// read, never changed or removed, so the ledger is also the save file's record
// of what the player did (doc 14).
class ConsequenceLedger {
public:
    static constexpr std::size_t kMaxFacts = 1000000;
    static constexpr std::size_t kMaxKeyBytes = 128;
    static constexpr std::size_t kMaxValueBytes = 500;

    AppendResult append(Fact fact);

    const std::vector<Fact>& facts() const { return facts_; }
    std::size_t size() const { return facts_.size(); }
    bool empty() const { return facts_.empty(); }

    const Fact* find(std::string_view id) const;
    // Facts in append order. Empty when nothing matches.
    std::vector<const Fact*> bySubject(std::string_view subject) const;
    std::vector<const Fact*> byKind(std::string_view kind) const;
    std::vector<const Fact*> bySubjectAndKind(std::string_view subject, std::string_view kind) const;

    // Depends on every field of every fact and on their order.
    std::uint64_t digest() const;

    // parse() is all or nothing and applies the same rules as append().
    std::vector<std::uint8_t> serialize() const;
    static bool parse(std::span<const std::uint8_t> bytes, ConsequenceLedger& out);

private:
    std::vector<Fact> facts_;
    std::map<std::string, std::size_t, std::less<>> byId_;
    std::map<std::string, std::vector<std::size_t>, std::less<>> bySubject_;
    std::map<std::string, std::vector<std::size_t>, std::less<>> byKind_;
};

}
