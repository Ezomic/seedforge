#include "ichor/data/npc_tiers.h"

#include "ichor/core/hash.h"
#include "ichor/data/npc_traits.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <set>

namespace ichor {

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaxFileBytes = 1024 * 1024;
constexpr int kMaxDepth = 8;
constexpr std::size_t kMaxRecords = 5000;
constexpr std::size_t kMaxWord = 64;
constexpr std::size_t kMaxName = 80;
constexpr std::size_t kMaxSentence = 160;
constexpr std::size_t kMaxCastRoles = 8;
constexpr std::string_view kEmDash = "\xE2\x80\x94";

constexpr std::array<NpcTier, 4> kTiers = {NpcTier::Principal, NpcTier::Regular, NpcTier::Resident, NpcTier::Crowd};
constexpr std::array<RelationKind, 5> kKinds = {RelationKind::Family, RelationKind::Rival, RelationKind::Creditor, RelationKind::Lover, RelationKind::Grudge};

enum class Field { Name, Voice, Arc, Vo, Face, CastRoles, Home, Role, Traits, Relationships, Want, Fear, Memory, Location, Ambience };

struct FieldSpec {
    std::string_view key;
    Field field;
};

constexpr std::array<FieldSpec, 15> kFields = {{
    {"name", Field::Name}, {"voice", Field::Voice}, {"arc", Field::Arc}, {"vo", Field::Vo}, {"face", Field::Face},
    {"cast_roles", Field::CastRoles}, {"home", Field::Home}, {"role", Field::Role}, {"traits", Field::Traits},
    {"relationships", Field::Relationships}, {"want", Field::Want}, {"fear", Field::Fear}, {"memory", Field::Memory},
    {"location", Field::Location}, {"ambience", Field::Ambience},
}};

// Doc 10, "Four tiers" and "Anatomy of a Regular": the fields each tier has, and no others.
std::vector<Field> fieldsOf(NpcTier tier)
{
    switch (tier) {
    case NpcTier::Principal:
        return {Field::Name, Field::Voice, Field::Arc, Field::Vo, Field::Face, Field::CastRoles};
    case NpcTier::Regular:
        return {Field::Name, Field::Home, Field::Role, Field::Traits, Field::Relationships, Field::Want, Field::Fear, Field::Memory};
    case NpcTier::Resident:
        return {Field::Name, Field::Location};
    case NpcTier::Crowd:
        return {Field::Ambience};
    }
    return {};
}

std::string_view typeName(const Json& json)
{
    if (json.is_null()) {
        return "null";
    }
    if (json.is_boolean()) {
        return "boolean";
    }
    if (json.is_number()) {
        return "number";
    }
    if (json.is_string()) {
        return "string";
    }
    if (json.is_array()) {
        return "array";
    }
    return "object";
}

std::string shown(std::string_view text)
{
    constexpr std::size_t kLimit = 40;
    std::string out = "\"";
    out.append(text.substr(0, kLimit));
    if (text.size() > kLimit) {
        out += "...";
    }
    return out + "\"";
}

bool isBlank(std::string_view text)
{
    return std::all_of(text.begin(), text.end(), [](unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; });
}

// [a-z][a-z0-9_]*
bool isWord(std::string_view text)
{
    const auto lower = [](char c) { return c >= 'a' && c <= 'z'; };
    return !text.empty() && lower(text[0]) && std::all_of(text.begin(), text.end(), [&](char c) { return lower(c) || (c >= '0' && c <= '9') || c == '_'; });
}

// Nesting is counted before parsing, so a hostile file never reaches a recursive walk.
bool nestsTooDeep(std::string_view text)
{
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (const char c : text) {
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
        } else if (c == '"') {
            inString = true;
        } else if (c == '[' || c == '{') {
            if (++depth > kMaxDepth) {
                return true;
            }
        } else if ((c == ']' || c == '}') && depth > 0) {
            --depth;
        }
    }
    return false;
}

std::string entry(std::string_view path, std::size_t index)
{
    return std::string(path) + "[" + std::to_string(index) + "]";
}

std::string join(std::string_view parent, std::string_view key)
{
    return std::string(parent) + "." + std::string(key);
}

class Checker {
public:
    Checker(std::string_view label, std::vector<std::string>& problems)
        : label_(label)
        , problems_(problems)
    {
    }

    void add(std::string_view path, const std::string& message)
    {
        std::string line(label_);
        line += ": ";
        if (!path.empty()) {
            line += path;
            line += ": ";
        }
        problems_.push_back(line + message);
    }

    bool expect(const Json& json, std::string_view path, bool ok, std::string_view wanted)
    {
        if (!ok) {
            add(path, "expected " + std::string(wanted) + ", got " + std::string(typeName(json)));
        }
        return ok;
    }

    bool text(const Json& json, std::string_view path, std::size_t maxBytes, std::string& out)
    {
        if (!expect(json, path, json.is_string(), "a string")) {
            return false;
        }
        const std::string& value = json.get_ref<const std::string&>();
        if (isBlank(value)) {
            add(path, "must not be empty");
            return false;
        }
        if (value.size() > maxBytes) {
            add(path, "longer than " + std::to_string(maxBytes) + " bytes");
            return false;
        }
        if (value.find(kEmDash) != std::string::npos) {
            add(path, "must not contain an em-dash");
            return false;
        }
        out = value;
        return true;
    }

    bool word(const Json& json, std::string_view path, std::string& out)
    {
        if (!expect(json, path, json.is_string(), "a string")) {
            return false;
        }
        const std::string& value = json.get_ref<const std::string&>();
        if (value.size() > kMaxWord) {
            add(path, "longer than " + std::to_string(kMaxWord) + " bytes");
            return false;
        }
        if (!isWord(value)) {
            add(path, "expected a lowercase ascii id (letters, digits, underscores), got " + shown(value));
            return false;
        }
        out = value;
        return true;
    }

    bool words(const Json& json, std::string_view path, std::size_t min, std::size_t max, std::vector<std::string>& out)
    {
        if (!expect(json, path, json.is_array(), "an array")) {
            return false;
        }
        if (json.size() < min || json.size() > max) {
            add(path, "expected " + std::to_string(min) + " to " + std::to_string(max) + " entries, got " + std::to_string(json.size()));
            return false;
        }
        std::set<std::string> seen;
        bool ok = true;
        for (std::size_t i = 0; i < json.size(); ++i) {
            std::string value;
            if (!word(json[i], entry(path, i), value)) {
                ok = false;
            } else if (!seen.insert(value).second) {
                add(entry(path, i), "duplicate " + shown(value));
                ok = false;
            } else {
                out.push_back(value);
            }
        }
        return ok;
    }

    bool relationships(const Json& json, std::string_view path, std::vector<Relationship>& out)
    {
        if (!expect(json, path, json.is_array(), "an array")) {
            return false;
        }
        if (json.size() < kMinRelationships || json.size() > kMaxRelationships) {
            add(path, "expected " + std::to_string(kMinRelationships) + " to " + std::to_string(kMaxRelationships) + " entries, got " + std::to_string(json.size()));
            return false;
        }
        std::set<std::pair<int, std::string>> seen;
        bool ok = true;
        for (std::size_t i = 0; i < json.size(); ++i) {
            Relationship relation;
            if (!relationship(json[i], entry(path, i), relation)) {
                ok = false;
            } else if (!seen.emplace(static_cast<int>(relation.kind), relation.target).second) {
                add(entry(path, i), "duplicate " + std::string(relationKindName(relation.kind)) + " of " + shown(relation.target));
                ok = false;
            } else {
                out.push_back(std::move(relation));
            }
        }
        return ok;
    }

private:
    bool relationship(const Json& json, const std::string& path, Relationship& out)
    {
        if (!expect(json, path, json.is_object(), "an object")) {
            return false;
        }
        bool ok = true;
        for (const auto& [key, item] : json.items()) {
            if (key != "kind" && key != "target") {
                add(join(path, key.substr(0, 40)), "unknown key");
                ok = false;
            }
        }
        if (const auto it = json.find("kind"); it == json.end()) {
            add(join(path, "kind"), "missing");
            ok = false;
        } else if (expect(*it, join(path, "kind"), it->is_string(), "a string")) {
            const auto kind = std::find_if(kKinds.begin(), kKinds.end(), [&](RelationKind k) { return relationKindName(k) == it->get_ref<const std::string&>(); });
            if (kind == kKinds.end()) {
                std::string choices;
                for (const RelationKind k : kKinds) {
                    choices += choices.empty() ? "" : ", ";
                    choices += relationKindName(k);
                }
                add(join(path, "kind"), "expected one of " + choices + ", got " + shown(it->get_ref<const std::string&>()));
                ok = false;
            } else {
                out.kind = *kind;
            }
        } else {
            ok = false;
        }
        if (const auto it = json.find("target"); it == json.end()) {
            add(join(path, "target"), "missing");
            ok = false;
        } else if (!word(*it, join(path, "target"), out.target)) {
            ok = false;
        }
        return ok;
    }

    std::string_view label_;
    std::vector<std::string>& problems_;
};

std::string tierChoices()
{
    std::string out;
    for (const NpcTier tier : kTiers) {
        out += out.empty() ? "" : ", ";
        out += npcTierName(tier);
    }
    return out;
}

// Reads one field of a record whose tier is known. The caller has already checked the key is allowed.
bool readField(Checker& check, const Json& json, const std::string& path, Field field, NpcRecord& record)
{
    const auto visit = [&](auto& target) {
        using T = std::decay_t<decltype(target)>;
        const auto str = [&](std::string& out, bool isWordField, std::size_t max = kMaxName) {
            return isWordField ? check.word(json, path, out) : check.text(json, path, max, out);
        };
        switch (field) {
        case Field::Name:
            if constexpr (!std::is_same_v<T, Crowd>) {
                return str(target.name, false);
            }
            break;
        case Field::Voice:
            if constexpr (std::is_same_v<T, Principal>) {
                return str(target.voice, true);
            }
            break;
        case Field::Arc:
            if constexpr (std::is_same_v<T, Principal>) {
                return str(target.arc, true);
            }
            break;
        case Field::Vo:
            if constexpr (std::is_same_v<T, Principal>) {
                return str(target.vo, true);
            }
            break;
        case Field::Face:
            if constexpr (std::is_same_v<T, Principal>) {
                return str(target.face, true);
            }
            break;
        case Field::CastRoles:
            if constexpr (std::is_same_v<T, Principal>) {
                return check.words(json, path, 1, kMaxCastRoles, target.castRoles);
            }
            break;
        case Field::Home:
            if constexpr (std::is_same_v<T, Regular>) {
                return str(target.home, true);
            }
            break;
        case Field::Role:
            if constexpr (std::is_same_v<T, Regular>) {
                return str(target.role, true);
            }
            break;
        case Field::Traits:
            if constexpr (std::is_same_v<T, Regular>) {
                return check.words(json, path, kMinNpcTraits, kMaxNpcTraits, target.traits);
            }
            break;
        case Field::Relationships:
            if constexpr (std::is_same_v<T, Regular>) {
                return check.relationships(json, path, target.relationships);
            }
            break;
        case Field::Want:
            if constexpr (std::is_same_v<T, Regular>) {
                return str(target.want, false, kMaxSentence);
            }
            break;
        case Field::Fear:
            if constexpr (std::is_same_v<T, Regular>) {
                return str(target.fear, false, kMaxSentence);
            }
            break;
        case Field::Memory:
            if constexpr (std::is_same_v<T, Regular>) {
                return str(target.memory, true);
            }
            break;
        case Field::Location:
            if constexpr (std::is_same_v<T, Resident>) {
                return str(target.location, true);
            }
            break;
        case Field::Ambience:
            if constexpr (std::is_same_v<T, Crowd>) {
                return str(target.ambience, true);
            }
            break;
        }
        return false;
    };
    return std::visit(visit, record);
}

NpcRecord emptyRecord(NpcTier tier)
{
    switch (tier) {
    case NpcTier::Principal:
        return Principal {};
    case NpcTier::Regular:
        return Regular {};
    case NpcTier::Resident:
        return Resident {};
    case NpcTier::Crowd:
        return Crowd {};
    }
    return Crowd {};
}

std::string& idRef(NpcRecord& record)
{
    return std::visit([](auto& r) -> std::string& { return r.id; }, record);
}

bool parseRecord(Checker& check, const Json& json, const std::string& path, NpcRecord& out)
{
    if (!check.expect(json, path, json.is_object(), "an object")) {
        return false;
    }
    bool ok = true;

    std::string id;
    if (const auto it = json.find("id"); it == json.end()) {
        check.add(join(path, "id"), "missing");
        ok = false;
    } else if (!check.word(*it, join(path, "id"), id)) {
        ok = false;
    }

    const auto tierIt = json.find("tier");
    if (tierIt == json.end()) {
        check.add(join(path, "tier"), "missing");
        return false;
    }
    if (!check.expect(*tierIt, join(path, "tier"), tierIt->is_string(), "a string")) {
        return false;
    }
    const auto tier = std::find_if(kTiers.begin(), kTiers.end(), [&](NpcTier t) { return npcTierName(t) == tierIt->get_ref<const std::string&>(); });
    if (tier == kTiers.end()) {
        check.add(join(path, "tier"), "expected one of " + tierChoices() + ", got " + shown(tierIt->get_ref<const std::string&>()));
        return false;
    }

    NpcRecord record = emptyRecord(*tier);
    idRef(record) = id;
    const std::vector<Field> allowed = fieldsOf(*tier);

    for (const auto& [key, item] : json.items()) {
        if (key == "id" || key == "tier") {
            continue;
        }
        const auto spec = std::find_if(kFields.begin(), kFields.end(), [&](const FieldSpec& s) { return s.key == key; });
        if (spec == kFields.end()) {
            check.add(join(path, key.substr(0, 40)), "unknown key");
            ok = false;
        } else if (std::find(allowed.begin(), allowed.end(), spec->field) == allowed.end()) {
            check.add(join(path, key), "not allowed for tier " + std::string(npcTierName(*tier)));
            ok = false;
        }
    }
    for (const Field field : allowed) {
        const auto spec = std::find_if(kFields.begin(), kFields.end(), [&](const FieldSpec& s) { return s.field == field; });
        const auto it = json.find(spec->key);
        if (it == json.end()) {
            check.add(join(path, spec->key), "missing");
            ok = false;
        } else if (!readField(check, *it, join(path, spec->key), field, record)) {
            ok = false;
        }
    }
    if (ok) {
        out = std::move(record);
    }
    return ok;
}

Json canonical(const NpcRecord& record)
{
    Json json = Json::object();
    std::visit(
        [&](const auto& r) {
            using T = std::decay_t<decltype(r)>;
            json["id"] = r.id;
            if constexpr (std::is_same_v<T, Principal>) {
                json["tier"] = "principal";
                json["name"] = r.name;
                json["voice"] = r.voice;
                json["arc"] = r.arc;
                json["vo"] = r.vo;
                json["face"] = r.face;
                json["cast_roles"] = r.castRoles;
            } else if constexpr (std::is_same_v<T, Regular>) {
                json["tier"] = "regular";
                json["name"] = r.name;
                json["home"] = r.home;
                json["role"] = r.role;
                json["traits"] = r.traits;
                Json relations = Json::array();
                for (const Relationship& relation : r.relationships) {
                    relations.push_back({{"kind", std::string(relationKindName(relation.kind))}, {"target", relation.target}});
                }
                json["relationships"] = std::move(relations);
                json["want"] = r.want;
                json["fear"] = r.fear;
                json["memory"] = r.memory;
            } else if constexpr (std::is_same_v<T, Resident>) {
                json["tier"] = "resident";
                json["name"] = r.name;
                json["location"] = r.location;
            } else {
                json["tier"] = "crowd";
                json["ambience"] = r.ambience;
            }
        },
        record);
    return json;
}

}

std::string_view npcTierName(NpcTier tier)
{
    switch (tier) {
    case NpcTier::Principal:
        return "principal";
    case NpcTier::Regular:
        return "regular";
    case NpcTier::Resident:
        return "resident";
    case NpcTier::Crowd:
        return "crowd";
    }
    return "";
}

std::string_view relationKindName(RelationKind kind)
{
    switch (kind) {
    case RelationKind::Family:
        return "family";
    case RelationKind::Rival:
        return "rival";
    case RelationKind::Creditor:
        return "creditor";
    case RelationKind::Lover:
        return "lover";
    case RelationKind::Grudge:
        return "grudge";
    }
    return "";
}

NpcTier npcTier(const NpcRecord& record)
{
    return static_cast<NpcTier>(record.index());
}

const std::string& npcId(const NpcRecord& record)
{
    return std::visit([](const auto& r) -> const std::string& { return r.id; }, record);
}

bool parseNpcFile(std::string_view label, std::string_view text, std::vector<NpcRecord>& out, std::vector<std::string>& problems)
{
    const std::size_t before = problems.size();
    Checker check(label, problems);
    const auto fail = [&](const std::string& message) {
        check.add("", message);
        return false;
    };
    if (text.size() > kMaxFileBytes) {
        return fail("file is larger than " + std::to_string(kMaxFileBytes) + " bytes");
    }
    if (isBlank(text)) {
        return fail("empty file");
    }
    if (nestsTooDeep(text)) {
        return fail("nested deeper than " + std::to_string(kMaxDepth) + " levels");
    }
    // The parser keeps the last of two equal keys; a duplicate is an authoring mistake, so it is refused.
    std::vector<std::set<std::string>> keysByDepth;
    std::string duplicate;
    const Json json = Json::parse(
        text.begin(), text.end(),
        [&](int depth, Json::parse_event_t event, Json& parsed) {
            if (event == Json::parse_event_t::object_start) {
                keysByDepth.resize(std::max(keysByDepth.size(), static_cast<std::size_t>(depth) + 2));
                keysByDepth[static_cast<std::size_t>(depth) + 1].clear();
            } else if (event == Json::parse_event_t::key && !keysByDepth[static_cast<std::size_t>(depth)].insert(parsed.get<std::string>()).second && duplicate.empty()) {
                duplicate = parsed.get<std::string>().substr(0, 40);
            }
            return true;
        },
        false);
    if (json.is_discarded()) {
        return fail("not valid JSON");
    }
    if (!duplicate.empty()) {
        return fail("duplicate key \"" + duplicate + "\"");
    }
    if (!json.is_array()) {
        return fail("expected an array, got " + std::string(typeName(json)));
    }
    if (json.empty()) {
        return fail("no records");
    }
    if (json.size() > kMaxRecords) {
        return fail("has more than " + std::to_string(kMaxRecords) + " records");
    }

    std::vector<NpcRecord> records;
    std::map<std::string, std::size_t, std::less<>> firstAt;
    for (std::size_t i = 0; i < json.size(); ++i) {
        NpcRecord record = Crowd {};
        const std::string path = "[" + std::to_string(i) + "]";
        if (!parseRecord(check, json[i], path, record)) {
            continue;
        }
        if (const auto seen = firstAt.find(npcId(record)); seen != firstAt.end()) {
            check.add(path + ".id", "duplicate of \"" + npcId(record) + "\" at [" + std::to_string(seen->second) + "]");
            continue;
        }
        firstAt.emplace(npcId(record), i);
        records.push_back(std::move(record));
    }

    if (problems.size() != before) {
        return false;
    }
    out = std::move(records);
    return true;
}

std::string npcCanonicalJson(const NpcRecord& record)
{
    return canonical(record).dump(-1, ' ', false, Json::error_handler_t::replace);
}

std::uint64_t npcDigest(const NpcRecord& record)
{
    return hash64(npcCanonicalJson(record));
}

bool NpcRoster::loadDirectory(const std::filesystem::path& directory)
{
    std::vector<std::string> problems;
    std::map<std::string, NpcRecord, std::less<>> records;
    const auto finish = [&] {
        const bool ok = problems.empty();
        problems_ = std::move(problems);
        if (ok) {
            records_ = std::move(records);
        }
        return ok;
    };

    std::error_code code;
    if (!std::filesystem::is_directory(directory, code)) {
        problems.push_back("no NPC directory at " + directory.string());
        return finish();
    }
    std::vector<std::filesystem::path> files;
    for (std::filesystem::directory_iterator it(directory, code), end; !code && it != end; it.increment(code)) {
        if (it->is_regular_file(code) && it->path().extension() == ".json") {
            files.push_back(it->path());
        }
    }
    if (code) {
        problems.push_back("cannot read " + directory.string() + ": " + code.message());
        return finish();
    }
    std::sort(files.begin(), files.end());

    struct Origin {
        std::string label;
        std::size_t index;
    };
    std::map<std::string, Origin, std::less<>> origin;
    std::vector<std::pair<std::string, std::vector<NpcRecord>>> parsed;
    for (const std::filesystem::path& file : files) {
        const std::string label = file.filename().generic_string();
        const std::uintmax_t size = std::filesystem::file_size(file, code);
        if (code) {
            problems.push_back(label + ": cannot read file: " + code.message());
            continue;
        }
        if (size > kMaxFileBytes) {
            problems.push_back(label + ": file is larger than " + std::to_string(kMaxFileBytes) + " bytes");
            continue;
        }
        std::ifstream stream(file, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        std::vector<NpcRecord> fileRecords;
        if (parseNpcFile(label, text, fileRecords, problems)) {
            parsed.emplace_back(label, std::move(fileRecords));
        }
    }

    for (auto& [label, fileRecords] : parsed) {
        for (std::size_t i = 0; i < fileRecords.size(); ++i) {
            const std::string id = npcId(fileRecords[i]);
            if (const auto seen = origin.find(id); seen != origin.end()) {
                problems.push_back(label + ": [" + std::to_string(i) + "].id: duplicate of \"" + id + "\" in " + seen->second.label);
                continue;
            }
            origin.emplace(id, Origin {label, i});
            records.emplace(id, std::move(fileRecords[i]));
        }
    }

    for (const auto& [id, record] : records) {
        const auto* regular = std::get_if<Regular>(&record);
        if (regular == nullptr) {
            continue;
        }
        const Origin& from = origin.at(id);
        for (std::size_t i = 0; i < regular->relationships.size(); ++i) {
            const std::string& target = regular->relationships[i].target;
            const std::string where = from.label + ": [" + std::to_string(from.index) + "].relationships[" + std::to_string(i) + "].target: ";
            const auto found = records.find(target);
            if (target == id) {
                problems.push_back(where + "an NPC cannot be related to itself");
            } else if (found == records.end()) {
                problems.push_back(where + "unknown NPC " + shown(target));
            } else if (npcTier(found->second) == NpcTier::Crowd) {
                problems.push_back(where + shown(target) + " is crowd and has no name");
            }
        }
    }
    return finish();
}

const NpcRecord* NpcRoster::find(std::string_view id) const
{
    const auto it = records_.find(id);
    return it == records_.end() ? nullptr : &it->second;
}

std::size_t NpcRoster::count(NpcTier tier) const
{
    return static_cast<std::size_t>(std::count_if(records_.begin(), records_.end(), [&](const auto& entry_) { return npcTier(entry_.second) == tier; }));
}

std::uint64_t NpcRoster::digest() const
{
    std::uint64_t h = 0;
    for (const auto& [id, record] : records_) {
        h = hashCombine(hashCombine(h, hash64(id)), npcDigest(record));
    }
    return h;
}

}
