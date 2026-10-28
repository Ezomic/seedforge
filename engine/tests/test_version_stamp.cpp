#include "ichor/core/controls_overlay.h"
#include "ichor/core/version.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

using namespace ichor;

TEST_CASE("the build carries a version stamp")
{
    REQUIRE_FALSE(versionStamp().empty());
    REQUIRE(versionStamp().find_first_of(" \t\r\n") == std::string_view::npos);
}

TEST_CASE("the log header names the build")
{
    const std::string header = logHeader();
    REQUIRE(header.starts_with("Ichor"));
    REQUIRE(header.find(versionStamp()) != std::string::npos);
}

TEST_CASE("the window title keeps its name and adds the build")
{
    const std::string title = versionedTitle("Ichor");
    REQUIRE(title.starts_with("Ichor"));
    REQUIRE(title.find(versionStamp()) != std::string::npos);
    REQUIRE(versionedTitle("").find(versionStamp()) != std::string::npos);
}

TEST_CASE("the open F1 card shows the build, the first-run hint does not")
{
    const auto mentionsStamp = [](const std::vector<std::string>& lines) {
        return std::ranges::any_of(lines, [](const std::string& line) { return line.find(versionStamp()) != std::string::npos; });
    };
    REQUIRE(mentionsStamp(controlsOverlayLines(startControlsOverlay(false, true))));
    REQUIRE_FALSE(mentionsStamp(controlsOverlayLines(startControlsOverlay(true, false))));
}
