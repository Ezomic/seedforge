#pragma once

#include "codec_fuzz.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

// Hostile-input mutation for decoders that read text (JSON, CSV, key=value) or files (SEED-463).
//
// A parser under test is wrapped as a ParseFn that returns whether it accepted the text. mutateText()
// feeds it truncations at many offsets, seeded byte edits, every number replaced by an extreme one,
// every string replaced by a very long one, and deep nesting. For each input the parser must not
// throw and must not request an allocation beyond the budget (the hook in
// test_codec_fuzz_alloc_hook.cpp refuses the request and records it). Crashes, overflow and other
// undefined behaviour are caught by the ASan and UBSan CI job running the same tests.

namespace ichor::hostile {

using ParseFn = std::function<bool(std::string_view)>;

struct TextOptions {
    std::size_t randomMutations = 1500;
    std::size_t truncations = 160;
    std::size_t maxNumberSites = 48;
    std::uint64_t seed = 0x5eedf00dull;
    std::size_t maxSingleAlloc = std::size_t{8} << 20;
    std::size_t maxTotalAlloc = std::size_t{64} << 20;
    std::size_t nesting = 20000;
};

struct TextReport {
    std::size_t cases = 0;
    std::size_t accepted = 0;
    std::size_t rejected = 0;
    std::size_t failureCount = 0;
    std::vector<std::string> failures;
    bool ok() const { return failureCount == 0; }
};

namespace detail {

inline std::string preview(const std::string& text)
{
    std::string out = std::to_string(text.size()) + "B:";
    for (std::size_t i = 0; i < text.size() && i < 60; ++i) out += text[i] >= 32 && text[i] < 127 ? text[i] : '?';
    return out;
}

inline const std::vector<std::string>& extremeNumbers()
{
    static const std::vector<std::string> numbers = {
        "-1", "0", "99999999999999999999999999", "-99999999999999999999999999", "4294967296", "4294967295", "2147483648",
        "-2147483649", "9223372036854775808", "18446744073709551616", "1e999", "-1e999", "1e-999", "0.5e400", "-0", "0.0000001",
        "123456789012345678901234567890.5", "1e308", "5e-324"};
    return numbers;
}

struct Site {
    std::size_t begin;
    std::size_t end;
};

inline std::vector<Site> numberSites(const std::string& text, std::size_t limit)
{
    std::vector<Site> all;
    bool inString = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\\' && inString) {
            ++i;
            continue;
        }
        if (c == '"') {
            inString = !inString;
            continue;
        }
        if (inString || !((c >= '0' && c <= '9') || c == '-')) continue;
        std::size_t j = i + 1;
        while (j < text.size() && ((text[j] >= '0' && text[j] <= '9') || text[j] == '.' || text[j] == 'e' || text[j] == 'E' || text[j] == '+' || text[j] == '-')) ++j;
        all.push_back({i, j});
        i = j - 1;
    }
    std::vector<Site> picked;
    if (all.size() <= limit) return all;
    for (std::size_t k = 0; k < limit; ++k) picked.push_back(all[k * all.size() / limit]);
    return picked;
}

inline std::vector<Site> stringSites(const std::string& text, std::size_t limit)
{
    std::vector<Site> all;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '"') continue;
        std::size_t j = i + 1;
        while (j < text.size() && text[j] != '"') j += text[j] == '\\' ? 2 : 1;
        if (j >= text.size()) break;
        all.push_back({i + 1, j});
        i = j;
    }
    if (all.size() <= limit) return all;
    std::vector<Site> picked;
    for (std::size_t k = 0; k < limit; ++k) picked.push_back(all[k * all.size() / limit]);
    return picked;
}

class TextRunner {
public:
    TextRunner(const ParseFn& parse, const TextOptions& options, TextReport& report) : parse_(parse), options_(options), report_(report) {}

    void run(const std::string& input, const char* kind)
    {
        ++report_.cases;
        if (std::getenv("HOSTILE_TRACE")) std::fprintf(stderr, "%s %zu\n", kind, input.size());
        bool accepted = false;
        const char* thrown = nullptr;
        bool over = false;
        std::size_t worst = 0;
        {
            ichor::fuzz::AllocGuard guard(options_.maxSingleAlloc, options_.maxTotalAlloc);
            try {
                accepted = parse_(input);
            } catch (const std::bad_alloc&) {
                thrown = "bad_alloc";
            } catch (const std::exception&) {
                thrown = "threw";
            } catch (...) {
                thrown = "threw a non-exception";
            }
            over = guard.violated();
            worst = guard.worst();
        }
        if (over || thrown) {
            ++report_.failureCount;
            if (report_.failures.size() < 8) {
                report_.failures.push_back(std::string(kind) + " " + (over ? "requested " + std::to_string(worst) + " bytes" : std::string(thrown)) + " input " + preview(input));
            }
            return;
        }
        (accepted ? report_.accepted : report_.rejected) += 1;
    }

private:
    const ParseFn& parse_;
    const TextOptions& options_;
    TextReport& report_;
};

}

inline TextReport mutateText(const std::string& valid, const ParseFn& parse, const TextOptions& options = {})
{
    TextReport report;
    detail::TextRunner runner(parse, options, report);

    const std::size_t step = std::max<std::size_t>(1, valid.size() / std::max<std::size_t>(1, options.truncations));
    for (std::size_t n = 0; n < valid.size(); n += (n < 48 ? 1 : step)) runner.run(valid.substr(0, n), "truncate");
    runner.run(valid + valid, "double");
    runner.run(valid + std::string(1, '\0'), "append-nul");
    runner.run(std::string(1, '\0') + valid, "prepend-nul");
    runner.run(valid + "\xff\xfe garbage", "append-garbage");

    for (const detail::Site& site : detail::numberSites(valid, options.maxNumberSites)) {
        for (const std::string& value : detail::extremeNumbers()) {
            runner.run(valid.substr(0, site.begin) + value + valid.substr(site.end), "number");
        }
    }
    for (const detail::Site& site : detail::stringSites(valid, options.maxNumberSites)) {
        runner.run(valid.substr(0, site.begin) + std::string(300000, 'A') + valid.substr(site.end), "long-string");
        runner.run(valid.substr(0, site.begin) + valid.substr(site.end), "empty-string");
        runner.run(valid.substr(0, site.begin) + "\\u0000\\ud800\\udc00\\\\" + valid.substr(site.end), "escapes");
        runner.run(valid.substr(0, site.begin) + std::string("\xc3\x28\xff\xfe", 4) + valid.substr(site.end), "bad-utf8");
    }

    runner.run(std::string(options.nesting, '[') + std::string(options.nesting, ']'), "nest-array");
    runner.run(std::string(options.nesting, '['), "nest-open");
    std::string objects;
    for (std::size_t i = 0; i < options.nesting; ++i) objects += "{\"a\":";
    runner.run(objects, "nest-object");
    runner.run(std::string(1000000, ' ') + valid, "leading-space");
    runner.run("[" + std::string(200000, '1') + "]", "huge-integer");
    std::string items = "[";
    for (int i = 0; i < 100000; ++i) items += "1,";
    runner.run(items + "1]", "many-items");

    std::mt19937_64 rng(options.seed);
    for (std::size_t i = 0; i < options.randomMutations && !valid.empty(); ++i) {
        std::string m = valid;
        const std::size_t edits = 1 + rng() % 4;
        for (std::size_t e = 0; e < edits && !m.empty(); ++e) {
            const std::size_t at = rng() % m.size();
            switch (rng() % 5) {
            case 0: m[at] = static_cast<char>(rng()); break;
            case 1: m[at] = static_cast<char>(m[at] ^ (1u << (rng() % 8))); break;
            case 2: m.insert(m.begin() + static_cast<std::ptrdiff_t>(at), "{}[],:\"\\-0e."[rng() % 12]); break;
            case 3: m.erase(at, 1 + rng() % std::min<std::size_t>(8, m.size() - at)); break;
            default: {
                const std::size_t len = 1 + rng() % std::min<std::size_t>(32, m.size() - at);
                m.insert(at, m.substr(at, len));
                break;
            }
            }
        }
        runner.run(m, "random");
    }
    return report;
}

inline std::string summary(const TextReport& report)
{
    std::string out = std::to_string(report.failureCount) + " of " + std::to_string(report.cases) + " hostile inputs failed";
    for (const std::string& f : report.failures) out += "\n  " + f;
    return out;
}

inline std::filesystem::path uniqueDir(const std::string& tag)
{
    std::random_device rd;
    const std::uint64_t id = (static_cast<std::uint64_t>(rd()) << 32) ^ rd() ^ static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path dir = std::filesystem::path(ICHOR_TEST_OUTPUT) / ("hostile_" + tag + "_" + std::to_string(id));
    std::filesystem::create_directories(dir);
    return dir;
}

}
