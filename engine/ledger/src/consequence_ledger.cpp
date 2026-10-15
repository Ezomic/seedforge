#include "ichor/ledger/consequence_ledger.h"

#include "ichor/core/binary.h"
#include "ichor/core/hash.h"

namespace ichor {

namespace {

constexpr std::uint32_t kMagic = 0x4743444Cu; // "LDCG"
constexpr std::uint32_t kVersion = 1;

bool plainKey(std::string_view text)
{
    if (text.empty() || text.size() > ConsequenceLedger::kMaxKeyBytes) return false;
    for (char c : text) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == ':';
        if (!ok) return false;
    }
    return true;
}

std::vector<const Fact*> collect(const std::vector<Fact>& facts, const std::vector<std::size_t>* indices)
{
    std::vector<const Fact*> out;
    if (indices == nullptr) return out;
    out.reserve(indices->size());
    for (std::size_t i : *indices) out.push_back(&facts[i]);
    return out;
}

std::uint64_t hashField(std::uint64_t h, std::string_view text)
{
    h = hashCombine(h, text.size());
    return hash64(text, h);
}

}

AppendResult ConsequenceLedger::append(Fact fact)
{
    if (!plainKey(fact.id) || !plainKey(fact.subject) || !plainKey(fact.kind) || fact.value.size() > kMaxValueBytes) {
        return AppendResult::InvalidField;
    }
    if (facts_.size() >= kMaxFacts) return AppendResult::Full;
    if (byId_.contains(fact.id)) return AppendResult::DuplicateId;
    const std::size_t index = facts_.size();
    byId_.emplace(fact.id, index);
    bySubject_[fact.subject].push_back(index);
    byKind_[fact.kind].push_back(index);
    facts_.push_back(std::move(fact));
    return AppendResult::Ok;
}

const Fact* ConsequenceLedger::find(std::string_view id) const
{
    const auto it = byId_.find(id);
    return it == byId_.end() ? nullptr : &facts_[it->second];
}

std::vector<const Fact*> ConsequenceLedger::bySubject(std::string_view subject) const
{
    const auto it = bySubject_.find(subject);
    return collect(facts_, it == bySubject_.end() ? nullptr : &it->second);
}

std::vector<const Fact*> ConsequenceLedger::byKind(std::string_view kind) const
{
    const auto it = byKind_.find(kind);
    return collect(facts_, it == byKind_.end() ? nullptr : &it->second);
}

std::vector<const Fact*> ConsequenceLedger::bySubjectAndKind(std::string_view subject, std::string_view kind) const
{
    std::vector<const Fact*> out;
    for (const Fact* fact : bySubject(subject)) {
        if (fact->kind == kind) out.push_back(fact);
    }
    return out;
}

std::uint64_t ConsequenceLedger::digest() const
{
    std::uint64_t h = hashCombine(kVersion, facts_.size());
    for (const Fact& fact : facts_) {
        h = hashField(h, fact.id);
        h = hashField(h, fact.subject);
        h = hashField(h, fact.kind);
        h = hashField(h, fact.value);
        h = hashCombine(h, fact.tick);
    }
    return h;
}

std::vector<std::uint8_t> ConsequenceLedger::serialize() const
{
    BinaryWriter writer;
    writer.write(kMagic);
    writer.write(kVersion);
    writer.write(facts_);
    return writer.take();
}

bool ConsequenceLedger::parse(std::span<const std::uint8_t> bytes, ConsequenceLedger& out)
{
    BinaryReader reader(bytes);
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::vector<Fact> facts;
    if (!reader.read(magic) || magic != kMagic || !reader.read(version) || version != kVersion) return false;
    if (!reader.read(facts) || !reader.atEnd()) return false;
    ConsequenceLedger parsed;
    for (Fact& fact : facts) {
        if (parsed.append(std::move(fact)) != AppendResult::Ok) return false;
    }
    out = std::move(parsed);
    return true;
}

}
