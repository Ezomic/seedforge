#include "ichor/script/script_host.h"

#include "ichor/core/log.h"
#include "ichor/core/rng.h"

#include <sol/sol.hpp>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>

namespace ichor {

namespace {

constexpr const char* kBootstrap = R"lua(
local pairs, sort, type, tostring, concat, select = pairs, table.sort, type, tostring, table.concat, select
local setmetatable, rawget, error, log = setmetatable, rawget, error, ichor.log

function ichor.sorted_pairs(t)
    local keys = {}
    for k in pairs(t) do keys[#keys + 1] = k end
    sort(keys, function(a, b)
        if type(a) == type(b) then return a < b end
        return type(a) < type(b)
    end)
    local i = 0
    return function()
        i = i + 1
        local k = keys[i]
        if k ~= nil then return k, t[k] end
    end
end

function print(...)
    local parts = {}
    for i = 1, select('#', ...) do parts[i] = tostring((select(i, ...))) end
    log(concat(parts, '\t'))
end

-- Finalizers would run outside any instruction budget.
local guarded = setmetatable
_G.setmetatable = function(t, mt)
    if mt ~= nil and (rawget(mt, '__gc') ~= nil or rawget(mt, '__close') ~= nil) then
        error('__gc and __close are not allowed in scripts', 2)
    end
    return guarded(t, mt)
end
)lua";

constexpr int kHookStride = 1000;
constexpr std::size_t kMaxStreams = 1024;

struct Budget {
    std::uint64_t limit = 0;
    std::uint64_t used = 0;
    // Once set, every further instruction raises, so a script cannot swallow
    // the error with pcall and carry on.
    bool exceeded = false;
};

struct Memory {
    std::size_t used = 0;
    std::size_t limit = 0;
};

// The hook finds its budget through a slot, since lua_Hook carries no user data.
Budget*& budgetSlot()
{
    thread_local Budget* slot = nullptr;
    return slot;
}

void budgetHook(lua_State* state, lua_Debug*)
{
    Budget* budget = budgetSlot();
    if (budget == nullptr) {
        return;
    }
    if (!budget->exceeded) {
        budget->used += kHookStride;
        if (budget->used <= budget->limit) {
            return;
        }
        budget->exceeded = true;
        lua_sethook(state, budgetHook, LUA_MASKCOUNT, 1);
    }
    luaL_error(state, "instruction budget of %I exceeded", static_cast<lua_Integer>(budget->limit));
}

void* limitedAlloc(void* userData, void* block, std::size_t oldSize, std::size_t newSize)
{
    auto* memory = static_cast<Memory*>(userData);
    const std::size_t held = block != nullptr ? oldSize : 0;
    if (newSize == 0) {
        memory->used -= held;
        std::free(block);
        return nullptr;
    }
    if (newSize > held && memory->used - held + newSize > memory->limit) {
        return nullptr;
    }
    void* grown = std::realloc(block, newSize);
    if (grown != nullptr) {
        memory->used = memory->used - held + newSize;
    }
    return grown;
}

}

struct ScriptHost::Impl {
    Memory memory;
    sol::state lua;
    Rng rng;
    std::map<std::string, Rng, std::less<>> streams;
    Budget budget;
    LogSink sink;

    Impl(std::uint64_t seed, const ScriptLimits& limits)
        : memory{0, static_cast<std::size_t>(limits.memoryBytes)}
        , lua(sol::default_at_panic, limitedAlloc, &memory)
        , rng(seed)
    {
        budget.limit = limits.instructions;
    }

    Rng& stream(const std::string& name)
    {
        const auto found = streams.find(name);
        if (found != streams.end()) {
            return found->second;
        }
        if (streams.size() >= kMaxStreams) {
            throw std::runtime_error("too many random streams");
        }
        return streams.emplace(name, rng.derive(name)).first->second;
    }
};

ScriptHost::ScriptHost(std::uint64_t seed, ScriptLimits limits)
    : impl_(std::make_unique<Impl>(seed, limits))
{
    sol::state& lua = impl_->lua;
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math, sol::lib::utf8, sol::lib::coroutine);
    for (const char* name : {"dofile", "loadfile", "load", "loadstring", "require", "collectgarbage", "warn"}) {
        lua[name] = sol::nil;
    }
    sol::table math = lua["math"];
    math["random"] = sol::nil;
    math["randomseed"] = sol::nil;
    sol::table string = lua["string"];
    string["dump"] = sol::nil;

    impl_->sink = [](Level level, const std::string& message) {
        logf(level == Level::Warn ? LogLevel::Warn : LogLevel::Info, "lua: {}", message);
    };

    sol::table api = lua.create_named_table("ichor");
    Impl* impl = impl_.get();
    api["log"] = [impl](const std::string& message) { impl->sink(Level::Info, message); };
    api["warn"] = [impl](const std::string& message) { impl->sink(Level::Warn, message); };
    api["random"] = [impl](const std::string& stream) { return static_cast<double>(impl->stream(stream).nextFloat()); };
    api["random_range"] = [impl](const std::string& stream, std::int64_t low, std::int64_t high) {
        if (high < low) {
            std::swap(low, high);
        }
        Rng& rng = impl->stream(stream);
        // Unsigned throughout: the span of the full int64 range is 2^64, which wraps to 0.
        const std::uint64_t span = static_cast<std::uint64_t>(high) - static_cast<std::uint64_t>(low) + 1;
        std::uint64_t draw = rng.nextU64();
        if (span != 0) {
            const std::uint64_t threshold = (0 - span) % span;
            while (draw < threshold) {
                draw = rng.nextU64();
            }
            draw %= span;
        }
        return static_cast<std::int64_t>(static_cast<std::uint64_t>(low) + draw);
    };
    const sol::protected_function_result boot = lua.safe_script(kBootstrap, sol::script_pass_on_error);
    if (!boot.valid()) {
        const sol::error failure = boot;
        logf(LogLevel::Error, "lua bootstrap failed: {}", failure.what());
    }
}

ScriptHost::~ScriptHost() = default;

void ScriptHost::setLogSink(LogSink sink)
{
    impl_->sink = std::move(sink);
}

void ScriptHost::bind(std::string_view name, NativeFunction function)
{
    sol::table api = impl_->lua["ichor"];
    api[std::string(name)] = [function = std::move(function)](sol::variadic_args args, sol::this_state state) -> sol::object {
        // Lua is a C build, so a memory error longjmps over C++ frames. The
        // argument vector therefore dies before anything that can allocate in
        // Lua runs, and arguments are read straight off the stack.
        const ScriptValue result = [&] {
            std::vector<ScriptValue> values;
            values.reserve(args.size());
            for (const auto& arg : args) {
                switch (arg.get_type()) {
                case sol::type::boolean: values.emplace_back(arg.get<bool>()); break;
                case sol::type::number: values.emplace_back(arg.get<double>()); break;
                case sol::type::string: values.emplace_back(arg.get<std::string>()); break;
                default: values.emplace_back(std::monostate{}); break;
                }
            }
            return function(values);
        }();
        return std::visit(
            [&](const auto& value) -> sol::object {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    return sol::make_object(state, sol::nil);
                } else {
                    return sol::make_object(state, value);
                }
            },
            result);
    };
}

namespace {

// Arms the instruction budget for one entry into Lua. Entering again while a
// scope is live would reset the hook's countdown on every entry and let a
// script that calls back into the host starve its own accounting, so run and
// call refuse reentry instead.
class BudgetScope {
public:
    BudgetScope(lua_State* state, Budget& budget)
        : state_(state)
    {
        budget.used = 0;
        budget.exceeded = false;
        budgetSlot() = &budget;
        lua_sethook(state_, budgetHook, LUA_MASKCOUNT, kHookStride);
    }
    ~BudgetScope()
    {
        lua_sethook(state_, nullptr, 0, 0);
        budgetSlot() = nullptr;
    }

private:
    lua_State* state_;
};

}

bool ScriptHost::run(std::string_view chunkName, std::string_view code)
{
    error_.clear();
    if (budgetSlot() == &impl_->budget) {
        error_ = "reentrant script call";
        return false;
    }
    BudgetScope scope(impl_->lua.lua_state(), impl_->budget);
    // Text only: Lua does not verify bytecode, and a crafted chunk could corrupt memory.
    const sol::load_result loaded = impl_->lua.load(code, std::string(chunkName), sol::load_mode::text);
    if (!loaded.valid()) {
        const sol::error failure = loaded;
        error_ = failure.what();
        return false;
    }
    const sol::protected_function_result result = loaded.get<sol::protected_function>()();
    if (impl_->budget.exceeded) {
        error_ = "instruction budget of " + std::to_string(impl_->budget.limit) + " exceeded";
        return false;
    }
    if (!result.valid()) {
        const sol::error failure = result;
        error_ = failure.what();
        return false;
    }
    return true;
}

bool ScriptHost::runFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error_ = "cannot read " + path.string();
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return run(path.filename().string(), text);
}

bool ScriptHost::has(std::string_view function) const
{
    const sol::object value = impl_->lua[std::string(function)];
    return value.get_type() == sol::type::function;
}

bool ScriptHost::call(std::string_view function, const std::vector<ScriptValue>& args, ScriptValue* out)
{
    error_.clear();
    if (budgetSlot() == &impl_->budget) {
        error_ = "reentrant script call";
        return false;
    }
    sol::protected_function fn = impl_->lua[std::string(function)];
    if (!fn.valid()) {
        error_ = "no function named " + std::string(function);
        return false;
    }
    BudgetScope scope(impl_->lua.lua_state(), impl_->budget);
    std::vector<sol::object> converted;
    converted.reserve(args.size());
    for (const ScriptValue& arg : args) {
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    converted.push_back(sol::make_object(impl_->lua, sol::nil));
                } else {
                    converted.push_back(sol::make_object(impl_->lua, value));
                }
            },
            arg);
    }
    const sol::protected_function_result result = fn(sol::as_args(converted));
    if (impl_->budget.exceeded) {
        error_ = "instruction budget of " + std::to_string(impl_->budget.limit) + " exceeded";
        return false;
    }
    if (!result.valid()) {
        const sol::error failure = result;
        error_ = failure.what();
        return false;
    }
    if (out != nullptr) {
        *out = std::monostate{};
        if (result.return_count() > 0) {
            const sol::object value = result.get<sol::object>();
            switch (value.get_type()) {
            case sol::type::boolean: *out = value.as<bool>(); break;
            case sol::type::number: *out = value.as<double>(); break;
            case sol::type::string: *out = value.as<std::string>(); break;
            default: break;
            }
        }
    }
    return true;
}

}
