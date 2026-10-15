#include "ichor/core/binary.h"
#include "ichor/core/input.h"
#include "ichor/sim/components.h"
#include "test_temp.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace ichor;

namespace {

struct Nested {
    Transform transform;
    std::string label;
    std::vector<std::uint32_t> numbers;
    InputFrame input;
};

}

template <>
struct ichor::Reflect<Nested> {
    static constexpr auto fields()
    {
        return std::make_tuple(field("transform", &Nested::transform), field("label", &Nested::label),
            field("numbers", &Nested::numbers), field("input", &Nested::input));
    }
};

TEST_CASE("reflected structs round-trip through the binary encoding")
{
    Nested value;
    value.transform.position = {1.5, -2.25, 1e6};
    value.transform.rotation = glm::quat(0.5f, 0.5f, 0.5f, 0.5f);
    value.transform.scale = {2.0f, 1.0f, 0.5f};
    value.label = "hoplite";
    value.numbers = {1, 2, 3, 99};
    value.input = {InputFrame::Sprint, 0.5f, -1.0f};

    BinaryWriter w;
    w.write(value);
    const auto bytes = w.bytes();

    BinaryReader r(bytes);
    Nested back;
    REQUIRE(r.read(back));
    REQUIRE(r.atEnd());
    REQUIRE(back.transform.position == value.transform.position);
    REQUIRE(back.transform.rotation == value.transform.rotation);
    REQUIRE(back.transform.scale == value.transform.scale);
    REQUIRE(back.label == value.label);
    REQUIRE(back.numbers == value.numbers);
    REQUIRE(back.input == value.input);
}

TEST_CASE("truncated input is refused, never read past the end")
{
    Nested value;
    value.label = "a long enough label";
    value.numbers.assign(50, 7);
    BinaryWriter w;
    w.write(value);
    const auto bytes = w.bytes();

    for (std::size_t cut : {std::size_t{0}, std::size_t{10}, bytes.size() / 2, bytes.size() - 1}) {
        BinaryReader r(std::span(bytes.data(), cut));
        Nested back;
        REQUIRE_FALSE(r.read(back));
    }
}

TEST_CASE("a corrupt length cannot allocate more than the file holds")
{
    std::vector<std::uint8_t> bytes = {0xff, 0xff, 0xff, 0xff, 'x'};
    BinaryReader r(bytes);
    std::string s;
    REQUIRE_FALSE(r.read(s));
    BinaryReader r2(bytes);
    std::vector<std::uint32_t> v;
    REQUIRE_FALSE(r2.read(v));
}

TEST_CASE("input streams save and load")
{
    InputStream stream;
    for (int i = 0; i < 100; ++i) {
        stream.frames.push_back({static_cast<std::uint32_t>(i % 4), static_cast<float>(i) * 0.01f, -0.5f});
    }
    const auto path = test::tempPath("ichor_inputs.iinp");
    REQUIRE(stream.save(path));
    InputStream loaded;
    REQUIRE(loaded.load(path));
    std::filesystem::remove(path);
    REQUIRE(loaded.frames == stream.frames);
}
