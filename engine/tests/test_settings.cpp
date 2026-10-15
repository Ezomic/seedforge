#include "ichor/save/settings.h"
#include "test_temp.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
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
