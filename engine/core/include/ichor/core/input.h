#pragma once

#include "ichor/core/reflect.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace ichor {

// One tick of player input. The simulation never reads a device; it reads
// these, which is what makes live play and replay the same code path.
struct InputFrame {
    enum Button : std::uint32_t {
        Confirm = 1u << 0,
        Cancel = 1u << 1,
        Sprint = 1u << 2,
        Attack = 1u << 3,
        Heavy = 1u << 4,
        Block = 1u << 5,
        Dodge = 1u << 6,
        QuickSave = 1u << 7,
        QuickLoad = 1u << 8,
        Journal = 1u << 9,
        Menu = 1u << 10,
        Map = 1u << 11,
        Notes = 1u << 12,
    };
    std::uint32_t buttons = 0;
    float moveX = 0.0f; // right positive
    float moveY = 0.0f; // forward positive

    bool operator==(const InputFrame&) const = default;
};

template <>
struct Reflect<InputFrame> {
    static constexpr auto fields()
    {
        return std::make_tuple(field("buttons", &InputFrame::buttons), field("moveX", &InputFrame::moveX), field("moveY", &InputFrame::moveY));
    }
};

struct InputStream {
    std::vector<InputFrame> frames;

    bool save(const std::filesystem::path& path) const;
    bool load(const std::filesystem::path& path);
};

}
