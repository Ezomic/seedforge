#include "ichor/ui/map_annotation_store.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <string>

using namespace ichor;

namespace {

MapAnnotation mark(const std::string& id, float x = 1.0f, float y = 2.0f)
{
    MapAnnotation a;
    a.id = id;
    a.kind = AnnotationKind::Mark;
    a.symbol = AnnotationSymbol::Camp;
    a.text = "camp";
    a.points = {{x, y}};
    return a;
}

MapAnnotation path(const std::string& id, std::size_t points)
{
    MapAnnotation a;
    a.id = id;
    a.kind = AnnotationKind::Path;
    for (std::size_t i = 0; i < points; ++i) {
        a.points.push_back({static_cast<float>(i), static_cast<float>(i) * 2.0f});
    }
    return a;
}

} // namespace

TEST_CASE("add, find, move and remove", "[map_annotation]")
{
    MapAnnotationStore store;
    REQUIRE(store.add(mark("a")) == AnnotationResult::Ok);
    REQUIRE(store.add(mark("b", 5, 6)) == AnnotationResult::Ok);
    REQUIRE(store.size() == 2);
    REQUIRE(store.find("b")->points[0].x == 5.0f);
    REQUIRE(store.find("nope") == nullptr);

    REQUIRE(store.move("a", {10, 20}) == AnnotationResult::Ok);
    REQUIRE(store.find("a")->points[0].y == 20.0f);
    REQUIRE(store.move("zzz", {0, 0}) == AnnotationResult::UnknownId);

    REQUIRE(store.remove("a") == AnnotationResult::Ok);
    REQUIRE(store.remove("a") == AnnotationResult::UnknownId);
    REQUIRE(store.size() == 1);
    REQUIRE(store.all()[0].id == "b");
}

TEST_CASE("insertion order survives removal", "[map_annotation]")
{
    MapAnnotationStore store;
    for (const char* id : {"c", "a", "b"}) {
        REQUIRE(store.add(mark(id)) == AnnotationResult::Ok);
    }
    REQUIRE(store.remove("a") == AnnotationResult::Ok);
    REQUIRE(store.all()[0].id == "c");
    REQUIRE(store.all()[1].id == "b");
}

TEST_CASE("duplicate ids are refused and change nothing", "[map_annotation]")
{
    MapAnnotationStore store;
    REQUIRE(store.add(mark("a", 1, 1)) == AnnotationResult::Ok);
    REQUIRE(store.add(mark("a", 9, 9)) == AnnotationResult::DuplicateId);
    REQUIRE(store.size() == 1);
    REQUIRE(store.find("a")->points[0].x == 1.0f);
}

TEST_CASE("ids are checked", "[map_annotation]")
{
    MapAnnotationStore store;
    REQUIRE(store.add(mark("")) == AnnotationResult::InvalidId);
    REQUIRE(store.add(mark("has space")) == AnnotationResult::InvalidId);
    REQUIRE(store.add(mark("new\nline")) == AnnotationResult::InvalidId);
    REQUIRE(store.add(mark(std::string("a\0b", 3))) == AnnotationResult::InvalidId);
    REQUIRE(store.add(mark("caf\xC3\xA9")) == AnnotationResult::InvalidId);
    REQUIRE(store.add(mark(std::string(48, 'x'))) == AnnotationResult::Ok);
    REQUIRE(store.add(mark(std::string(49, 'y'))) == AnnotationResult::InvalidId);
    REQUIRE(store.add(mark(std::string(10000000, 'z'))) == AnnotationResult::InvalidId);
    REQUIRE(store.add(mark("ok-1_2.3")) == AnnotationResult::Ok);
}

TEST_CASE("the per-map limit stops additions but not removals", "[map_annotation]")
{
    AnnotationLimits limits;
    limits.maxAnnotations = 3;
    MapAnnotationStore store(limits);
    for (int i = 0; i < 3; ++i) {
        REQUIRE(store.add(mark("m" + std::to_string(i))) == AnnotationResult::Ok);
    }
    REQUIRE(store.add(mark("m3")) == AnnotationResult::TooMany);
    REQUIRE(store.size() == 3);
    REQUIRE(store.remove("m0") == AnnotationResult::Ok);
    REQUIRE(store.add(mark("m3")) == AnnotationResult::Ok);
}

TEST_CASE("text limits", "[map_annotation]")
{
    MapAnnotationStore store;
    auto a = mark("a");
    a.text = std::string(280, 'x');
    REQUIRE(store.add(a) == AnnotationResult::Ok);
    a.id = "b";
    a.text = std::string(281, 'x');
    REQUIRE(store.add(a) == AnnotationResult::TextTooLong);
    a.text = std::string(100000000, 'x');
    REQUIRE(store.add(std::move(a)) == AnnotationResult::TextTooLong);

    auto note = mark("n");
    note.kind = AnnotationKind::Note;
    note.text.clear();
    REQUIRE(store.add(note) == AnnotationResult::InvalidText);
    note.text = "the ford is east of here";
    REQUIRE(store.add(note) == AnnotationResult::Ok);
}

TEST_CASE("hostile text is refused", "[map_annotation]")
{
    MapAnnotationStore store;
    const std::string bad[] = {
        std::string("a\0b", 3),
        "line\nbreak",
        "tab\there",
        "esc\x1b[31m",
        "del\x7f",
        "\xC2\x85",             // C1 NEL
        "\xC0\xAF",             // overlong slash
        "\xE0\x80\xAF",         // overlong three-byte
        "\xED\xA0\x80",         // UTF-16 surrogate
        "\xF4\x90\x80\x80",     // above U+10FFFF
        "\xF8\x88\x80\x80\x80", // five-byte form
        "\x80",                 // lone continuation
        "\xE2\x82",             // truncated sequence
        "\xFF\xFE",
    };
    int n = 0;
    for (const auto& text : bad) {
        auto a = mark("t" + std::to_string(n++));
        a.text = text;
        REQUIRE(store.add(a) == AnnotationResult::InvalidText);
    }
    REQUIRE(store.size() == 0);

    auto good = mark("g");
    good.text = "caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x97\xBA";
    REQUIRE(store.add(good) == AnnotationResult::Ok);
    REQUIRE(isCleanAnnotationText(""));
    REQUIRE_FALSE(isCleanAnnotationText("\xF0\x9F\x97"));
}

TEST_CASE("positions must be finite and in range", "[map_annotation]")
{
    constexpr float nan = std::numeric_limits<float>::quiet_NaN();
    constexpr float inf = std::numeric_limits<float>::infinity();
    constexpr float big = std::numeric_limits<float>::max();
    MapAnnotationStore store;
    REQUIRE(store.add(mark("a", nan, 0)) == AnnotationResult::InvalidPosition);
    REQUIRE(store.add(mark("b", 0, -inf)) == AnnotationResult::InvalidPosition);
    REQUIRE(store.add(mark("c", big, 0)) == AnnotationResult::InvalidPosition);
    REQUIRE(store.add(mark("d", 1.0e6f + 1000.0f, 0)) == AnnotationResult::InvalidPosition);
    REQUIRE(store.add(mark("e", 1.0e6f, -1.0e6f)) == AnnotationResult::Ok);
    REQUIRE(store.size() == 1);

    REQUIRE(store.move("e", {nan, 0}) == AnnotationResult::InvalidPosition);
    REQUIRE(store.move("e", {0, 2.0e6f}) == AnnotationResult::InvalidPosition);
    REQUIRE(store.find("e")->points[0].x == 1.0e6f);
}

TEST_CASE("negative zero is stored as zero", "[map_annotation]")
{
    MapAnnotationStore store;
    REQUIRE(store.add(mark("z", -0.0f, -0.0f)) == AnnotationResult::Ok);
    REQUIRE_FALSE(std::signbit(store.find("z")->points[0].x));
    REQUIRE_FALSE(std::signbit(store.find("z")->points[0].y));
}

TEST_CASE("shapes match the kind", "[map_annotation]")
{
    MapAnnotationStore store;
    auto two = mark("m");
    two.points.push_back({3, 3});
    REQUIRE(store.add(two) == AnnotationResult::InvalidShape);
    auto none = mark("m");
    none.points.clear();
    REQUIRE(store.add(none) == AnnotationResult::InvalidShape);
    REQUIRE(store.add(path("p", 1)) == AnnotationResult::InvalidShape);
    REQUIRE(store.add(path("p", 0)) == AnnotationResult::InvalidShape);
    REQUIRE(store.add(path("p", 128)) == AnnotationResult::Ok);
    REQUIRE(store.add(path("q", 129)) == AnnotationResult::InvalidShape);
    REQUIRE(store.add(path("r", 100000)) == AnnotationResult::InvalidShape);

    auto badKind = mark("k");
    badKind.kind = static_cast<AnnotationKind>(77);
    REQUIRE(store.add(badKind) == AnnotationResult::InvalidShape);
    auto badSymbol = mark("s");
    badSymbol.symbol = static_cast<AnnotationSymbol>(200);
    REQUIRE(store.add(badSymbol) == AnnotationResult::InvalidShape);
}

TEST_CASE("moving a path translates it and is all or nothing", "[map_annotation]")
{
    MapAnnotationStore store;
    REQUIRE(store.add(path("p", 3)) == AnnotationResult::Ok); // (0,0) (1,2) (2,4)
    REQUIRE(store.move("p", {10, 10}) == AnnotationResult::Ok);
    const auto& pts = store.find("p")->points;
    REQUIRE(pts[0].x == 10.0f);
    REQUIRE(pts[2].x == 12.0f);
    REQUIRE(pts[2].y == 14.0f);

    REQUIRE(store.move("p", {1.0e6f, 0}) == AnnotationResult::InvalidPosition); // later points leave the range
    REQUIRE(store.find("p")->points[0].x == 10.0f);
    REQUIRE(store.find("p")->points[2].x == 12.0f);
}
