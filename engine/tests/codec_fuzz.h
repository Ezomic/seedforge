#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Shared byte-mutation fuzzing for every hostile-input codec (SEED-423).
//
// A codec under test is wrapped as a DecodeFn: it takes the mutated bytes and returns nullopt when
// the decoder rejected them, or the canonical re-encoding of whatever it decoded. fuzzCodec() then
// truncates the valid buffer at every offset, flips every bit and rewrites, inserts and deletes
// bytes in a bounded window, and runs seeded random multi-byte mutations. For every mutated buffer:
//   - the decoder must not throw, and must not request an allocation beyond the budget (a hook in
//     codec_fuzz_alloc_hook.cpp refuses the request and records it, so a hostile length never
//     really allocates);
//   - if the decoder accepts, the re-encoding must equal the mutated bytes (the codec is canonical,
//     so a different valid value is fine). Equal to the original bytes is only accepted with
//     allowNormalising, a prefix of the mutated bytes only with allowTornPrefix, and any other
//     re-encoding only with allowNonCanonical, which still requires the re-encoding to be a fixed
//     point (it decodes and re-encodes to itself), so a codec that drifts on every pass is caught.
// Crashes and sanitizer reports are caught by the ASan/UBSan CI job running the same tests.

namespace ichor::fuzz {

using Bytes = std::vector<std::uint8_t>;
using Canonical = std::optional<Bytes>;
using DecodeFn = std::function<Canonical(std::span<const std::uint8_t>)>;

namespace detail {

struct AllocState {
    bool active = false;
    std::size_t maxSingle = 0;
    std::size_t maxTotal = 0;
    std::size_t total = 0;
    std::size_t worst = 0;
    bool violated = false;
};

AllocState& allocState();

}

struct Options {
    std::size_t window = 192;           // bytes mutated at the start and again at the end of the buffer
    std::size_t randomMutations = 3000; // seeded multi-byte mutations
    std::uint64_t seed = 0x5eedf00dull;
    std::size_t maxSingleAlloc = std::size_t{4} << 20; // one request above this is a violation
    std::size_t maxTotalAlloc = std::size_t{16} << 20; // bytes requested over one decode (churn, not live bytes)
    bool allowNormalising = false;
    bool allowTornPrefix = false;
    bool allowNonCanonical = false;
    std::size_t maxFailuresListed = 8;
};

struct Report {
    std::size_t cases = 0;
    std::size_t accepted = 0;
    std::size_t rejected = 0;
    std::size_t failureCount = 0;
    std::vector<std::string> failures;
    bool ok() const { return failureCount == 0; }
};

class AllocGuard {
public:
    AllocGuard(std::size_t maxSingle, std::size_t maxTotal)
    {
        auto& s = detail::allocState();
        s = {};
        s.maxSingle = maxSingle;
        s.maxTotal = maxTotal;
        s.active = true;
    }
    ~AllocGuard() { detail::allocState().active = false; }
    AllocGuard(const AllocGuard&) = delete;
    AllocGuard& operator=(const AllocGuard&) = delete;

    bool violated() const { return detail::allocState().violated; }
    std::size_t worst() const { return detail::allocState().worst; }
};

namespace detail {

inline std::uint64_t mix(std::uint64_t& s)
{
    s += 0x9e3779b97f4a7c15ull;
    std::uint64_t z = s;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

inline std::string describe(const Bytes& b)
{
    static constexpr char hex[] = "0123456789abcdef";
    std::string out = std::to_string(b.size()) + "B:";
    const std::size_t shown = std::min<std::size_t>(b.size(), 24);
    for (std::size_t i = 0; i < shown; ++i) {
        out += hex[b[i] >> 4];
        out += hex[b[i] & 15];
    }
    if (b.size() > shown) out += "...";
    return out;
}

inline bool isPrefix(const Bytes& prefix, const Bytes& of)
{
    return prefix.size() <= of.size() && std::equal(prefix.begin(), prefix.end(), of.begin());
}

inline std::vector<std::size_t> windowOffsets(std::size_t size, std::size_t window)
{
    std::vector<std::size_t> offsets;
    if (size <= window * 2) {
        for (std::size_t i = 0; i < size; ++i) offsets.push_back(i);
        return offsets;
    }
    for (std::size_t i = 0; i < window; ++i) offsets.push_back(i);
    const std::size_t middle = size - window * 2;
    for (std::size_t i = 1; i <= 32; ++i) offsets.push_back(window + middle * i / 33);
    for (std::size_t i = size - window; i < size; ++i) offsets.push_back(i);
    return offsets;
}

class Runner {
public:
    Runner(const Bytes& valid, const DecodeFn& decode, const Options& options, Report& report)
        : valid_(valid), decode_(decode), options_(options), report_(report)
    {
    }

    void run(const Bytes& mutated, const char* kind, std::size_t where)
    {
        if (mutated == valid_) return;
        ++report_.cases;
        const Attempt attempt = guarded(mutated);
        std::string problem;
        if (attempt.overBudget) {
            problem = "requested " + std::to_string(attempt.worst) + " bytes";
        } else if (!attempt.error.empty()) {
            problem = attempt.error;
        }
        const Canonical& result = attempt.result;
        if (problem.empty()) problem = judge(result, mutated);
        if (problem.empty()) {
            (result ? report_.accepted : report_.rejected) += 1;
            return;
        }
        ++report_.failureCount;
        if (report_.failures.size() < options_.maxFailuresListed) {
            report_.failures.push_back(std::string(kind) + "@" + std::to_string(where) + " " + problem + " input " + describe(mutated));
        }
    }

private:
    struct Attempt {
        Canonical result;
        bool overBudget = false;
        std::size_t worst = 0;
        std::string error;
    };

    // The guard is off again before any message is built, so formatting cannot trip the budget.
    Attempt guarded(const Bytes& input) const
    {
        Attempt attempt;
        const char* thrown = nullptr;
        {
            AllocGuard guard(options_.maxSingleAlloc, options_.maxTotalAlloc);
            try {
                attempt.result = decode_(std::span<const std::uint8_t>(input));
            } catch (const std::bad_alloc&) {
                thrown = "bad_alloc";
            } catch (const std::exception&) {
                thrown = "threw";
            } catch (...) {
                thrown = "threw a non-exception";
            }
            attempt.overBudget = guard.violated();
            attempt.worst = guard.worst();
        }
        if (thrown && !attempt.overBudget) attempt.error = thrown;
        return attempt;
    }

    std::string judge(const Canonical& result, const Bytes& mutated) const
    {
        if (!result) return {};
        if (*result == mutated) return {};
        if (options_.allowNormalising && *result == valid_) return {};
        if (options_.allowTornPrefix && isPrefix(*result, mutated)) return {};
        if (options_.allowNonCanonical) {
            const Attempt again = guarded(*result);
            if (again.overBudget || !again.error.empty()) return "re-decoding its own re-encoding failed";
            if (again.result && *again.result == *result) return {};
            return "accepted but its re-encoding " + describe(*result) + " is not a fixed point";
        }
        return "accepted but re-encodes to " + describe(*result);
    }

    const Bytes& valid_;
    const DecodeFn& decode_;
    const Options& options_;
    Report& report_;
};

}

inline Report fuzzCodec(const Bytes& valid, const DecodeFn& decode, const Options& options = {})
{
    Report report;
    detail::Runner runner(valid, decode, options, report);

    const Canonical baseline = decode(std::span<const std::uint8_t>(valid));
    if (!baseline || *baseline != valid) {
        ++report.failureCount;
        report.failures.push_back("the valid buffer does not decode and re-encode to itself: " + detail::describe(valid));
        return report;
    }

    for (std::size_t n = 0; n < valid.size(); ++n) {
        runner.run(Bytes(valid.begin(), valid.begin() + static_cast<std::ptrdiff_t>(n)), "truncate", n);
    }

    std::vector<std::size_t> offsets = detail::windowOffsets(valid.size(), options.window);
    std::sort(offsets.begin(), offsets.end());
    offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
    for (const std::size_t at : offsets) {
        for (int bit = 0; bit < 8; ++bit) {
            Bytes m = valid;
            m[at] ^= static_cast<std::uint8_t>(1u << bit);
            runner.run(m, "bitflip", at * 8 + static_cast<std::size_t>(bit));
        }
        for (const std::uint8_t value : {std::uint8_t{0x00}, std::uint8_t{0xFF}, static_cast<std::uint8_t>(valid[at] + 1), static_cast<std::uint8_t>(valid[at] - 1)}) {
            Bytes m = valid;
            m[at] = value;
            runner.run(m, "byteset", at);
        }
        for (const std::uint8_t value : {std::uint8_t{0x00}, std::uint8_t{0xFF}, std::uint8_t{0x7F}}) {
            Bytes m = valid;
            m.insert(m.begin() + static_cast<std::ptrdiff_t>(at), value);
            runner.run(m, "insert", at);
        }
        Bytes m = valid;
        m.erase(m.begin() + static_cast<std::ptrdiff_t>(at));
        runner.run(m, "delete", at);
    }

    for (const std::uint8_t value : {std::uint8_t{0x00}, std::uint8_t{0xFF}, std::uint8_t{0x7F}}) {
        Bytes appended = valid;
        appended.push_back(value);
        runner.run(appended, "append", valid.size());
    }
    Bytes appended;
    appended.assign(valid.begin(), valid.end());
    appended.insert(appended.end(), valid.begin(), valid.end());
    runner.run(appended, "double", valid.size());

    std::uint64_t state = options.seed;
    for (std::size_t i = 0; i < options.randomMutations && !valid.empty(); ++i) {
        Bytes m = valid;
        const std::uint64_t edits = 1 + detail::mix(state) % 4;
        for (std::uint64_t e = 0; e < edits; ++e) {
            const std::size_t at = static_cast<std::size_t>(detail::mix(state) % (m.empty() ? 1 : m.size()));
            switch (detail::mix(state) % 4) {
            case 0:
                if (!m.empty()) m[at] = static_cast<std::uint8_t>(detail::mix(state));
                break;
            case 1:
                if (!m.empty()) m[at] ^= static_cast<std::uint8_t>(1u << (detail::mix(state) % 8));
                break;
            case 2:
                m.insert(m.begin() + static_cast<std::ptrdiff_t>(at), static_cast<std::uint8_t>(detail::mix(state)));
                break;
            default:
                if (!m.empty()) m.erase(m.begin() + static_cast<std::ptrdiff_t>(at));
                break;
            }
        }
        runner.run(m, "random", i);
    }
    return report;
}

inline std::string summary(const Report& report)
{
    std::string out = std::to_string(report.failureCount) + " of " + std::to_string(report.cases) + " mutations failed";
    for (const std::string& f : report.failures) out += "\n  " + f;
    return out;
}

}
