#include "ichor/world/enemy_reskin.h"

#include "ichor/core/hash.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <set>

namespace ichor {

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaxNameBytes = 64;
constexpr int kMaxJsonDepth = 3;
constexpr std::size_t kMaxProblems = 64;
constexpr std::array<std::string_view, 10> kKnownKeys = {"id", "kind", "key", "role", "material", "tint", "scale_permille", "speed_permille", "scale_jitter_permille", "gear"};

bool validName(const std::string& text)
{
    return !text.empty() && text.size() <= kMaxNameBytes;
}

bool validTint(const std::string& text)
{
    if (text.size() != 7 || text[0] != '#') {
        return false;
    }
    return std::all_of(text.begin() + 1, text.end(), [](unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); });
}

void readName(const Json& item, const char* key, const std::string& where, bool required, std::string& out, std::vector<std::string>& problems, const std::string& label)
{
    const auto found = item.find(key);
    if (found == item.end()) {
        if (required) {
            problems.push_back(label + ": " + where + "." + key + ": required");
        }
        return;
    }
    if (!found->is_string() || !validName(found->get<std::string>())) {
        problems.push_back(label + ": " + where + "." + key + ": must be a string of 1 to 64 bytes");
        return;
    }
    out = found->get<std::string>();
}

std::uint32_t readPermille(const Json& item, const char* key, const std::string& where, std::uint32_t fallback, std::uint32_t low, std::uint32_t high, std::vector<std::string>& problems, const std::string& label)
{
    const auto found = item.find(key);
    if (found == item.end()) {
        return fallback;
    }
    if (!found->is_number_unsigned() || found->get<std::uint64_t>() < low || found->get<std::uint64_t>() > high) {
        problems.push_back(label + ": " + where + "." + key + ": must be an integer from " + std::to_string(low) + " to " + std::to_string(high));
        return fallback;
    }
    return static_cast<std::uint32_t>(found->get<std::uint64_t>());
}

std::optional<EnemyRole> roleFromName(const std::string& name)
{
    for (std::size_t r = 0; r < kEnemyRoleCount; ++r) {
        if (enemyRoleName(static_cast<EnemyRole>(r)) == name) {
            return static_cast<EnemyRole>(r);
        }
    }
    return std::nullopt;
}

void parseLayer(const Json& item, std::size_t index, const std::string& label, std::set<std::string>& ids, std::vector<ReskinLayer>& out, std::vector<std::string>& problems)
{
    const std::string where = "[" + std::to_string(index) + "]";
    if (!item.is_object()) {
        problems.push_back(label + ": " + where + ": must be an object");
        return;
    }
    const std::size_t before = problems.size();
    for (const auto& [key, value] : item.items()) {
        if (std::find(kKnownKeys.begin(), kKnownKeys.end(), key) == kKnownKeys.end() && problems.size() < kMaxProblems) {
            problems.push_back(label + ": " + where + "." + key.substr(0, kMaxNameBytes) + ": unknown key");
        }
    }
    ReskinLayer layer;
    readName(item, "id", where, true, layer.id, problems, label);
    readName(item, "key", where, true, layer.key, problems, label);
    readName(item, "material", where, false, layer.material, problems, label);
    if (!layer.id.empty() && !ids.insert(layer.id).second) {
        problems.push_back(label + ": " + where + ".id: duplicate id " + layer.id);
    }
    const auto kind = item.find("kind");
    if (kind == item.end() || !kind->is_string() || (kind->get<std::string>() != "faction" && kind->get<std::string>() != "biome")) {
        problems.push_back(label + ": " + where + ".kind: must be \"faction\" or \"biome\"");
    } else {
        layer.kind = kind->get<std::string>() == "faction" ? ReskinKind::Faction : ReskinKind::Biome;
    }
    if (const auto role = item.find("role"); role != item.end()) {
        const auto parsed = role->is_string() ? roleFromName(role->get<std::string>()) : std::nullopt;
        if (!parsed) {
            problems.push_back(label + ": " + where + ".role: must be one of the six enemy role names");
        } else {
            layer.anyRole = false;
            layer.role = *parsed;
        }
    }
    if (const auto tint = item.find("tint"); tint != item.end()) {
        if (!tint->is_string() || !validTint(tint->get<std::string>())) {
            problems.push_back(label + ": " + where + ".tint: must be \"#RRGGBB\"");
        } else {
            layer.tint = tint->get<std::string>();
        }
    }
    layer.scalePermille = readPermille(item, "scale_permille", where, 1000, kMinReskinPermille, kMaxReskinPermille, problems, label);
    layer.speedPermille = readPermille(item, "speed_permille", where, 1000, kMinReskinPermille, kMaxReskinPermille, problems, label);
    layer.scaleJitterPermille = readPermille(item, "scale_jitter_permille", where, 0, 0, kMaxReskinJitterPermille, problems, label);
    if (const auto gear = item.find("gear"); gear != item.end()) {
        if (!gear->is_array() || gear->size() > kMaxReskinGear) {
            problems.push_back(label + ": " + where + ".gear: must be a list of at most " + std::to_string(kMaxReskinGear) + " names");
        } else {
            for (const Json& entry : *gear) {
                if (!entry.is_string() || !validName(entry.get<std::string>())) {
                    problems.push_back(label + ": " + where + ".gear: names must be strings of 1 to 64 bytes");
                    break;
                }
                layer.gear.push_back(entry.get<std::string>());
            }
        }
    }
    if (problems.size() == before) {
        out.push_back(std::move(layer));
    }
}

std::uint32_t clampPermille(std::uint64_t value)
{
    return static_cast<std::uint32_t>(std::clamp<std::uint64_t>(value, kMinReskinPermille, kMaxReskinPermille));
}

bool applies(const ReskinLayer& layer, ReskinKind kind, const std::string& key, EnemyRole role, bool roleSpecific)
{
    return layer.kind == kind && !layer.key.empty() && layer.key == key && layer.anyRole != roleSpecific && (layer.anyRole || layer.role == role);
}

}

bool parseReskinLayers(std::string_view label, std::string_view text, std::vector<ReskinLayer>& out, std::vector<std::string>& problems)
{
    const std::string name(label);
    const std::size_t before = problems.size();
    if (text.size() > kMaxReskinBytes) {
        problems.push_back(name + ": file: larger than the limit");
        return false;
    }
    bool tooDeep = false;
    const auto limitDepth = [&tooDeep](int depth, Json::parse_event_t event, Json&) {
        if ((event == Json::parse_event_t::object_start || event == Json::parse_event_t::array_start) && depth > kMaxJsonDepth) {
            tooDeep = true;
            return false;
        }
        return true;
    };
    const Json json = Json::parse(text.begin(), text.end(), limitDepth, false);
    if (tooDeep || json.is_discarded() || !json.is_array()) {
        problems.push_back(name + ": file: not a JSON array");
        return false;
    }
    if (json.size() > kMaxReskinLayers) {
        problems.push_back(name + ": file: too many layers");
        return false;
    }
    std::vector<ReskinLayer> layers;
    std::set<std::string> ids;
    for (std::size_t i = 0; i < json.size() && problems.size() < kMaxProblems; ++i) {
        parseLayer(json[i], i, name, ids, layers, problems);
    }
    if (problems.size() != before) {
        return false;
    }
    out = std::move(layers);
    return true;
}

ReskinParams resolveReskin(const ReskinRequest& request, const std::vector<ReskinLayer>& layers)
{
    std::vector<const ReskinLayer*> order;
    const auto collect = [&](ReskinKind kind, const std::string& key) {
        for (const bool roleSpecific : {false, true}) {
            std::vector<const ReskinLayer*> group;
            for (const ReskinLayer& layer : layers) {
                if (applies(layer, kind, key, request.role, roleSpecific)) {
                    group.push_back(&layer);
                }
            }
            std::stable_sort(group.begin(), group.end(), [](const ReskinLayer* a, const ReskinLayer* b) { return a->id < b->id; });
            order.insert(order.end(), group.begin(), group.end());
        }
    };
    collect(ReskinKind::Biome, request.biome);
    collect(ReskinKind::Faction, request.faction);

    ReskinParams out;
    std::uint64_t jitter = 0;
    for (const ReskinLayer* layer : order) {
        out.scalePermille = clampPermille(static_cast<std::uint64_t>(out.scalePermille) * layer->scalePermille / 1000);
        out.speedPermille = clampPermille(static_cast<std::uint64_t>(out.speedPermille) * layer->speedPermille / 1000);
        jitter += layer->scaleJitterPermille;
        if (!layer->material.empty()) {
            out.material = layer->material;
        }
        if (!layer->tint.empty()) {
            out.tint = layer->tint;
        }
        for (const std::string& piece : layer->gear) {
            if (out.gear.size() < kMaxReskinGear && std::find(out.gear.begin(), out.gear.end(), piece) == out.gear.end()) {
                out.gear.push_back(piece);
            }
        }
    }
    jitter = std::min<std::uint64_t>(jitter, kMaxReskinJitterPermille);
    if (jitter > 0) {
        const std::uint64_t draw = mix64(hashCombine(request.seed, static_cast<std::uint64_t>(request.role) + 1)) % (2 * jitter + 1);
        out.scalePermille = clampPermille(static_cast<std::uint64_t>(out.scalePermille) * (1000 + draw - jitter) / 1000);
    }
    return out;
}

}
