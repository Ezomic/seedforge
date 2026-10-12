#include "ichor/script/script_host.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace ichor;

namespace {

double number(const ScriptValue& value)
{
    return std::get<double>(value);
}

std::vector<double> draws(ScriptHost& host, const std::string& stream, int count)
{
    REQUIRE(host.run("draw", "function draw(s) return ichor.random(s) end"));
    std::vector<double> out;
    for (int i = 0; i < count; ++i) {
        ScriptValue result;
        REQUIRE(host.call("draw", {stream}, &result));
        out.push_back(number(result));
    }
    return out;
}

}

TEST_CASE("a script runs, returns values and takes arguments")
{
    ScriptHost host(1);
    REQUIRE(host.run("t", "function add(a, b) return a + b end function greet(n) return 'hello ' .. n end"));
    REQUIRE(host.has("add"));
    REQUIRE_FALSE(host.has("missing"));
    ScriptValue result;
    REQUIRE(host.call("add", {2.0, 40.0}, &result));
    REQUIRE(number(result) == Catch::Approx(42.0));
    REQUIRE(host.call("greet", {std::string("world")}, &result));
    REQUIRE(std::get<std::string>(result) == "hello world");
}

TEST_CASE("the sandbox has no way out")
{
    ScriptHost host(1);
    for (const char* name : {"io", "os", "package", "debug", "dofile", "loadfile", "load", "require"}) {
        INFO(name);
        REQUIRE(host.run("probe", std::string("assert(") + name + " == nil)"));
    }
    REQUIRE(host.run("probe", "assert(math.random == nil and math.randomseed == nil)"));
    REQUIRE(host.run("probe", "assert(string.format('%d', 3) == '3')"));
}

TEST_CASE("errors come back as results with the chunk name")
{
    ScriptHost host(1);
    REQUIRE_FALSE(host.run("broken.lua", "this is not lua"));
    REQUIRE(host.lastError().find("broken.lua") != std::string::npos);
    REQUIRE_FALSE(host.run("raises.lua", "error('boom')"));
    REQUIRE(host.lastError().find("boom") != std::string::npos);
    REQUIRE_FALSE(host.call("nothing"));
    REQUIRE(host.run("ok", "x = 1")); // the host is still usable
    REQUIRE(host.lastError().empty());
}

TEST_CASE("a runaway script is stopped by the instruction budget and the host survives")
{
    ScriptHost host(1, {50'000});
    REQUIRE_FALSE(host.run("spin", "while true do end"));
    REQUIRE(host.lastError().find("budget") != std::string::npos);
    REQUIRE(host.run("after", "function ok() return 7 end"));
    ScriptValue result;
    REQUIRE(host.call("ok", {}, &result));
    REQUIRE(number(result) == Catch::Approx(7.0));

    REQUIRE(host.run("loop", "function busy() local n = 0 while true do n = n + 1 end end"));
    REQUIRE_FALSE(host.call("busy"));
    REQUIRE(host.lastError().find("budget") != std::string::npos);
}

TEST_CASE("the budget is per entry, so many small calls never add up")
{
    ScriptHost host(1, {20'000});
    REQUIRE(host.run("f", "function small() local n = 0 for i = 1, 2000 do n = n + i end return n end"));
    for (int i = 0; i < 200; ++i) {
        REQUIRE(host.call("small"));
    }
}

TEST_CASE("named random streams are seeded, reproducible and independent")
{
    ScriptHost a(42);
    ScriptHost b(42);
    ScriptHost c(43);
    const auto first = draws(a, "loot", 8);
    REQUIRE(first == draws(b, "loot", 8));
    REQUIRE(first != draws(c, "loot", 8));
    for (double v : first) {
        REQUIRE(v >= 0.0);
        REQUIRE(v < 1.0);
    }

    // Drawing from one stream does not move another.
    ScriptHost d(42);
    ScriptHost e(42);
    draws(d, "weather", 5);
    REQUIRE(draws(d, "loot", 8) == draws(e, "loot", 8));
}

TEST_CASE("random_range is inclusive and covers its span")
{
    ScriptHost host(9);
    REQUIRE(host.run("r", "function roll() return ichor.random_range('dice', 1, 6) end"));
    std::vector<bool> seen(7, false);
    for (int i = 0; i < 400; ++i) {
        ScriptValue result;
        REQUIRE(host.call("roll", {}, &result));
        const int value = static_cast<int>(number(result));
        REQUIRE(value >= 1);
        REQUIRE(value <= 6);
        seen[static_cast<std::size_t>(value)] = true;
    }
    for (int face = 1; face <= 6; ++face) {
        REQUIRE(seen[static_cast<std::size_t>(face)]);
    }
}

TEST_CASE("running a chunk again replaces its functions, which is hot reload")
{
    ScriptHost host(1);
    ScriptValue result;
    REQUIRE(host.run("tuning", "function stamina() return 100 end"));
    REQUIRE(host.call("stamina", {}, &result));
    REQUIRE(number(result) == Catch::Approx(100.0));
    REQUIRE(host.run("tuning", "function stamina() return 120 end"));
    REQUIRE(host.call("stamina", {}, &result));
    REQUIRE(number(result) == Catch::Approx(120.0));
}

TEST_CASE("a script file loads by path")
{
    const auto path = std::filesystem::temp_directory_path() / "ichor_script_test.lua";
    {
        std::ofstream file(path, std::ios::binary);
        file << "function answer() return 42 end";
    }
    ScriptHost host(1);
    REQUIRE(host.runFile(path));
    ScriptValue result;
    REQUIRE(host.call("answer", {}, &result));
    REQUIRE(number(result) == Catch::Approx(42.0));
    std::filesystem::remove(path);
    REQUIRE_FALSE(host.runFile(path));
}

TEST_CASE("sorted_pairs walks keys in a fixed order whatever the hash seed")
{
    ScriptHost host(1);
    REQUIRE(host.run("s", R"(
        function order()
            local t = { zebra = 1, apple = 2, mango = 3, [2] = 'b', [1] = 'a' }
            local out = {}
            for k in ichor.sorted_pairs(t) do out[#out + 1] = tostring(k) end
            return table.concat(out, ',')
        end
    )"));
    ScriptValue result;
    REQUIRE(host.call("order", {}, &result));
    REQUIRE(std::get<std::string>(result) == "1,2,apple,mango,zebra");
}

TEST_CASE("log lines go to the sink")
{
    ScriptHost host(1);
    std::vector<std::string> lines;
    int warnings = 0;
    host.setLogSink([&](ScriptHost::Level level, const std::string& message) {
        lines.push_back(message);
        warnings += level == ScriptHost::Level::Warn ? 1 : 0;
    });
    REQUIRE(host.run("l", "ichor.log('a') ichor.warn('b')"));
    REQUIRE(lines == std::vector<std::string>{"a", "b"});
    REQUIRE(warnings == 1);
}

TEST_CASE("pcall cannot swallow the budget and keep the script running")
{
    ScriptHost host(1, {50'000});
    REQUIRE_FALSE(host.run("swallow", "while true do pcall(function() while true do end end) end"));
    REQUIRE(host.lastError().find("budget") != std::string::npos);
    REQUIRE_FALSE(host.run("swallow-once", "pcall(function() while true do end end) x = 1"));
    REQUIRE(host.lastError().find("budget") != std::string::npos);
    REQUIRE(host.run("after", "y = 2")); // a fresh entry gets a fresh budget
}

TEST_CASE("a script that allocates too much gets a memory error and the host survives")
{
    ScriptHost host(1, {1'000'000, 8ull * 1024 * 1024});
    REQUIRE_FALSE(host.run("hog", "local s = ('x'):rep(100000000)"));
    REQUIRE_FALSE(host.run("grow", "local t = {} for i = 1, 1e9 do t[i] = {i} end"));
    REQUIRE(host.run("after", "z = 3"));
}

TEST_CASE("random_range survives extreme bounds")
{
    ScriptHost host(5);
    REQUIRE(host.run("r", R"(
        local lo, hi = math.mininteger, math.maxinteger
        local a = ichor.random_range('x', lo, hi)
        local b = ichor.random_range('x', -1, hi)
        local c = ichor.random_range('x', lo, 1)
        local d = ichor.random_range('x', 5, 5)
        assert(math.type(a) == 'integer' and b >= -1 and c <= 1 and d == 5)
    )"));
}

TEST_CASE("finalizers and bytecode are refused")
{
    ScriptHost host(1);
    REQUIRE_FALSE(host.run("gc", "setmetatable({}, { __gc = function() end })"));
    REQUIRE_FALSE(host.run("close", "setmetatable({}, { __close = function() end })"));
    REQUIRE(host.run("ok", "setmetatable({}, { __index = function() return 1 end })"));
    REQUIRE_FALSE(host.run("bytecode", "Lua"));
    REQUIRE(host.run("nodump", "assert(string.dump == nil)"));
}

TEST_CASE("print goes to the log sink, not stdout")
{
    ScriptHost host(1);
    std::vector<std::string> lines;
    host.setLogSink([&](ScriptHost::Level, const std::string& message) { lines.push_back(message); });
    REQUIRE(host.run("p", "print('a', 1, true)"));
    REQUIRE(lines == std::vector<std::string>{"a	1	true"});
}

TEST_CASE("a log sink cannot call back into the host")
{
    ScriptHost host(1, {30'000});
    std::string inner;
    host.setLogSink([&](ScriptHost::Level, const std::string&) {
        if (!host.run("inner", "x = 1")) inner = host.lastError();
    });
    REQUIRE_FALSE(host.run("outer", "while true do ichor.log('tick') end"));
    REQUIRE(inner == "reentrant script call");
}
