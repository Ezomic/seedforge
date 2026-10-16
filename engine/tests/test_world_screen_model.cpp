#include "ichor/ui/world_screen_model.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>

using namespace ichor;

namespace {

WorldScreenAxiom axiom(std::string id, std::string title, AxiomSlot slot, bool discovered = false)
{
    return {std::move(id), std::move(title), slot, discovered};
}

const WorldScreenSection* find(const std::vector<WorldScreenSection>& view, WorldScreenSectionKind kind)
{
    for (const auto& section : view) {
        if (section.kind == kind) {
            return &section;
        }
    }
    return nullptr;
}

}

TEST_CASE("an empty world has no sections", "[world_screen]")
{
    CHECK(buildWorldScreen({}).empty());
}

TEST_CASE("an undiscovered synergy stays hidden", "[world_screen]")
{
    WorldScreenInput in;
    in.synergies = {{"s.hidden", "The Secret", false}, {"s.known", "The Known", true}};
    const auto view = buildWorldScreen(in);
    const auto* section = find(view, WorldScreenSectionKind::Synergies);
    REQUIRE(section != nullptr);
    REQUIRE(section->lines.size() == 1);
    CHECK(section->lines[0].text == "The Known");

    in.synergies = {{"s.hidden", "The Secret", false}};
    CHECK(find(buildWorldScreen(in), WorldScreenSectionKind::Synergies) == nullptr);
}

TEST_CASE("pressure and anomaly stay hidden until discovered", "[world_screen]")
{
    WorldScreenInput in;
    in.axioms = {axiom("pressure.famine", "The Famine", AxiomSlot::Pressure),
        axiom("anomaly.labyrinth", "The Labyrinth", AxiomSlot::Anomaly),
        axiom("cataclysm.flood", "Flood", AxiomSlot::Cataclysm)};
    in.pressureLevel = 0.5;
    auto view = buildWorldScreen(in);
    REQUIRE(view.size() == 1);
    CHECK(view[0].kind == WorldScreenSectionKind::Axioms);
    CHECK(view[0].lines.size() == 1);
    CHECK(find(view, WorldScreenSectionKind::Pressure) == nullptr);

    in.axioms[0].discovered = true;
    view = buildWorldScreen(in);
    const auto* pressure = find(view, WorldScreenSectionKind::Pressure);
    REQUIRE(pressure != nullptr);
    REQUIRE(pressure->lines.size() == 1);
    CHECK(pressure->lines[0].text == "The Famine");
    CHECK(pressure->lines[0].percent == 50);
    CHECK(find(view, WorldScreenSectionKind::Axioms)->lines.size() == 1);

    in.axioms[1].discovered = true;
    CHECK(find(buildWorldScreen(in), WorldScreenSectionKind::Axioms)->lines.size() == 2);
}

TEST_CASE("sections come in a fixed order and axioms sort by slot then id", "[world_screen]")
{
    WorldScreenInput in;
    in.axioms = {axiom("powers.league", "League", AxiomSlot::Powers), axiom("law.b", "B", AxiomSlot::Law),
        axiom("cataclysm.flood", "Flood", AxiomSlot::Cataclysm), axiom("law.a", "A", AxiomSlot::Law),
        axiom("pressure.famine", "Famine", AxiomSlot::Pressure, true)};
    in.synergies = {{"s", "S", true}};
    in.pressureLevel = 0.2;
    in.territory = {{"League", 0.4}};
    const auto view = buildWorldScreen(in);
    REQUIRE(view.size() == 4);
    CHECK(view[0].kind == WorldScreenSectionKind::Axioms);
    CHECK(view[1].kind == WorldScreenSectionKind::Synergies);
    CHECK(view[2].kind == WorldScreenSectionKind::Pressure);
    CHECK(view[3].kind == WorldScreenSectionKind::Territory);
    REQUIRE(view[0].lines.size() == 4);
    CHECK(view[0].lines[0].text == "Flood");
    CHECK(view[0].lines[1].text == "A");
    CHECK(view[0].lines[2].text == "B");
    CHECK(view[0].lines[3].text == "League");

    auto reversed = in;
    std::reverse(reversed.axioms.begin(), reversed.axioms.end());
    CHECK(buildWorldScreen(reversed) == view);
}

TEST_CASE("long lists are capped and the overflow counted", "[world_screen]")
{
    WorldScreenInput in;
    for (int i = 0; i < 1000; ++i) {
        in.axioms.push_back(axiom("law." + std::to_string(1000 + i), "L", AxiomSlot::Law));
        in.synergies.push_back({"s" + std::to_string(i), "S", true});
        in.territory.push_back({"f" + std::to_string(i), 0.0005 * i});
    }
    const auto view = buildWorldScreen(in);
    CHECK(find(view, WorldScreenSectionKind::Axioms)->lines.size() == kWorldScreenMaxAxiomLines);
    CHECK(find(view, WorldScreenSectionKind::Axioms)->omitted == 1000 - kWorldScreenMaxAxiomLines);
    CHECK(find(view, WorldScreenSectionKind::Synergies)->lines.size() == kWorldScreenMaxSynergyLines);
    const auto* territory = find(view, WorldScreenSectionKind::Territory);
    CHECK(territory->lines.size() == kWorldScreenMaxTerritoryLines);
    CHECK(territory->lines[0].text == "f999");
    CHECK(territory->omitted == 999 - kWorldScreenMaxTerritoryLines);
}

TEST_CASE("repeated ids are listed once", "[world_screen]")
{
    WorldScreenInput in;
    in.axioms = {axiom("law.a", "A", AxiomSlot::Law), axiom("law.a", "A again", AxiomSlot::Law)};
    in.synergies = {{"s", "S", true}, {"s", "S", true}};
    const auto view = buildWorldScreen(in);
    CHECK(find(view, WorldScreenSectionKind::Axioms)->lines.size() == 1);
    CHECK(find(view, WorldScreenSectionKind::Synergies)->lines.size() == 1);
}

TEST_CASE("hostile text is cleaned and bounded", "[world_screen]")
{
    WorldScreenInput in;
    in.axioms = {axiom("law.a", std::string("Bad\n\t\x01 title\x7f") + std::string(5000, 'x'), AxiomSlot::Law),
        axiom("law.b", std::string(79, 'a') + "\xC3\xA9", AxiomSlot::Law),
        axiom("law.c", "\n\t ", AxiomSlot::Law), axiom("", "", AxiomSlot::Law)};
    const auto view = buildWorldScreen(in);
    const auto& lines = find(view, WorldScreenSectionKind::Axioms)->lines;
    REQUIRE(lines.size() == 3);
    CHECK(lines[0].text.size() <= kWorldScreenMaxTextBytes);
    CHECK(lines[0].text.rfind("Bad title", 0) == 0);
    for (const char c : lines[0].text) {
        CHECK(static_cast<unsigned char>(c) >= 0x20);
        CHECK(c != 0x7f);
    }
    CHECK(lines[1].text == std::string(79, 'a'));
    CHECK(lines[2].text == "law.c");
}

TEST_CASE("missing and non finite numbers are handled", "[world_screen]")
{
    WorldScreenInput in;
    in.axioms = {axiom("pressure.famine", "Famine", AxiomSlot::Pressure, true)};
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    auto line = [&](std::optional<double> level) {
        in.pressureLevel = level;
        return find(buildWorldScreen(in), WorldScreenSectionKind::Pressure)->lines[0];
    };
    CHECK(!line(std::nullopt).percent.has_value());
    CHECK(!line(nan).percent.has_value());
    CHECK(!line(inf).percent.has_value());
    CHECK(line(-3.0).percent == 0);
    CHECK(line(7.0).percent == 100);

    in.territory = {{"A", nan}, {"B", inf}, {"C", -1.0}, {"D", 2.0}, {"", 0.5}, {"E", 0.25}};
    const auto view = buildWorldScreen(in);
    const auto* territory = find(view, WorldScreenSectionKind::Territory);
    REQUIRE(territory != nullptr);
    REQUIRE(territory->lines.size() == 2);
    CHECK(territory->lines[0].text == "D");
    CHECK(territory->lines[0].percent == 100);
    CHECK(territory->lines[1].text == "E");
    CHECK(territory->lines[1].percent == 25);
}
