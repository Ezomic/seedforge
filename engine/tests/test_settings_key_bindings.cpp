#include "ichor/core/controls_overlay.h"
#include "ichor/core/key_bindings.h"
#include "ichor/gfx/null_renderer.h"
#include "ichor/loop/engine.h"
#include "ichor/platform/null_platform.h"
#include "ichor/save/settings.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <format>
#include <set>
#include <string>
#include <vector>

using namespace ichor;

namespace {

// USB HID usages, which are SDL scancodes.
constexpr KeyCode kA = 4;
constexpr KeyCode kJ = 13;
constexpr KeyCode kK = 14;
constexpr KeyCode kU = 24;
constexpr KeyCode kV = 25;
constexpr KeyCode kW = 26;
constexpr KeyCode kF1 = 58;
constexpr KeyCode kEscape = 41;
constexpr KeyCode kLeftArrow = 80;

bool anyLineStartsWith(const std::vector<std::string>& lines, const std::string& text)
{
    return std::ranges::any_of(lines, [&](const std::string& line) { return line.starts_with(text); });
}

std::size_t index(BindAction action)
{
    return static_cast<std::size_t>(action);
}

}

TEST_CASE("the default bindings are the keys the game shipped with")
{
    const KeyBindings defaults;
    CHECK(keyName(defaults.key(BindAction::MoveForward)) == "W");
    CHECK(keyName(defaults.key(BindAction::MoveLeft)) == "A");
    CHECK(keyName(defaults.key(BindAction::MoveBack)) == "S");
    CHECK(keyName(defaults.key(BindAction::MoveRight)) == "D");
    CHECK(keyName(defaults.key(BindAction::Sprint)) == "Shift");
    CHECK(keyName(defaults.key(BindAction::Attack)) == "J");
    CHECK(keyName(defaults.key(BindAction::Heavy)) == "K");
    CHECK(keyName(defaults.key(BindAction::Block)) == "L");
    CHECK(keyName(defaults.key(BindAction::Dodge)) == "Space");
    CHECK(keyName(defaults.key(BindAction::LockOn)) == "E");
    CHECK(keyName(defaults.key(BindAction::Interact)) == "F");
    CHECK(keyName(defaults.key(BindAction::Confirm)) == "Enter");
    CHECK(keyName(defaults.key(BindAction::Cancel)) == "Q");
    CHECK(keyName(defaults.key(BindAction::Inventory)) == "I");
    CHECK(keyName(defaults.key(BindAction::Map)) == "M");
    CHECK(keyName(defaults.key(BindAction::Journal)) == "Tab");
    CHECK(keyName(defaults.key(BindAction::Notes)) == "N");
    CHECK(keyName(defaults.key(BindAction::Character)) == "C");
    CHECK(keyName(defaults.key(BindAction::QuickSave)) == "F5");
    CHECK(keyName(defaults.key(BindAction::QuickLoad)) == "F9");
    CHECK(sanitizedBindings(defaults) == defaults);
}

TEST_CASE("every key name parses back to its key, and unbindable keys have no name")
{
    for (KeyCode key = 1; key < 256; ++key) {
        INFO(key);
        if (bindable(key)) {
            CHECK(parseKeyName(keyName(key)) == key);
        } else {
            CHECK(keyName(key).empty());
        }
    }
    CHECK_FALSE(parseKeyName("").has_value());
    CHECK_FALSE(parseKeyName("Banana").has_value());
    CHECK_FALSE(bindable(kEscape));
    CHECK_FALSE(bindable(kF1));
    CHECK_FALSE(bindable(kLeftArrow));
}

TEST_CASE("a bad or doubled binding falls back to that action's default")
{
    KeyBindings bad;
    bad.keys[index(BindAction::Attack)] = kEscape;
    bad.keys[index(BindAction::Heavy)] = 0;
    bad.keys[index(BindAction::Block)] = 999;
    CHECK(sanitizedBindings(bad) == KeyBindings{});

    KeyBindings doubled;
    doubled.keys[index(BindAction::Heavy)] = kW; // forward owns W already
    const KeyBindings fixed = sanitizedBindings(doubled);
    CHECK(fixed.key(BindAction::MoveForward) == kW);
    CHECK(fixed.key(BindAction::Heavy) == kK);

    KeyBindings tangled;
    tangled.keys[index(BindAction::MoveForward)] = kK; // takes Heavy's default, so Heavy cannot go home
    tangled.keys[index(BindAction::Heavy)] = kK;
    CHECK(sanitizedBindings(tangled) == KeyBindings{});
}

TEST_CASE("rebinding swaps with the action that held the key and never leaves a conflict")
{
    const KeyBindings once = rebound(KeyBindings{}, BindAction::Attack, kV);
    CHECK(once.key(BindAction::Attack) == kV);
    CHECK(sanitizedBindings(once) == once);

    const KeyBindings swapped = rebound(KeyBindings{}, BindAction::Attack, kK); // Heavy had K
    CHECK(swapped.key(BindAction::Attack) == kK);
    CHECK(swapped.key(BindAction::Heavy) == kJ);
    const std::set<KeyCode> distinct(swapped.keys.begin(), swapped.keys.end());
    CHECK(distinct.size() == kBindActionCount);

    CHECK(rebound(KeyBindings{}, BindAction::Attack, kEscape) == KeyBindings{});
    CHECK(rebound(KeyBindings{}, BindAction::Attack, kJ) == KeyBindings{});
}

TEST_CASE("the bindings survive the settings file, and a bad value reads as the default")
{
    Settings settings;
    settings.bindings = rebound(rebound(KeyBindings{}, BindAction::Dodge, kV), BindAction::MoveForward, kA);
    const Settings read = parseSettings(serializeSettings(settings));
    CHECK(read == settings);
    CHECK(read.bindings.key(BindAction::Dodge) == kV);
    CHECK(read.bindings.key(BindAction::MoveForward) == kA);
    CHECK(read.bindings.key(BindAction::MoveLeft) == kW); // forward took A, so left went to W

    const std::string text = serializeSettings(Settings{});
    CHECK(parseSettings(text).bindings == KeyBindings{});

    const auto withLine = [&](const std::string& now) {
        const std::string was = "bind.dodge=Space";
        std::string edited = text;
        const std::size_t at = edited.find(was);
        REQUIRE(at != std::string::npos);
        edited.replace(at, was.size(), now);
        return parseSettings(edited);
    };
    CHECK(withLine("bind.dodge=Banana").bindings == KeyBindings{});
    CHECK(withLine("bind.dodge=Esc").bindings == KeyBindings{});
    CHECK(withLine("bind.dodge=").bindings == KeyBindings{});
    CHECK(withLine("bind.dodge=J").bindings == KeyBindings{}); // doubled with Attack: the later action goes home
    CHECK(withLine("bind.dodge=V").bindings.key(BindAction::Dodge) == kV);
    CHECK(withLine("bind.dodge=V").bindings.key(BindAction::Attack) == kJ); // one bad line costs nothing else
}

TEST_CASE("a settings file from before the bindings reads as the default keys")
{
    const std::string old = "ichor-settings 1\nfov=60\nmouseSensitivity=1\npromptsSeen=0\nend\n";
    const Settings read = parseSettings(old);
    CHECK(read.bindings == KeyBindings{});
    CHECK(read.fovDegrees == 60.0f);
}

TEST_CASE("the controls card lists the live bindings, so a rebound key moves on the card")
{
    ControlsOverlay card;
    card.open = true;
    CHECK(anyLineStartsWith(controlsOverlayLines(card), "W A S D"));
    CHECK(anyLineStartsWith(controlsOverlayLines(card), "J, Left click"));
    CHECK(anyLineStartsWith(controlsOverlayLines(card), "Space"));

    card.bindings = rebound(rebound(KeyBindings{}, BindAction::Dodge, kV), BindAction::MoveForward, kU);
    const std::vector<std::string> lines = controlsOverlayLines(card);
    CHECK(anyLineStartsWith(lines, "V "));
    CHECK(anyLineStartsWith(lines, "U A S D"));
    CHECK_FALSE(anyLineStartsWith(lines, "Space"));
    CHECK_FALSE(anyLineStartsWith(lines, "W A S D"));
}

TEST_CASE("every rebindable action has its live key on the card, whatever the bindings are")
{
    KeyBindings shuffled;
    for (std::size_t i = 0; i < kBindActionCount; ++i) {
        shuffled = rebound(shuffled, static_cast<BindAction>(i), static_cast<KeyCode>(kA + (kBindActionCount - 1 - i)));
    }
    REQUIRE(sanitizedBindings(shuffled) == shuffled);

    ControlsOverlay card;
    card.open = true;
    card.bindings = shuffled;
    std::string joined;
    for (const std::string& line : controlsOverlayLines(card)) {
        joined += line + "\n";
    }
    // The lines that name one action by itself: a key reads at the start of its line.
    const std::vector<std::pair<BindAction, std::string>> lines = {
        {BindAction::LockOn, "lock on"}, {BindAction::Cancel, "cancel"}, {BindAction::Inventory, "inventory"}, {BindAction::Map, "map"},
        {BindAction::Journal, "journal"}, {BindAction::Notes, "notes"}, {BindAction::Character, "character"}, {BindAction::Dodge, "dodge"},
    };
    for (const auto& [action, label] : lines) {
        INFO(label);
        CHECK(joined.find(std::format("{:<15}{}", keyName(shuffled.key(action)), label)) != std::string::npos);
    }
    CHECK(joined.find(keyName(shuffled.key(BindAction::QuickSave)) + " / " + keyName(shuffled.key(BindAction::QuickLoad))) != std::string::npos);
    CHECK(joined.find("(" + keyName(shuffled.key(BindAction::Sprint)) + " sprints)") != std::string::npos);
}

TEST_CASE("the engine hands the live bindings to the card it draws")
{
    NullPlatform platform;
    NullRenderer renderer;
    REQUIRE(platform.init({}));
    REQUIRE(renderer.init());
    Engine engine(platform, renderer, {});
    engine.setOverlayMode(OverlayMode::Off);
    engine.setKeyBindings(rebound(KeyBindings{}, BindAction::Dodge, kV));
    REQUIRE(engine.controlsOverlay().bindings.key(BindAction::Dodge) == kV);
    ControlsOverlay open = engine.controlsOverlay();
    open.open = true;
    CHECK(anyLineStartsWith(controlsOverlayLines(open), "V "));
    CHECK_FALSE(anyLineStartsWith(controlsOverlayLines(open), "Space"));
}
