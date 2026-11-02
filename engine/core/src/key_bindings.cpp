#include "ichor/core/key_bindings.h"

#include <algorithm>
#include <format>

namespace ichor {

namespace {

constexpr KeyCode kSpace = 44;
constexpr KeyCode kTab = 43;
constexpr KeyCode kEnter = 40;
constexpr KeyCode kLeftCtrl = 224;
constexpr KeyCode kLeftShift = 225;
constexpr KeyCode kFirstLetter = 4;
constexpr KeyCode kFirstDigit = 30;
constexpr KeyCode kZero = 39;
constexpr KeyCode kF1 = 58;

struct ActionInfo {
    std::string_view label;
    std::string_view key;
};

constexpr std::array<ActionInfo, kBindActionCount> kActions = {{
    {"Move forward", "moveForward"},
    {"Move back", "moveBack"},
    {"Move left", "moveLeft"},
    {"Move right", "moveRight"},
    {"Sprint", "sprint"},
    {"Attack", "attack"},
    {"Heavy attack", "heavy"},
    {"Block", "block"},
    {"Dodge", "dodge"},
    {"Lock on", "lockOn"},
    {"Interact", "interact"},
    {"Confirm", "confirm"},
    {"Cancel", "cancel"},
    {"Inventory", "inventory"},
    {"Map", "map"},
    {"Journal", "journal"},
    {"Notes", "notes"},
    {"Character", "character"},
    {"Quick save", "quickSave"},
    {"Quick load", "quickLoad"},
}};

bool isFunctionKey(KeyCode key)
{
    return key >= kF1 && key < kF1 + 12;
}

}

std::string_view bindActionLabel(BindAction action)
{
    return kActions[static_cast<std::size_t>(action)].label;
}

std::string_view bindActionKey(BindAction action)
{
    return kActions[static_cast<std::size_t>(action)].key;
}

std::string keyName(KeyCode key)
{
    if (!bindable(key)) {
        return {};
    }
    if (key >= kFirstLetter && key < kFirstLetter + 26) {
        return std::string(1, static_cast<char>('A' + (key - kFirstLetter)));
    }
    if (key >= kFirstDigit && key < kFirstDigit + 9) {
        return std::string(1, static_cast<char>('1' + (key - kFirstDigit)));
    }
    if (key == kZero) {
        return "0";
    }
    if (isFunctionKey(key)) {
        return std::format("F{}", key - kF1 + 1);
    }
    switch (key) {
    case kSpace: return "Space";
    case kTab: return "Tab";
    case kEnter: return "Enter";
    case kLeftCtrl: return "Ctrl";
    case kLeftShift: return "Shift";
    default: return {};
    }
}

std::optional<KeyCode> parseKeyName(std::string_view name)
{
    for (KeyCode key = 1; !name.empty() && key < 256; ++key) {
        if (keyName(key) == name) {
            return key;
        }
    }
    return std::nullopt;
}

bool bindable(KeyCode key)
{
    if ((key >= kFirstLetter && key < kFirstLetter + 26) || (key >= kFirstDigit && key <= kZero)) {
        return true;
    }
    if (isFunctionKey(key)) {
        const int number = key - kF1 + 1;
        return number == 5 || number == 6 || number == 7 || number >= 9;
    }
    return key == kSpace || key == kTab || key == kEnter || key == kLeftCtrl || key == kLeftShift;
}

KeyBindings sanitizedBindings(KeyBindings bindings)
{
    const KeyBindings defaults;
    std::array<bool, 256> taken{};
    for (std::size_t i = 0; i < kBindActionCount; ++i) {
        KeyCode& key = bindings.keys[i];
        if (!bindable(key) || taken[key]) {
            key = defaults.keys[i];
            if (taken[key]) {
                return defaults;
            }
        }
        taken[key] = true;
    }
    return bindings;
}

KeyBindings rebound(const KeyBindings& bindings, BindAction action, KeyCode key)
{
    if (!bindable(key)) {
        return bindings;
    }
    KeyBindings result = bindings;
    const std::size_t index = static_cast<std::size_t>(action);
    const KeyCode old = result.keys[index];
    for (KeyCode& other : result.keys) {
        if (other == key) {
            other = old;
        }
    }
    result.keys[index] = key;
    return result;
}

std::string moveKeysText(const KeyBindings& bindings)
{
    return std::format("{} {} {} {}", keyName(bindings.key(BindAction::MoveForward)), keyName(bindings.key(BindAction::MoveLeft)), keyName(bindings.key(BindAction::MoveBack)),
        keyName(bindings.key(BindAction::MoveRight)));
}

std::string attackKeysText(const KeyBindings& bindings)
{
    return std::format("{}, Left click", keyName(bindings.key(BindAction::Attack)));
}

std::string blockKeysText(const KeyBindings& bindings)
{
    return std::format("{}, Middle btn", keyName(bindings.key(BindAction::Block)));
}

std::string dodgeKeysText(const KeyBindings& bindings)
{
    return keyName(bindings.key(BindAction::Dodge));
}

}
