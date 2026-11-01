#include "ichor/save/settings.h"
#include "test_temp.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using namespace ichor;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

class TempFile {
public:
    TempFile()
        : path_(test::tempPath("ichor_settings_test_" + std::to_string(counter_++)) / "settings.cfg")
    {
        std::filesystem::remove_all(path_.parent_path());
    }
    ~TempFile() { std::filesystem::remove_all(path_.parent_path()); }
    const std::filesystem::path& path() const { return path_; }
    void write(const std::string& bytes) const
    {
        std::filesystem::create_directories(path_.parent_path());
        std::ofstream(path_, std::ios::binary | std::ios::trunc) << bytes;
    }

private:
    static inline int counter_ = 0;
    std::filesystem::path path_;
};

const Settings kDefaults;

}

TEST_CASE("settings clamp into their ranges and bad numbers become the default")
{
    REQUIRE(Settings{70.0f, 2.0f}.clamped() == Settings{70.0f, 2.0f});
    REQUIRE(Settings{-10.0f, -1.0f}.clamped() == Settings{Settings::kMinFovDegrees, Settings::kMinMouseSensitivity});
    REQUIRE(Settings{0.0f, 0.0f}.clamped() == Settings{Settings::kMinFovDegrees, Settings::kMinMouseSensitivity});
    REQUIRE(Settings{1e30f, 1e30f}.clamped() == Settings{Settings::kMaxFovDegrees, Settings::kMaxMouseSensitivity});
    REQUIRE(Settings{kInf, -kInf}.clamped() == kDefaults);
    REQUIRE(Settings{kNaN, kNaN}.clamped() == kDefaults);
    REQUIRE(Settings{kNaN, 3.0f}.clamped() == Settings{Settings::kDefaultFovDegrees, 3.0f});
}

TEST_CASE("settings survive a round trip through text, and the text of bad settings is clamped")
{
    const Settings custom{87.5f, 0.35f};
    REQUIRE(parseSettings(serializeSettings(custom)) == custom);
    REQUIRE(parseSettings(serializeSettings(kDefaults)) == kDefaults);
    REQUIRE(parseSettings(serializeSettings(Settings{kNaN, 1e9f})) == Settings{Settings::kDefaultFovDegrees, Settings::kMaxMouseSensitivity});
}

TEST_CASE("a file that is empty, cut short, of another version or not text gives the defaults")
{
    REQUIRE(parseSettings("") == kDefaults);
    REQUIRE(parseSettings("\n\n") == kDefaults);
    REQUIRE(parseSettings("ichor-settings 2\nfov=90\nmouseSensitivity=2\nend\n") == kDefaults);
    REQUIRE(parseSettings("ichor-settings\nfov=90\nend\n") == kDefaults);
    REQUIRE(parseSettings("fov=90\nmouseSensitivity=2\nend\n") == kDefaults);
    REQUIRE(parseSettings(std::string("\xff\xfe\x00\x80\xc3\x28", 6)) == kDefaults);

    const std::string good = serializeSettings(Settings{90.0f, 2.0f});
    for (std::size_t length = 0; length + 1 < good.size(); ++length) {
        INFO("cut at " << length);
        const Settings parsed = parseSettings(std::string_view(good).substr(0, length));
        REQUIRE(parsed == kDefaults);
    }
    REQUIRE(parseSettings(good) == Settings{90.0f, 2.0f});
    REQUIRE(parseSettings(std::string(good, 0, good.size() - 1)) == Settings{90.0f, 2.0f}); // only the last newline missing
}

TEST_CASE("one bad value costs only that field, and unknown keys are ignored")
{
    const std::string head = "ichor-settings 1\n";
    REQUIRE(parseSettings(head + "fov=abc\nmouseSensitivity=2\nend\n") == Settings{Settings::kDefaultFovDegrees, 2.0f});
    REQUIRE(parseSettings(head + "fov=90deg\nmouseSensitivity=2\nend\n") == Settings{Settings::kDefaultFovDegrees, 2.0f});
    REQUIRE(parseSettings(head + "fov=nan\nmouseSensitivity=inf\nend\n") == kDefaults);
    REQUIRE(parseSettings(head + "fov=-inf\nend\n") == kDefaults);
    REQUIRE(parseSettings(head + "fov=\nmouseSensitivity=3\nend\n") == Settings{Settings::kDefaultFovDegrees, 3.0f});
    REQUIRE(parseSettings(head + "fov=-5\nmouseSensitivity=0\nend\n") == Settings{Settings::kMinFovDegrees, Settings::kMinMouseSensitivity});
    REQUIRE(parseSettings(head + "fov=1e40\nmouseSensitivity=99\nend\n") == Settings{Settings::kMaxFovDegrees, Settings::kMaxMouseSensitivity});
    REQUIRE(parseSettings(head + "fov=-1e40\nmouseSensitivity=-1e999\nend\n") == Settings{Settings::kMinFovDegrees, Settings::kMinMouseSensitivity});
    REQUIRE(parseSettings(head + "volume=11\nfov=80\nfuture key\nend\n") == Settings{80.0f, Settings::kDefaultMouseSensitivity});
    REQUIRE(parseSettings(head + "fov=70\r\nmouseSensitivity=2\r\nend\r\n") == Settings{70.0f, 2.0f});
    REQUIRE(parseSettings(head + "fov=70\nfov=80\nend\n") == Settings{80.0f, Settings::kDefaultMouseSensitivity});
    REQUIRE(parseSettings(head + "fov=70\nend\nfov=100\n") == Settings{70.0f, Settings::kDefaultMouseSensitivity});
}

TEST_CASE("an oversized file is refused rather than read")
{
    const std::string padding(8192, '#');
    REQUIRE(parseSettings("ichor-settings 1\nfov=90\n" + padding + "\nend\n") == kDefaults);
}

TEST_CASE("settings files load and save, and a missing or damaged one is the defaults")
{
    TempFile file;
    REQUIRE(loadSettings(file.path()) == kDefaults); // no file yet

    const Settings custom{75.0f, 1.5f};
    REQUIRE(saveSettings(file.path(), custom)); // creates the folder too
    REQUIRE(loadSettings(file.path()) == custom);
    REQUIRE_FALSE(std::filesystem::exists(file.path().string() + ".tmp"));

    REQUIRE(saveSettings(file.path(), Settings{100.0f, 0.5f}));
    REQUIRE(loadSettings(file.path()) == Settings{100.0f, 0.5f});

    file.write("garbage");
    REQUIRE(loadSettings(file.path()) == kDefaults);
    file.write(std::string());
    REQUIRE(loadSettings(file.path()) == kDefaults);
    file.write(std::string(100000, 'x'));
    REQUIRE(loadSettings(file.path()) == kDefaults);

    std::filesystem::remove(file.path());
    std::filesystem::create_directories(file.path()); // a directory where the file should be
    REQUIRE(loadSettings(file.path()) == kDefaults);
    REQUIRE_FALSE(saveSettings(file.path(), custom));
}

TEST_CASE("the --mouse-sens value overrides the stored sensitivity for the run, inside the same range")
{
    const Settings stored{80.0f, 2.0f};
    REQUIRE(effectiveMouseSensitivity(stored, std::nullopt) == 2.0f);
    REQUIRE(effectiveMouseSensitivity(kDefaults, std::nullopt) == Settings::kDefaultMouseSensitivity);
    REQUIRE(effectiveMouseSensitivity(stored, 0.5f) == 0.5f);
    REQUIRE(effectiveMouseSensitivity(stored, 0.0f) == Settings::kMinMouseSensitivity);
    REQUIRE(effectiveMouseSensitivity(stored, -3.0f) == Settings::kMinMouseSensitivity);
    REQUIRE(effectiveMouseSensitivity(stored, 50.0f) == Settings::kMaxMouseSensitivity);
    REQUIRE(effectiveMouseSensitivity(stored, kNaN) == 2.0f); // a flag that is not a number leaves the stored value
    REQUIRE(effectiveMouseSensitivity(Settings{80.0f, kNaN}, std::nullopt) == Settings::kDefaultMouseSensitivity);
}

TEST_CASE("a sensitivity step starts from what the player looks with and keeps the other settings")
{
    const Settings stored{80.0f, 1.0f};
    REQUIRE(stepMouseSensitivity(stored, 1.0f, 1) == Settings{80.0f, 1.1f});
    REQUIRE(stepMouseSensitivity(stored, 1.0f, -3) == Settings{80.0f, 0.7f});
    REQUIRE(stepMouseSensitivity(stored, 2.5f, 1) == Settings{80.0f, 2.6f}); // from the --mouse-sens value, not the stored one
    REQUIRE(stepMouseSensitivity(stored, 0.1f, -1) == Settings{80.0f, Settings::kMinMouseSensitivity});
    REQUIRE(stepMouseSensitivity(stored, 4.95f, 2) == Settings{80.0f, Settings::kMaxMouseSensitivity});
    REQUIRE(stepMouseSensitivity(stored, 1.0f, 0) == stored);

    // Many small steps land on tenths, so the file and the menu never show 1.0000001.
    Settings walked = stored;
    for (int i = 0; i < 37; ++i) {
        walked = stepMouseSensitivity(walked, walked.mouseSensitivity, 1);
    }
    REQUIRE(walked.mouseSensitivity == 4.7f);
    REQUIRE(serializeSettings(walked).find("mouseSensitivity=4.7\n") != std::string::npos);
}

TEST_CASE("the display mode, resolution and vsync survive the file, and a bad value reads as the default")
{
    Settings display;
    display.displayMode = DisplayMode::Fullscreen;
    display.resolution = {1920, 1080};
    display.vsync = false;
    REQUIRE(parseSettings(serializeSettings(display)) == display);
    const std::string text = serializeSettings(display);
    REQUIRE(text.find("display=fullscreen\n") != std::string::npos);
    REQUIRE(text.find("resolution=1920x1080\n") != std::string::npos);
    REQUIRE(text.find("vsync=0\n") != std::string::npos);
    REQUIRE(serializeSettings(kDefaults).find("display=windowed\nresolution=0x0\nvsync=1\n") != std::string::npos);

    // A file from before these keys, and one that leaves them out, is the stand-in default: windowed, the window as made, vsync on.
    const std::string head = "ichor-settings 1\n";
    REQUIRE(parseSettings(head + "fov=80\nmouseSensitivity=2\nend\n") == Settings{80.0f, 2.0f});
    REQUIRE(kDefaults.displayMode == DisplayMode::Windowed);
    REQUIRE_FALSE(kDefaults.resolution.set());
    REQUIRE(kDefaults.vsync);

    for (const char* bad : {"", "Fullscreen", "borderless", "1"}) {
        INFO(bad);
        REQUIRE(parseSettings(head + "display=" + bad + "\nend\n").displayMode == DisplayMode::Windowed);
    }
    REQUIRE(parseSettings(head + "display=fullscreen\nend\n").displayMode == DisplayMode::Fullscreen);
    REQUIRE(parseSettings(head + "display=windowed\nend\n").displayMode == DisplayMode::Windowed);

    for (const char* bad : {"", "abc", "1920", "1920x", "x1080", "1920x1080x60", "1920 x 1080", "-1920x1080", "0x1080", "1920x0", "99999x99999", "1e3x1e3", "1920.0x1080"}) {
        INFO(bad);
        REQUIRE(parseSettings(head + "resolution=" + bad + "\nend\n").resolution == Resolution{});
    }
    REQUIRE(parseSettings(head + "resolution=2560x1440\nend\n").resolution == Resolution{2560, 1440});
    REQUIRE(parseSettings(head + "resolution=16384x16384\nend\n").resolution == Resolution{16384, 16384});

    for (const char* bad : {"", "2", "on", "true", "-1", "01"}) {
        INFO(bad);
        REQUIRE(parseSettings(head + "vsync=" + bad + "\nend\n").vsync);
    }
    REQUIRE_FALSE(parseSettings(head + "vsync=0\nend\n").vsync);
    REQUIRE(parseSettings(head + "vsync=1\nend\n").vsync);

    // A bad display value costs only its field.
    const Settings mixed = parseSettings(head + "fov=80\ndisplay=nope\nresolution=800x600\nvsync=0\nend\n");
    REQUIRE(mixed.fovDegrees == 80.0f);
    REQUIRE(mixed.displayMode == DisplayMode::Windowed);
    REQUIRE(mixed.resolution == Resolution{800, 600});
    REQUIRE_FALSE(mixed.vsync);

    Settings absurd;
    absurd.resolution = {-5, 20000};
    REQUIRE(absurd.clamped().resolution == Resolution{});
    absurd.resolution = {640, 480};
    REQUIRE(absurd.clamped().resolution == Resolution{640, 480});

    TempFile file;
    REQUIRE(saveSettings(file.path(), display));
    REQUIRE(loadSettings(file.path()) == display);
}

TEST_CASE("a resolution step walks the display's sizes from the one shown and stops at the ends")
{
    const std::vector<Resolution> modes{{3840, 2160}, {2560, 1440}, {1920, 1080}, {1280, 720}};
    Settings stored;
    stored.fovDegrees = 80.0f;
    REQUIRE(stepResolution(stored, modes, {1920, 1080}, 1).resolution == Resolution{1280, 720});
    REQUIRE(stepResolution(stored, modes, {1920, 1080}, -1).resolution == Resolution{2560, 1440});
    REQUIRE(stepResolution(stored, modes, {1920, 1080}, -5).resolution == Resolution{3840, 2160});
    REQUIRE(stepResolution(stored, modes, {1280, 720}, 3).resolution == Resolution{1280, 720});
    REQUIRE(stepResolution(stored, modes, {1900, 1000}, 1).resolution == Resolution{1280, 720}); // a window size no mode has starts from the nearest
    REQUIRE(stepResolution(stored, modes, {1920, 1080}, 1).fovDegrees == 80.0f);
    REQUIRE(stepResolution(stored, modes, {1920, 1080}, 0) == stored);
    REQUIRE(stepResolution(stored, {}, {1920, 1080}, 1) == stored);
}

TEST_CASE("the first-minute prompt flags survive the file, and a file from before them has none set")
{
    const Settings flagged{80.0f, 2.0f, 0b1011u};
    REQUIRE(parseSettings(serializeSettings(flagged)) == flagged);
    REQUIRE(serializeSettings(flagged).find("promptsSeen=11\n") != std::string::npos);

    const std::string head = "ichor-settings 1\n";
    REQUIRE(parseSettings(head + "fov=80\nmouseSensitivity=2\nend\n") == Settings{80.0f, 2.0f, 0u});
    REQUIRE(parseSettings(head + "promptsSeen=15\nend\n") == Settings{Settings::kDefaultFovDegrees, Settings::kDefaultMouseSensitivity, 15u});
    for (const char* bad : {"abc", "-1", "1.5", "", "99999999999999999999", "3x"}) {
        INFO(bad);
        REQUIRE(parseSettings(head + "fov=80\npromptsSeen=" + bad + "\nend\n") == Settings{80.0f, Settings::kDefaultMouseSensitivity, 0u});
    }
    REQUIRE(parseSettings(head + "promptsSeen=0\nend\n").promptsSeen == 0u); // the reset

    REQUIRE(Settings{kNaN, kNaN, 7u}.clamped() == Settings{Settings::kDefaultFovDegrees, Settings::kDefaultMouseSensitivity, 7u});
    REQUIRE(stepMouseSensitivity(flagged, 2.0f, 1).promptsSeen == 0b1011u);

    TempFile file;
    REQUIRE(saveSettings(file.path(), flagged));
    REQUIRE(loadSettings(file.path()) == flagged);
}

TEST_CASE("the UI scale and palette survive the file, and a bad value reads as the default")
{
    Settings look;
    look.uiScale = 1.25f;
    look.palette = ColourPalette::ColourblindSafe;
    REQUIRE(parseSettings(serializeSettings(look)) == look);
    const std::string text = serializeSettings(look);
    REQUIRE(text.find("uiScale=1.25\n") != std::string::npos);
    REQUIRE(text.find("palette=colourblind\n") != std::string::npos);
    REQUIRE(serializeSettings(kDefaults).find("uiScale=1\npalette=default\n") != std::string::npos);

    // A file from before these keys is the stand-in default.
    const std::string head = "ichor-settings 1\n";
    const Settings old = parseSettings(head + "fov=80\nend\n");
    REQUIRE(old.uiScale == Settings::kDefaultUiScale);
    REQUIRE(old.palette == ColourPalette::Default);
    REQUIRE(kDefaults.uiScale == 1.0f);
    REQUIRE(kDefaults.palette == ColourPalette::Default);

    for (const char* bad : {"", "Colourblind", "colorblind", "1", "tritan"}) {
        INFO(bad);
        REQUIRE(parseSettings(head + "palette=" + bad + "\nend\n").palette == ColourPalette::Default);
    }
    for (const char* bad : {"", "abc", "nan", "inf", "1x", "--1"}) {
        INFO(bad);
        REQUIRE(parseSettings(head + "uiScale=" + bad + "\nend\n").uiScale == Settings::kDefaultUiScale);
    }
    REQUIRE(parseSettings(head + "uiScale=9\nend\n").uiScale == Settings::kMaxUiScale);
    REQUIRE(parseSettings(head + "uiScale=0.1\nend\n").uiScale == Settings::kMinUiScale);

    // A bad value costs only its field.
    const Settings mixed = parseSettings(head + "fov=80\nuiScale=wide\npalette=colourblind\nend\n");
    REQUIRE(mixed.fovDegrees == 80.0f);
    REQUIRE(mixed.uiScale == Settings::kDefaultUiScale);
    REQUIRE(mixed.palette == ColourPalette::ColourblindSafe);

    Settings absurd;
    absurd.uiScale = kNaN;
    REQUIRE(absurd.clamped().uiScale == Settings::kDefaultUiScale);

    TempFile file;
    REQUIRE(saveSettings(file.path(), look));
    REQUIRE(loadSettings(file.path()) == look);
}

TEST_CASE("a UI scale step moves a quarter at a time inside the range and keeps the other settings")
{
    Settings stored;
    stored.fovDegrees = 80.0f;
    stored.palette = ColourPalette::ColourblindSafe;
    REQUIRE(stepUiScale(stored, 0) == stored);
    REQUIRE(stepUiScale(stored, 1).uiScale == 1.25f);
    REQUIRE(stepUiScale(stored, -1).uiScale == 0.75f);
    REQUIRE(stepUiScale(stored, -9).uiScale == Settings::kMinUiScale);
    REQUIRE(stepUiScale(stored, 9).uiScale == Settings::kMaxUiScale);
    REQUIRE(stepUiScale(stored, 1).fovDegrees == 80.0f);
    REQUIRE(stepUiScale(stored, 1).palette == ColourPalette::ColourblindSafe);
}
