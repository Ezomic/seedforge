#include "ichor/save/settings.h"
#include "test_temp.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>

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
