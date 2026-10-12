#pragma once

#include "ichor/world/world_desc.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <vector>

namespace ichor {

// The forged heightmap: 16-bit samples the forge wrote once, read for the
// campaign's lifetime. World x and z run along sample columns and rows.
class Heightfield {
public:
    Heightfield() = default;
    Heightfield(WorldDesc desc, std::vector<std::uint16_t> samples);

    const WorldDesc& desc() const { return desc_; }
    const std::vector<std::uint16_t>& samples() const { return samples_; }
    bool empty() const { return samples_.empty(); }

    float heightAtSample(std::uint32_t sx, std::uint32_t sz) const;
    // Bilinear height at a world position; clamped to the edges.
    float heightAt(double x, double z) const;
    glm::vec3 normalAt(double x, double z) const;

    // The samples of one cell, (samplesPerCellSide)^2, row major, plus the
    // cell's height range. Cells share their edge samples with neighbours.
    struct Cell {
        CellId id;
        std::uint32_t samplesPerSide = 0;
        std::vector<float> heights; // metres
        float minHeight = 0.0f;
        float maxHeight = 0.0f;
    };
    bool cell(CellId id, Cell& out) const;

    CellId cellAt(double x, double z) const;
    double cellOriginX(CellId id) const { return static_cast<double>(id.x) * desc_.cellMetres; }
    double cellOriginZ(CellId id) const { return static_cast<double>(id.z) * desc_.cellMetres; }
    bool contains(CellId id) const;

    // A stable digest of the samples; the forge's determinism test pins it.
    std::uint64_t digest() const;

private:
    WorldDesc desc_;
    std::vector<std::uint16_t> samples_;
};

}
