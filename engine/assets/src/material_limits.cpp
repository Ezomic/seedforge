#include "ichor/assets/material_limits.h"

#include "ichor/core/log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <set>

namespace ichor {

namespace {

MaterialLimits g_active;

bool outside(float value, float lo, float hi)
{
    return !(value >= lo && value <= hi);
}

bool metal(const Material& material)
{
    return material.metallicFactor >= 0.5f;
}

}

bool isValid(const MaterialLimits& l)
{
    const auto ordered = [](float lo, float hi) { return std::isfinite(lo) && std::isfinite(hi) && lo <= hi; };
    return ordered(l.roughnessMin, l.roughnessMax) && ordered(l.albedoMin, l.albedoMax) && ordered(l.metalF0Min, l.metalF0Max)
        && l.roughnessMin >= 0.0f && l.roughnessMax <= 1.0f && l.metalF0Min >= 0.0f && l.metalF0Max <= 1.0f
        && std::isfinite(l.dielectricF0) && l.dielectricF0 >= 0.0f && l.dielectricF0 <= 1.0f;
}

float clampToRange(float value, float lo, float hi, float fallback)
{
    if (std::isnan(value)) {
        value = fallback;
    }
    return std::clamp(value, lo, hi);
}

std::string_view materialFieldName(MaterialField field)
{
    switch (field) {
    case MaterialField::Roughness:
        return "roughness";
    case MaterialField::Metallic:
        return "metallic";
    case MaterialField::MetalF0:
        return "metal_f0";
    }
    return "";
}

std::vector<MaterialIssue> findMaterialIssues(const std::vector<Material>& materials, const MaterialLimits& limits)
{
    std::vector<MaterialIssue> issues;
    for (std::uint32_t i = 0; i < materials.size(); ++i) {
        const Material& m = materials[i];
        const auto report = [&](MaterialField field, float value, float lo, float hi) {
            issues.push_back({i, m.name, field, value, lo, hi});
        };
        if (outside(m.roughnessFactor, limits.roughnessMin, limits.roughnessMax)) {
            report(MaterialField::Roughness, m.roughnessFactor, limits.roughnessMin, limits.roughnessMax);
        }
        if (outside(m.metallicFactor, 0.0f, 1.0f)) {
            report(MaterialField::Metallic, m.metallicFactor, 0.0f, 1.0f);
        }
        if (metal(m)) {
            for (int c = 0; c < 3; ++c) {
                if (outside(m.baseColorFactor[c], limits.metalF0Min, limits.metalF0Max)) {
                    report(MaterialField::MetalF0, m.baseColorFactor[c], limits.metalF0Min, limits.metalF0Max);
                }
            }
        }
    }
    return issues;
}

void clampMaterial(Material& m, const MaterialLimits& limits)
{
    m.roughnessFactor = clampToRange(m.roughnessFactor, limits.roughnessMin, limits.roughnessMax, limits.roughnessMax);
    m.metallicFactor = clampToRange(m.metallicFactor, 0.0f, 1.0f, 0.0f);
    if (metal(m)) {
        for (int c = 0; c < 3; ++c) {
            m.baseColorFactor[c] = clampToRange(m.baseColorFactor[c], limits.metalF0Min, limits.metalF0Max, limits.metalF0Max);
        }
    }
}

namespace {

using Json = nlohmann::json;

bool readNumber(const Json& object, std::string_view key, float& out, std::vector<std::string>& problems, std::string_view path)
{
    const auto it = object.find(std::string(key));
    if (it == object.end() || !it->is_number()) {
        problems.push_back(std::format("{}/{}: missing or not a number", path, key));
        return false;
    }
    out = it->get<float>();
    return true;
}

bool readRange(const Json& root, std::string_view key, float& lo, float& hi, std::vector<std::string>& problems)
{
    const auto it = root.find(std::string(key));
    if (it == root.end() || !it->is_object()) {
        problems.push_back(std::format("/{}: missing or not an object", key));
        return false;
    }
    const std::string path = "/" + std::string(key);
    bool ok = readNumber(*it, "min", lo, problems, path);
    ok = readNumber(*it, "max", hi, problems, path) && ok;
    for (const auto& item : it->items()) {
        if (item.key() != "min" && item.key() != "max") {
            problems.push_back(std::format("{}/{}: unknown key", path, item.key()));
            ok = false;
        }
    }
    return ok;
}

}

bool parseMaterialLimits(std::string_view text, MaterialLimits& out, std::vector<std::string>& problems)
{
    const Json root = Json::parse(text.begin(), text.end(), nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        problems.push_back("not a JSON object");
        return false;
    }
    MaterialLimits limits;
    const std::size_t before = problems.size();
    readRange(root, "roughness", limits.roughnessMin, limits.roughnessMax, problems);
    readRange(root, "albedo", limits.albedoMin, limits.albedoMax, problems);
    readRange(root, "metal_f0", limits.metalF0Min, limits.metalF0Max, problems);
    readNumber(root, "dielectric_f0", limits.dielectricF0, problems, "");
    static const std::set<std::string, std::less<>> known = {"roughness", "albedo", "metal_f0", "dielectric_f0"};
    for (const auto& item : root.items()) {
        if (!known.contains(item.key())) {
            problems.push_back(std::format("/{}: unknown key", item.key()));
        }
    }
    if (problems.size() == before && !isValid(limits)) {
        problems.push_back("ranges must be finite, ordered, and roughness and reflectance within 0 to 1");
    }
    if (problems.size() != before) {
        return false;
    }
    out = limits;
    return true;
}

bool loadMaterialLimits(const std::filesystem::path& path, MaterialLimits& out, std::vector<std::string>& problems)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        problems.push_back(std::format("{}: cannot be read", path.string()));
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const std::size_t before = problems.size();
    if (!parseMaterialLimits(text, out, problems)) {
        for (std::size_t i = before; i < problems.size(); ++i) {
            problems[i] = path.string() + ": " + problems[i];
        }
        return false;
    }
    return true;
}

void logMaterialIssues(std::string_view label, const std::vector<MaterialIssue>& issues)
{
    for (const MaterialIssue& issue : issues) {
        logf(LogLevel::Warn, "{}: material {} \"{}\": {} {} is outside {} to {}", label, issue.material, issue.name,
            materialFieldName(issue.field), issue.value, issue.min, issue.max);
    }
}

std::vector<MaterialIssue> clampMaterials(std::vector<Material>& materials, const MaterialLimits& limits)
{
    std::vector<MaterialIssue> issues = findMaterialIssues(materials, limits);
    for (Material& material : materials) {
        clampMaterial(material, limits);
    }
    return issues;
}

const MaterialLimits& activeMaterialLimits()
{
    return g_active;
}

void setActiveMaterialLimits(const MaterialLimits& limits)
{
    g_active = limits;
}

}
