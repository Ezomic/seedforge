#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ichor {

// A physical key as its USB HID usage, which is also the SDL scancode, so the platform indexes its key state with it.
// 0 is no key.
using KeyCode = std::uint16_t;

// The actions whose keys the player can change. Fixed keys (Esc, F1 to F4, F8, the arrows, the right Shift and the
// keypad Enter) are not in the list. Presentation and input only: bindings never reach the simulation or a hash.
enum class BindAction : std::uint8_t {
    MoveForward,
    MoveBack,
    MoveLeft,
    MoveRight,
    Sprint,
    Attack,
    Heavy,
    Block,
    Dodge,
    LockOn,
    Interact,
    Confirm,
    Cancel,
    Inventory,
    Map,
    Journal,
    Notes,
    Character,
    QuickSave,
    QuickLoad,
};

constexpr std::size_t kBindActionCount = 20;

struct KeyBindings {
    // The keys the game shipped with.
    std::array<KeyCode, kBindActionCount> keys = {26, 22, 4, 7, 225, 13, 14, 15, 44, 8, 9, 40, 20, 12, 16, 43, 17, 6, 62, 66};

    KeyCode key(BindAction action) const { return keys[static_cast<std::size_t>(action)]; }

    bool operator==(const KeyBindings&) const = default;
};

// The text of the action in the controls screen and the settings file ("MoveForward" is "moveForward" there).
std::string_view bindActionLabel(BindAction action);
std::string_view bindActionKey(BindAction action);

// "W", "Space", "F5"; empty for a key that cannot be bound.
std::string keyName(KeyCode key);
std::optional<KeyCode> parseKeyName(std::string_view name);

// Letters, digits, Space, Tab, Enter, left Shift and Ctrl, and the function keys the game does not use itself.
bool bindable(KeyCode key);

// Every action on a bindable key, no two on the same one. An action whose key is bad or already taken by an earlier
// action goes back to its default; if that default is taken too, the whole set goes back to the defaults.
KeyBindings sanitizedBindings(KeyBindings bindings);

// The bindings after the player gives the action the key: the action that had the key gets the old one, so the set
// stays conflict free. A key that cannot be bound changes nothing.
KeyBindings rebound(const KeyBindings& bindings, BindAction action, KeyCode key);

// How the key lines read on the controls card and in the first-minute prompts, from the live bindings.
std::string moveKeysText(const KeyBindings& bindings);
std::string attackKeysText(const KeyBindings& bindings);
std::string blockKeysText(const KeyBindings& bindings);
std::string dodgeKeysText(const KeyBindings& bindings);

}
