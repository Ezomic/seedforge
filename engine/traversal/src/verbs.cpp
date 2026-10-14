#include "ichor/traversal/verbs.h"

#include "ichor/core/wading.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace ichor {

namespace {

constexpr std::array<std::pair<std::string_view, Capability>, 6> kCapabilityNames = {{
    {"swim", Capability::Swim},
    {"dive", Capability::Dive},
    {"climb", Capability::Climb},
    {"descend", Capability::Descend},
    {"boat", Capability::Boat},
    {"cold_protection", Capability::ColdProtection},
}};

constexpr std::array<std::pair<std::string_view, Terrain>, 5> kTerrainNames = {{
    {"ground", Terrain::Ground},
    {"steep", Terrain::Steep},
    {"shallow_water", Terrain::ShallowWater},
    {"water", Terrain::Water},
    {"deep_water", Terrain::DeepWater},
}};

float cleanMetres(float value)
{
    return value > 0.0f ? value : 0.0f;
}

float cleanDegrees(float value)
{
    return value > 0.0f ? value : 0.0f;
}

std::string where(const std::string& id, const char* field)
{
    return "verb '" + id + "': \"" + field + "\" ";
}

bool readInt(const DataRecord& record, const char* name, int lo, int hi, int& out, std::string& error)
{
    const auto it = record.fields.find(name);
    const auto* value = it == record.fields.end() ? nullptr : std::get_if<double>(&it->second);
    if (value == nullptr || *value != std::floor(*value)) {
        error = where(record.id, name) + "must be an integer";
        return false;
    }
    if (*value < lo || *value > hi) {
        error = where(record.id, name) + "must be between " + std::to_string(lo) + " and " + std::to_string(hi);
        return false;
    }
    out = static_cast<int>(*value);
    return true;
}

}

CapabilitySet CapabilitySet::all()
{
    CapabilitySet set;
    for (const auto& [name, capability] : kCapabilityNames) {
        set.add(capability);
    }
    return set;
}

void CapabilitySet::add(Capability capability)
{
    if (capability != Capability::None) {
        bits_ |= 1u << static_cast<unsigned>(capability);
    }
}

bool CapabilitySet::has(Capability capability) const
{
    return capability == Capability::None || (bits_ & (1u << static_cast<unsigned>(capability))) != 0;
}

std::optional<Capability> capabilityFromName(const std::string& name)
{
    for (const auto& [key, capability] : kCapabilityNames) {
        if (key == name) {
            return capability;
        }
    }
    return std::nullopt;
}

std::optional<Terrain> terrainFromName(const std::string& name)
{
    for (const auto& [key, terrain] : kTerrainNames) {
        if (key == name) {
            return terrain;
        }
    }
    return std::nullopt;
}

float Verb::speedFactor(float waterDepth) const
{
    if (!depthScaled) {
        return static_cast<float>(speedPermille) / 1000.0f;
    }
    // The same operations, in the same order, as wadingSpeedFactor in world/water_field.cpp.
    if (!(waterDepth > kWadeStart)) {
        return 1.0f;
    }
    const float t = std::min((waterDepth - kWadeStart) / (kWadeFull - kWadeStart), 1.0f);
    const float drop = static_cast<float>(1000 - speedPermille) / 1000.0f;
    return 1.0f - drop * (t * t * (3.0f - 2.0f * t));
}

std::optional<VerbSet> VerbSet::fromTable(const DataTable& table, std::string& error)
{
    std::vector<Verb> verbs;
    for (const auto& [id, record] : table.records) {
        Verb verb;
        verb.id = id;

        const auto capability = record.fields.find("capability");
        if (capability != record.fields.end()) {
            const auto* name = std::get_if<std::string>(&capability->second);
            const auto parsed = name == nullptr ? std::nullopt : capabilityFromName(*name);
            if (!parsed) {
                error = where(id, "capability") + "must name a capability (swim, dive, climb, descend, boat or cold_protection)";
                return std::nullopt;
            }
            verb.needs = *parsed;
        }

        const auto terrain = record.fields.find("terrain");
        const auto* terrainName = terrain == record.fields.end() ? nullptr : std::get_if<std::string>(&terrain->second);
        const auto parsedTerrain = terrainName == nullptr ? std::nullopt : terrainFromName(*terrainName);
        if (!parsedTerrain) {
            error = where(id, "terrain") + "must be ground, steep, shallow_water, water or deep_water";
            return std::nullopt;
        }
        verb.terrain = *parsedTerrain;

        if (!readInt(record, "speed_permille", 0, kMaxSpeedPermille, verb.speedPermille, error)
            || !readInt(record, "stamina_per_second", 0, kMaxStaminaPerSecond, verb.staminaPerSecond, error)) {
            return std::nullopt;
        }

        const auto scaled = record.fields.find("depth_scaled");
        if (scaled != record.fields.end()) {
            const auto* flag = std::get_if<bool>(&scaled->second);
            if (flag == nullptr) {
                error = where(id, "depth_scaled") + "must be a boolean";
                return std::nullopt;
            }
            verb.depthScaled = *flag;
        }
        verbs.push_back(std::move(verb));
    }
    return fromVerbs(std::move(verbs), error);
}

std::optional<VerbSet> VerbSet::fromVerbs(std::vector<Verb> verbs, std::string& error)
{
    if (verbs.empty()) {
        error = "a verb set needs at least one verb";
        return std::nullopt;
    }
    std::sort(verbs.begin(), verbs.end(), [](const Verb& a, const Verb& b) { return a.id < b.id; });
    for (std::size_t i = 0; i < verbs.size(); ++i) {
        const Verb& verb = verbs[i];
        if (verb.id.empty()) {
            error = "a verb has an empty id";
            return std::nullopt;
        }
        if (i > 0 && verbs[i - 1].id == verb.id) {
            error = "duplicate verb id '" + verb.id + "'";
            return std::nullopt;
        }
        if (verb.speedPermille < 0 || verb.speedPermille > kMaxSpeedPermille) {
            error = where(verb.id, "speed_permille") + "must be between 0 and " + std::to_string(kMaxSpeedPermille);
            return std::nullopt;
        }
        if (verb.staminaPerSecond < 0 || verb.staminaPerSecond > kMaxStaminaPerSecond) {
            error = where(verb.id, "stamina_per_second") + "must be between 0 and " + std::to_string(kMaxStaminaPerSecond);
            return std::nullopt;
        }
    }
    VerbSet set;
    set.verbs_ = std::move(verbs);
    return set;
}

bool terrainFits(Terrain terrain, const Situation& situation)
{
    const float depth = cleanMetres(situation.waterDepth);
    const float slope = cleanDegrees(situation.slopeDegrees);
    const bool dry = !(depth > kWadeStart);
    switch (terrain) {
    case Terrain::Ground:
        return situation.onGround && dry && slope <= kSteepSlopeDegrees;
    case Terrain::Steep:
        return situation.onGround && dry && slope > kSteepSlopeDegrees;
    case Terrain::ShallowWater:
        return situation.onGround && !dry && depth < kWadeFull;
    case Terrain::Water:
        return !dry;
    case Terrain::DeepWater:
        return depth >= kWadeFull;
    }
    return false;
}

Availability availableVerbs(const VerbSet& set, const CapabilitySet& capabilities, const Situation& situation)
{
    Availability result;
    const float depth = cleanMetres(situation.waterDepth);
    float bestSpeed = 0.0f;
    for (std::size_t i = 0; i < set.verbs().size(); ++i) {
        const Verb& verb = set.verbs()[i];
        if (!capabilities.has(verb.needs) || !terrainFits(verb.terrain, situation)) {
            continue;
        }
        result.verbs.push_back(i);
        const float speed = verb.speedFactor(depth);
        if (!result.best || speed > bestSpeed) {
            result.best = i;
            bestSpeed = speed;
        }
    }
    if (result.best) {
        result.speedFactor = bestSpeed;
    }
    return result;
}

}
