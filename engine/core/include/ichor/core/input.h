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
        LockOn = 1u << 13, // toggles the lock-on camera and auto-facing; acts on the press
        Character = 1u << 14,
        Interact = 1u << 15,  // picks up what lies within reach (F); acts on the press
        Inventory = 1u << 16, // opens and closes the inventory screen
    };
    std::uint32_t buttons = 0;
    float moveX = 0.0f; // right positive
    float moveY = 0.0f; // forward positive
    float lookX = 0.0f; // turn rate as a fraction of the game's look speed, right positive; readers must sanitise it

    bool operator==(const InputFrame&) const = default;
};

template <>
struct Reflect<InputFrame> {
    static constexpr auto fields()
    {
        return std::make_tuple(field("buttons", &InputFrame::buttons), field("moveX", &InputFrame::moveX), field("moveY", &InputFrame::moveY), field("lookX", &InputFrame::lookX));
    }
};

struct InputStream {
    std::vector<InputFrame> frames;
    // Set by load() for a version 1 file, which predates look input and the camera-relative controls.
    bool legacyControls = false;

    bool save(const std::filesystem::path& path) const;
    bool load(const std::filesystem::path& path);
};

// The last ticks of input, oldest first, kept whatever the run records (an F8 feedback note's replay tail). Capacity 0 keeps none.
class InputTail {
public:
    explicit InputTail(std::size_t capacity = 0)
        : capacity_(capacity)
    {
    }

    void push(const InputFrame& frame);
    InputStream stream() const;

private:
    std::size_t capacity_ = 0;
    std::size_t next_ = 0;
    std::vector<InputFrame> frames_;
};

}
