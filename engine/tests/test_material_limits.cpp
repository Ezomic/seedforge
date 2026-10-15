#include "ichor/assets/material_limits.h"
#include "ichor/world/palette.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <limits>

using namespace ichor;

namespace {

MaterialLimits narrow()
{
    MaterialLimits limits;
    limits.roughnessMin = 0.3f;
    limits.roughnessMax = 0.9f;
    limits.metalF0Min = 0.4f;
    limits.metalF0Max = 0.9f;
    return limits;
}

Material dielectric(float roughness)
{
    Material m;
    m.name = "stone";
    m.metallicFactor = 0.0f;
    m.roughnessFactor = roughness;
    return m;
}

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

}

TEST_CASE("roughness below, inside and above the range is held to it")
{
    const MaterialLimits limits = narrow();
    Material below = dielectric(0.0f);
    Material inside = dielectric(0.5f);
    Material above = dielectric(1.0f);
    clampMaterial(below, limits);
    clampMaterial(inside, limits);
    clampMaterial(above, limits);
    REQUIRE(below.roughnessFactor == 0.3f);
    REQUIRE(inside.roughnessFactor == 0.5f);
    REQUIRE(above.roughnessFactor == 0.9f);
}

TEST_CASE("NaN, infinite and negative inputs end up inside the range")
{
    const MaterialLimits limits = narrow();
    for (const float bad : {kNaN, -1.0f, -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), 1e30f}) {
        Material m = dielectric(bad);
        m.metallicFactor = bad;
        clampMaterial(m, limits);
        REQUIRE(m.roughnessFactor >= limits.roughnessMin);
        REQUIRE(m.roughnessFactor <= limits.roughnessMax);
        REQUIRE(m.metallicFactor >= 0.0f);
        REQUIRE(m.metallicFactor <= 1.0f);
    }
    Material nan = dielectric(kNaN);
    clampMaterial(nan, limits);
    REQUIRE(nan.roughnessFactor == limits.roughnessMax);
}

TEST_CASE("a metal's reflectance is held per channel and a non-metal's colour is left alone")
{
    const MaterialLimits limits = narrow();
    Material metal;
    metal.metallicFactor = 1.0f;
    metal.baseColorFactor = {0.05f, 0.6f, kNaN, 1.0f};
    clampMaterial(metal, limits);
    REQUIRE(metal.baseColorFactor.r == 0.4f);
    REQUIRE(metal.baseColorFactor.g == 0.6f);
    REQUIRE(metal.baseColorFactor.b == 0.9f);
    REQUIRE(metal.baseColorFactor.a == 1.0f);

    Material paint;
    paint.metallicFactor = 0.0f;
    paint.baseColorFactor = {0.05f, 0.05f, 0.05f, 1.0f};
    clampMaterial(paint, limits);
    REQUIRE(paint.baseColorFactor.r == 0.05f);
}

TEST_CASE("clamping twice changes nothing more than clamping once")
{
    const MaterialLimits limits = narrow();
    for (const float bad : {kNaN, -2.0f, 0.0f, 0.5f, 1.0f, 7.0f}) {
        Material once = dielectric(bad);
        once.metallicFactor = 1.0f;
        once.baseColorFactor = {bad, bad, bad, 1.0f};
        clampMaterial(once, limits);
        Material twice = once;
        clampMaterial(twice, limits);
        REQUIRE(twice.roughnessFactor == once.roughnessFactor);
        REQUIRE(twice.metallicFactor == once.metallicFactor);
        REQUIRE(twice.baseColorFactor == once.baseColorFactor);
        REQUIRE(findMaterialIssues({twice}, limits).empty());
    }
}

TEST_CASE("the validator lists every material outside the range, as it stands before the clamp")
{
    const MaterialLimits limits = narrow();
    std::vector<Material> materials{dielectric(0.5f), dielectric(0.1f), dielectric(kNaN), dielectric(0.95f)};
    materials[1].name = "too_shiny";
    materials[3].name = "too_matte";
    const auto issues = findMaterialIssues(materials, limits);
    REQUIRE(issues.size() == 3);
    REQUIRE(issues[0].material == 1);
    REQUIRE(issues[0].name == "too_shiny");
    REQUIRE(issues[0].field == MaterialField::Roughness);
    REQUIRE(issues[0].value == 0.1f);
    REQUIRE(issues[0].min == 0.3f);
    REQUIRE(issues[0].max == 0.9f);
    REQUIRE(issues[1].material == 2);
    REQUIRE(std::isnan(issues[1].value));
    REQUIRE(issues[2].material == 3);

    for (Material& m : materials) {
        clampMaterial(m, limits);
    }
    REQUIRE(findMaterialIssues(materials, limits).empty());
}

TEST_CASE("the validator reports each metal channel outside the reflectance range")
{
    const MaterialLimits limits = narrow();
    Material metal;
    metal.name = "dull_iron";
    metal.metallicFactor = 1.0f;
    metal.roughnessFactor = 0.5f;
    metal.baseColorFactor = {0.1f, 0.5f, 0.2f, 1.0f};
    const auto issues = findMaterialIssues({metal}, limits);
    REQUIRE(issues.size() == 2);
    REQUIRE(issues[0].field == MaterialField::MetalF0);
    REQUIRE(issues[0].value == 0.1f);
}

TEST_CASE("the default limits change nothing for an ordinary material")
{
    Material m = dielectric(0.5f);
    m.baseColorFactor = {0.8f, 0.2f, 0.1f, 1.0f};
    const Material before = m;
    clampMaterial(m, MaterialLimits{});
    REQUIRE(m.roughnessFactor == before.roughnessFactor);
    REQUIRE(m.baseColorFactor == before.baseColorFactor);
}

TEST_CASE("limits parse from JSON and a bad file names every problem")
{
    MaterialLimits out;
    std::vector<std::string> problems;
    REQUIRE(parseMaterialLimits(R"({"roughness":{"min":0.2,"max":0.8},"albedo":{"min":0.1,"max":0.9},"metal_f0":{"min":0.5,"max":0.9},"dielectric_f0":0.05})", out, problems));
    REQUIRE(problems.empty());
    REQUIRE(out.roughnessMin == 0.2f);
    REQUIRE(out.metalF0Max == 0.9f);
    REQUIRE(out.dielectricF0 == 0.05f);

    const MaterialLimits kept = out;
    REQUIRE_FALSE(parseMaterialLimits(R"({"roughness":{"min":0.9,"max":0.2},"albedo":{"min":0.1},"extra":1})", out, problems));
    REQUIRE(problems.size() >= 3);
    REQUIRE(out.roughnessMin == kept.roughnessMin);

    problems.clear();
    REQUIRE_FALSE(parseMaterialLimits(R"({"roughness":{"min":0.9,"max":0.2},"albedo":{"min":0.1,"max":0.9},"metal_f0":{"min":0.5,"max":0.9},"dielectric_f0":0.05})", out, problems));
    REQUIRE(problems.size() == 1);
    problems.clear();
    REQUIRE_FALSE(parseMaterialLimits("not json", out, problems));
}

TEST_CASE("the shipped limits file parses and matches the default palette")
{
    MaterialLimits limits;
    std::vector<std::string> problems;
    REQUIRE(loadMaterialLimits(std::filesystem::path(ICHOR_REPO_DATA) / "materials" / "response.json", limits, problems));
    REQUIRE(problems.empty());
    const MaterialLimits palette = defaultPalette().response;
    REQUIRE(limits.roughnessMin == palette.roughnessMin);
    REQUIRE(limits.roughnessMax == palette.roughnessMax);
    REQUIRE(limits.albedoMin == palette.albedoMin);
    REQUIRE(limits.albedoMax == palette.albedoMax);
    REQUIRE(limits.metalF0Min == palette.metalF0Min);
    REQUIRE(limits.metalF0Max == palette.metalF0Max);
    REQUIRE(limits.dielectricF0 == palette.dielectricF0);
}

TEST_CASE("the active limits are what a renderer clamps to")
{
    const MaterialLimits before = activeMaterialLimits();
    setActiveMaterialLimits(narrow());
    REQUIRE(activeMaterialLimits().roughnessMax == 0.9f);
    setActiveMaterialLimits(before);
}
