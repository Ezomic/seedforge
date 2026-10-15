#pragma once

#include "ichor/assets/bundle.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// The one place that says how far any surface may stray, whatever its source. The shader holds
// every pixel to these ranges; the same roughness and metal reflectance numbers hold every material
// factor when a bundle is uploaded. Albedo brightness is held by the shader alone, since a texture
// decides it.
// The defaults change nothing.
struct MaterialLimits {
    float roughnessMin = 0.04f;
    float roughnessMax = 1.0f;
    float albedoMin = 0.0f;    // brightness of non-metal albedo
    float albedoMax = 1000.0f; // effectively open
    float metalF0Min = 0.0f;   // reflectance at normal incidence of a metal, per channel
    float metalF0Max = 1.0f;
    float dielectricF0 = 0.04f; // reflectance of every non-metal
};

// True when every range is finite and ordered, so a clamp against it is well defined.
bool isValid(const MaterialLimits& limits);

// Clamps into [lo, hi]; NaN becomes `fallback` (itself clamped), so no input escapes the range.
float clampToRange(float value, float lo, float hi, float fallback);

enum class MaterialField { Roughness, Metallic, MetalF0 };

std::string_view materialFieldName(MaterialField field);

struct MaterialIssue {
    std::uint32_t material = 0;
    std::string name;
    MaterialField field = MaterialField::Roughness;
    float value = 0.0f; // for MetalF0 the offending colour channel
    float min = 0.0f;
    float max = 0.0f;
};

// Every material value outside the limits, as it stands, before any clamp. NaN counts as outside.
// Only the factors are visible here; a texture can still push a pixel out, which the shader holds.
std::vector<MaterialIssue> findMaterialIssues(const std::vector<Material>& materials, const MaterialLimits& limits);

// Brings one material inside the limits. Idempotent.
void clampMaterial(Material& material, const MaterialLimits& limits);

// Reads limits from JSON: {"roughness": {"min", "max"}, "albedo": {"min", "max"},
// "metal_f0": {"min", "max"}, "dielectric_f0"}. Every key is required and no other is allowed.
// Returns false and fills `problems` on anything wrong; `out` is only written on success.
bool parseMaterialLimits(std::string_view text, MaterialLimits& out, std::vector<std::string>& problems);
bool loadMaterialLimits(const std::filesystem::path& path, MaterialLimits& out, std::vector<std::string>& problems);

// Logs one warning per issue, labelled with where the materials came from, so authors can fix the source.
void logMaterialIssues(std::string_view label, const std::vector<MaterialIssue>& issues);

// Clamps every material of a bundle that is about to be drawn and returns what was outside the
// limits beforehand, so the caller can report it. This is the step the renderer runs on upload.
std::vector<MaterialIssue> clampMaterials(std::vector<Material>& materials, const MaterialLimits& limits);

// The limits materials are clamped to when they are created or loaded; set once at startup.
const MaterialLimits& activeMaterialLimits();
void setActiveMaterialLimits(const MaterialLimits& limits);

}
