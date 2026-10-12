#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ichor {

using ScriptValue = std::variant<std::monostate, bool, double, std::string>;

struct ScriptLimits {
    std::uint64_t instructions = 1'000'000; // per entry into Lua
    std::uint64_t memoryBytes = 64ull * 1024 * 1024;
};

// A sandboxed Lua state for composing content: dialogue, quest steps,
// encounter dressing, tuning tables (doc 17, architecture rule 10). It has no
// io, os, package, debug, load or print to stdout. Randomness comes from named streams derived
// from the engine seed, so a script draws identically on every machine, and a
// runaway script is stopped by an instruction budget that pcall cannot swallow,
// and a script that allocates too much gets a memory error. Pattern matching
// inside one string.find call is a single C call, so it is not interruptible.
// Errors are returned, never thrown.
//
// Lua 5.4 seeds its string hash per process, so the order of pairs() over
// string keys can differ between runs. Scripts that need an order use
// ichor.sorted_pairs.
class ScriptHost {
public:
    explicit ScriptHost(std::uint64_t seed, ScriptLimits limits = {});
    ~ScriptHost();
    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    // Runs a chunk; its globals persist, so running a chunk again replaces
    // the functions it defines. That is hot reload.
    bool run(std::string_view chunkName, std::string_view code);
    bool runFile(const std::filesystem::path& path);

    bool has(std::string_view function) const;
    bool call(std::string_view function, const std::vector<ScriptValue>& args = {}, ScriptValue* result = nullptr);

    // A native function scripts can call as ichor.<name>. Arguments arrive as
    // plain values (tables and functions become empty), and whatever the
    // function throws becomes a Lua error naming it. It runs under the
    // calling script's instruction budget.
    using NativeFunction = std::function<ScriptValue(const std::vector<ScriptValue>&)>;
    void bind(std::string_view name, NativeFunction function);

    const std::string& lastError() const { return error_; }

    // Where ichor.log and ichor.warn go; the engine log by default.
    enum class Level { Info, Warn };
    using LogSink = std::function<void(Level, const std::string&)>;
    void setLogSink(LogSink sink);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string error_;
};

}
