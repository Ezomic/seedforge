#include "ichor/data/axiom.h"

#include "ichor/core/hash.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>

namespace ichor {

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaxFileBytes = 1024 * 1024;
constexpr int kMaxDepth = 16;
constexpr std::size_t kMaxTitle = 120;
constexpr std::size_t kMaxWord = 64;
constexpr std::size_t kMaxTag = 32;
constexpr std::size_t kMaxEntries = 64;
constexpr std::size_t kMinBlocks = 4;
constexpr std::string_view kEmDash = "\xE2\x80\x94";

constexpr std::array<AxiomSlot, 5> kSlots = {AxiomSlot::Cataclysm, AxiomSlot::Law, AxiomSlot::Powers, AxiomSlot::Pressure, AxiomSlot::Anomaly};

enum class Kind { Number, NonNegative, Text, Words, Weights };

struct KeySpec {
    std::string_view key;
    Kind kind;
};

struct BlockSpec {
    std::string_view name;
    std::vector<KeySpec> keys;
};

// Doc 01 only shows these keys, in its flood example; later catalogue tickets extend the table.
const std::array<BlockSpec, 9>& blockSpecs()
{
    static const std::array<BlockSpec, 9> specs = {{
        {"terrain", {{"sea_level_offset_m", Kind::Number}, {"coastal_erosion", Kind::Number}}},
        {"settlement", {{"min_elevation_m", Kind::Number}, {"prefer_slope", Kind::Text}, {"density_mult", Kind::NonNegative}}},
        {"biomes", {{"inject", Kind::Words}, {"suppress", Kind::Words}}},
        {"poi", {{"weights", Kind::Weights}}},
        {"enemies", {{"roster_add", Kind::Words}, {"roster_remove", Kind::Words}}},
        {"traversal", {{"unlock", Kind::Words}}},
        {"economy", {{"scarce", Kind::Words}, {"abundant", Kind::Words}}},
        {"palette", {{"grade", Kind::Text}, {"fog_density", Kind::NonNegative}}},
        {"audio", {{"bed_overrides", Kind::Words}, {"music_palette", Kind::Text}}},
    }};
    return specs;
}

const BlockSpec* findBlock(std::string_view name)
{
    for (const BlockSpec& spec : blockSpecs()) {
        if (spec.name == name) {
            return &spec;
        }
    }
    return nullptr;
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

bool isLower(char c) { return c >= 'a' && c <= 'z'; }
bool isUpper(char c) { return c >= 'A' && c <= 'Z'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }

// [a-z][a-z0-9_]*
bool isWord(std::string_view text)
{
    return !text.empty() && isLower(text[0]) && std::all_of(text.begin(), text.end(), [](char c) { return isLower(c) || isDigit(c) || c == '_'; });
}

// [A-Z][A-Z0-9_]*
bool isTag(std::string_view text)
{
    return !text.empty() && isUpper(text[0]) && std::all_of(text.begin(), text.end(), [](char c) { return isUpper(c) || isDigit(c) || c == '_'; });
}

// <slot word>.<name word>, lowercase ascii only, so ids hash and compare the same everywhere.
bool isId(std::string_view text)
{
    const std::size_t dot = text.find('.');
    return dot != std::string_view::npos && isWord(text.substr(0, dot)) && isWord(text.substr(dot + 1));
}

// Block nesting is counted before parsing, so a hostile file never reaches a recursive walk.
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

    bool number(const Json& json, std::string_view path, bool nonNegative, double& out)
    {
        if (!expect(json, path, json.is_number(), "a number")) {
            return false;
        }
        double value = json.get<double>();
        if (!std::isfinite(value)) {
            add(path, "must be finite");
            return false;
        }
        if (value == 0.0) {
            value = 0.0; // -0.0 would digest differently from 0.0
        }
        if (nonNegative && value < 0.0) {
            add(path, "must not be negative");
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

    bool list(const Json& json, std::string_view path, std::vector<const Json*>& items)
    {
        if (!expect(json, path, json.is_array(), "an array")) {
            return false;
        }
        if (json.size() > kMaxEntries) {
            add(path, "has more than " + std::to_string(kMaxEntries) + " entries");
            return false;
        }
        for (const Json& item : json) {
            items.push_back(&item);
        }
        return true;
    }

    static std::string entry(std::string_view path, std::size_t index) { return std::string(path) + "[" + std::to_string(index) + "]"; }

    bool words(const Json& json, std::string_view path, std::vector<std::string>& out)
    {
        std::vector<const Json*> items;
        if (!list(json, path, items)) {
            return false;
        }
        std::set<std::string> seen;
        bool ok = true;
        for (std::size_t i = 0; i < items.size(); ++i) {
            std::string value;
            if (!word(*items[i], entry(path, i), value)) {
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

    bool tags(const Json& json, std::string_view path, std::vector<std::string>& out)
    {
        std::vector<const Json*> items;
        if (!list(json, path, items)) {
            return false;
        }
        std::set<std::string> seen;
        bool ok = true;
        for (std::size_t i = 0; i < items.size(); ++i) {
            const std::string where = entry(path, i);
            if (!expect(*items[i], where, items[i]->is_string(), "a string")) {
                ok = false;
                continue;
            }
            const std::string& value = items[i]->get_ref<const std::string&>();
            if (value.size() > kMaxTag || !isTag(value)) {
                add(where, "expected an uppercase ascii tag of at most " + std::to_string(kMaxTag) + " bytes (letters, digits, underscores), got " + shown(value));
                ok = false;
            } else if (!seen.insert(value).second) {
                add(where, "duplicate " + shown(value));
                ok = false;
            } else {
                out.push_back(value);
            }
        }
        return ok;
    }

    bool conflicts(const Json& json, std::string_view path, std::string_view selfId, std::vector<std::string>& out)
    {
        std::vector<const Json*> items;
        if (!list(json, path, items)) {
            return false;
        }
        std::set<std::string> seen;
        bool ok = true;
        for (std::size_t i = 0; i < items.size(); ++i) {
            const std::string where = entry(path, i);
            if (!expect(*items[i], where, items[i]->is_string(), "a string")) {
                ok = false;
                continue;
            }
            const std::string& value = items[i]->get_ref<const std::string&>();
            if (value.size() > kMaxWord || !isId(value)) {
                add(where, "expected an axiom id like \"cataclysm.flood\", got " + shown(value));
                ok = false;
            } else if (value == selfId) {
                add(where, "an axiom cannot conflict with itself");
                ok = false;
            } else if (!seen.insert(value).second) {
                add(where, "duplicate " + shown(value));
                ok = false;
            } else {
                out.push_back(value);
            }
        }
        return ok;
    }

    bool weights(const Json& json, std::string_view path, std::map<std::string, double, std::less<>>& out)
    {
        if (!expect(json, path, json.is_object(), "an object")) {
            return false;
        }
        if (json.size() > kMaxEntries) {
            add(path, "has more than " + std::to_string(kMaxEntries) + " entries");
            return false;
        }
        bool ok = true;
        for (const auto& [key, value] : json.items()) {
            if (key.size() > kMaxWord || !isWord(key)) {
                add(path, "key " + shown(key) + " is not a lowercase ascii id (letters, digits, underscores)");
                ok = false;
                continue;
            }
            double weight = 0.0;
            if (number(value, std::string(path) + "." + key, true, weight)) {
                out.emplace(key, weight);
            } else {
                ok = false;
            }
        }
        return ok;
    }

    bool value(const Json& json, std::string_view path, Kind kind, AxiomValue& out)
    {
        switch (kind) {
        case Kind::Number:
        case Kind::NonNegative: {
            double number_ = 0.0;
            if (!number(json, path, kind == Kind::NonNegative, number_)) {
                return false;
            }
            out = number_;
            return true;
        }
        case Kind::Text: {
            std::string text_;
            if (!text(json, path, kMaxTitle, text_)) {
                return false;
            }
            out = std::move(text_);
            return true;
        }
        case Kind::Words: {
            std::vector<std::string> list_;
            if (!words(json, path, list_)) {
                return false;
            }
            out = std::move(list_);
            return true;
        }
        case Kind::Weights: {
            std::map<std::string, double, std::less<>> map_;
            if (!weights(json, path, map_)) {
                return false;
            }
            out = std::move(map_);
            return true;
        }
        }
        return false;
    }

    void block(const Json& json, const BlockSpec& spec, Axiom& axiom)
    {
        const std::string name(spec.name);
        if (!expect(json, name, json.is_object(), "an object")) {
            return;
        }
        if (json.empty()) {
            add(name, "must not be empty");
            return;
        }
        AxiomBlock parsed;
        bool ok = true;
        for (const auto& [key, item] : json.items()) {
            const auto known = std::find_if(spec.keys.begin(), spec.keys.end(), [&](const KeySpec& k) { return k.key == key; });
            if (known == spec.keys.end()) {
                add(name + "." + std::string(key.substr(0, 40)), "unknown key");
                ok = false;
                continue;
            }
            AxiomValue parsedValue;
            if (value(item, name + "." + key, known->kind, parsedValue)) {
                parsed.emplace(key, std::move(parsedValue));
            } else {
                ok = false;
            }
        }
        if (ok) {
            axiom.blocks.emplace(name, std::move(parsed));
        }
    }

private:
    std::string_view label_;
    std::vector<std::string>& problems_;
};

std::optional<AxiomSlot> slotFromName(std::string_view name)
{
    for (const AxiomSlot slot : kSlots) {
        if (axiomSlotName(slot) == name) {
            return slot;
        }
    }
    return std::nullopt;
}

std::string slotChoices()
{
    std::string out;
    for (const AxiomSlot slot : kSlots) {
        out += out.empty() ? "" : ", ";
        out += axiomSlotName(slot);
    }
    return out;
}

}

std::string_view axiomSlotName(AxiomSlot slot)
{
    switch (slot) {
    case AxiomSlot::Cataclysm:
        return "cataclysm";
    case AxiomSlot::Law:
        return "law";
    case AxiomSlot::Powers:
        return "powers";
    case AxiomSlot::Pressure:
        return "pressure";
    case AxiomSlot::Anomaly:
        return "anomaly";
    }
    return "";
}

char axiomSlotLetter(AxiomSlot slot)
{
    return static_cast<char>('A' + static_cast<int>(slot));
}

bool parseAxiom(std::string_view label, std::string_view text, Axiom& out, std::vector<std::string>& problems)
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
    if (!json.is_object()) {
        return fail("expected an object, got " + std::string(typeName(json)));
    }

    Axiom axiom;
    bool slotKnown = false;
    for (const auto& [key, item] : json.items()) {
        if (key != "id" && key != "slot" && key != "title" && key != "tags" && key != "conflicts" && findBlock(key) == nullptr) {
            check.add(key.substr(0, 40), "unknown key");
        }
    }

    if (const auto it = json.find("slot"); it == json.end()) {
        check.add("slot", "missing");
    } else if (check.expect(*it, "slot", it->is_string(), "a string")) {
        if (const std::optional<AxiomSlot> slot = slotFromName(it->get_ref<const std::string&>())) {
            axiom.slot = *slot;
            slotKnown = true;
        } else {
            check.add("slot", "expected one of " + slotChoices() + " (slots A to E), got " + shown(it->get_ref<const std::string&>()));
        }
    }

    if (const auto it = json.find("id"); it == json.end()) {
        check.add("id", "missing");
    } else if (check.expect(*it, "id", it->is_string(), "a string")) {
        const std::string& id = it->get_ref<const std::string&>();
        if (id.size() > kMaxWord) {
            check.add("id", "longer than " + std::to_string(kMaxWord) + " bytes");
        } else if (!isId(id)) {
            check.add("id", "expected \"<slot>.<name>\" in lowercase ascii letters, digits and underscores, got " + shown(id));
        } else if (slotKnown && id.compare(0, axiomSlotName(axiom.slot).size() + 1, std::string(axiomSlotName(axiom.slot)) + ".") != 0) {
            check.add("id", "must start with the slot name \"" + std::string(axiomSlotName(axiom.slot)) + ".\", got " + shown(id));
        } else {
            axiom.id = id;
        }
    }

    if (const auto it = json.find("title"); it == json.end()) {
        check.add("title", "missing");
    } else {
        check.text(*it, "title", kMaxTitle, axiom.title);
    }
    if (const auto it = json.find("tags"); it == json.end()) {
        check.add("tags", "missing");
    } else {
        check.tags(*it, "tags", axiom.tags);
    }
    if (const auto it = json.find("conflicts"); it == json.end()) {
        check.add("conflicts", "missing");
    } else {
        check.conflicts(*it, "conflicts", axiom.id, axiom.conflicts);
    }

    for (const BlockSpec& spec : blockSpecs()) {
        if (const auto it = json.find(spec.name); it != json.end()) {
            check.block(*it, spec, axiom);
        }
    }
    const auto present = static_cast<std::size_t>(std::count_if(blockSpecs().begin(), blockSpecs().end(), [&](const BlockSpec& spec) { return json.contains(spec.name); }));
    if (present < kMinBlocks) {
        check.add("blocks", "an axiom must touch at least " + std::to_string(kMinBlocks) + " directive blocks, found " + std::to_string(present));
    }

    if (problems.size() != before) {
        return false;
    }
    out = std::move(axiom);
    return true;
}

std::string axiomCanonicalJson(const Axiom& axiom)
{
    Json json = Json::object();
    json["id"] = axiom.id;
    json["slot"] = std::string(axiomSlotName(axiom.slot));
    json["title"] = axiom.title;
    json["tags"] = axiom.tags;
    json["conflicts"] = axiom.conflicts;
    for (const auto& [name, block] : axiom.blocks) {
        Json& target = json[name] = Json::object();
        for (const auto& [key, value] : block) {
            if (const double* number = std::get_if<double>(&value)) {
                target[key] = *number;
            } else if (const std::string* text = std::get_if<std::string>(&value)) {
                target[key] = *text;
            } else if (const auto* list = std::get_if<std::vector<std::string>>(&value)) {
                target[key] = *list;
            } else {
                Json weights = Json::object();
                for (const auto& [word, weight] : std::get<std::map<std::string, double, std::less<>>>(value)) {
                    weights[word] = weight;
                }
                target[key] = std::move(weights);
            }
        }
    }
    return json.dump(-1, ' ', false, Json::error_handler_t::replace);
}

std::uint64_t axiomDigest(const Axiom& axiom)
{
    return hash64(axiomCanonicalJson(axiom));
}

bool AxiomSet::loadDirectory(const std::filesystem::path& dataRoot)
{
    std::vector<std::string> problems;
    std::map<std::string, Axiom, std::less<>> axioms;
    const auto finish = [&] {
        const bool ok = problems.empty();
        problems_ = std::move(problems);
        if (ok) {
            axioms_ = std::move(axioms);
        }
        return ok;
    };

    const std::filesystem::path directory = dataRoot / "axioms";
    std::error_code code;
    if (!std::filesystem::is_directory(directory, code)) {
        problems.push_back("no axioms directory at " + directory.string());
        return finish();
    }
    std::vector<std::filesystem::path> files;
    for (std::filesystem::recursive_directory_iterator it(directory, code), end; !code && it != end; it.increment(code)) {
        if (it->is_regular_file(code) && it->path().extension() == ".json") {
            files.push_back(it->path());
        }
    }
    if (code) {
        problems.push_back("cannot read " + directory.string() + ": " + code.message());
        return finish();
    }
    std::sort(files.begin(), files.end());

    std::map<std::string, std::string, std::less<>> origin;
    for (const std::filesystem::path& file : files) {
        const std::string label = std::filesystem::relative(file, dataRoot).generic_string();
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
        Axiom axiom;
        if (!parseAxiom(label, text, axiom, problems)) {
            continue;
        }
        axiom.file = label;
        if (const auto seen = origin.find(axiom.id); seen != origin.end()) {
            problems.push_back(label + ": id: duplicate of \"" + axiom.id + "\" in " + seen->second);
            continue;
        }
        origin.emplace(axiom.id, label);
        axioms.emplace(axiom.id, std::move(axiom));
    }
    return finish();
}

const Axiom* AxiomSet::find(std::string_view id) const
{
    const auto it = axioms_.find(id);
    return it == axioms_.end() ? nullptr : &it->second;
}

std::uint64_t AxiomSet::digest() const
{
    std::uint64_t h = 0;
    for (const auto& [id, axiom] : axioms_) {
        h = hashCombine(hashCombine(h, hash64(id)), axiomDigest(axiom));
    }
    return h;
}

}
