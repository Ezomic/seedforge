#include "ichor/script/script_host.h"

#include "ichor/core/log.h"
#include "ichor/core/rng.h"

#include <sol/sol.hpp>

#include <fstream>
#include <iterator>
#include <map>

namespace ichor {

namespace {

constexpr const char* kBootstrap = R"lua(
function ichor.sorted_pairs(t)
    local keys = {}
    for k in pairs(t) do keys[#keys + 1] = k end
    table.sort(keys, function(a, b)
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
)lua";

struct Budget {
    std::uint64_t limit = 0;
    std::uint64_t used = 0;
};

constexpr int kHookStride = 1000;

// The hook finds its budget through the registry, since lua_Hook carries no user data.
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
    budget->used += kHookStride;
    if (budget->used > budget->limit) {
        luaL_error(state, "instruction budget of %d exceeded", static_cast<int>(budget->limit));
    }
}

}

struct ScriptHost::Impl {
    sol::state lua;
    Rng rng;
    std::map<std::string, Rng, std::less<>> streams;
    Budget budget;
    LogSink sink;

    Impl(std::uint64_t seed, std::uint64_t limit)
        : rng(seed)
    {
        budget.limit = limit;
    }

    Rng& stream(const std::string& name)
    {
        const auto found = streams.find(name);
        if (found != streams.end()) {
            return found->second;
        }
        return streams.emplace(name, rng.derive(name)).first->second;
    }
};

ScriptHost::ScriptHost(std::uint64_t seed, std::uint64_t instructionBudget)
    : impl_(std::make_unique<Impl>(seed, instructionBudget))
{
    sol::state& lua = impl_->lua;
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math, sol::lib::utf8, sol::lib::coroutine);
    for (const char* name : {"dofile", "loadfile", "load", "loadstring", "require", "collectgarbage"}) {
        lua[name] = sol::nil;
    }
    sol::table math = lua["math"];
    math["random"] = sol::nil;
    math["randomseed"] = sol::nil;

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
        const auto span = static_cast<std::uint64_t>(high - low) + 1;
        return low + static_cast<std::int64_t>(impl->stream(stream).nextU64() % span);
    };
    lua.safe_script(kBootstrap, sol::script_pass_on_error);
}

ScriptHost::~ScriptHost() = default;

void ScriptHost::setLogSink(LogSink sink)
{
    impl_->sink = std::move(sink);
}

namespace {

// Arms the instruction budget for the duration of one entry into Lua.
class BudgetScope {
public:
    BudgetScope(lua_State* state, Budget& budget)
        : state_(state)
    {
        budget.used = 0;
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
    BudgetScope scope(impl_->lua.lua_state(), impl_->budget);
    const sol::load_result loaded = impl_->lua.load(code, std::string(chunkName));
    if (!loaded.valid()) {
        const sol::error failure = loaded;
        error_ = failure.what();
        return false;
    }
    const sol::protected_function_result result = loaded.get<sol::protected_function>()();
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
