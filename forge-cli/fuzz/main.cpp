#include <forgecli/fuzz.h>

#include <charconv>
#include <cstdio>
#include <string>
#include <string_view>

namespace {

bool parseUnsigned(std::string_view text, std::uint64_t& out)
{
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc() && end == text.data() + text.size();
}

void usage()
{
    std::fputs("usage: ichor_forge_fuzz --seed N --seed-end M [--size S] [--phase a,b]\n"
               "  --size   square grid size (default 128, tectonics needs at least 16)\n"
               "  --phase  comma separated phases (default: all)\n"
               "exit code 1 when any seed violates a rule\n",
        stderr);
}

}

int main(int argc, char** argv)
{
    forgecli::FuzzOptions options;
    bool haveBegin = false;
    bool haveEnd = false;
    for (int i = 1; i < argc; i += 2) {
        const std::string_view arg = argv[i];
        if (i + 1 >= argc) {
            usage();
            return 2;
        }
        const std::string_view value = argv[i + 1];
        std::uint64_t n = 0;
        if (arg == "--seed" && parseUnsigned(value, n)) {
            options.seedBegin = n;
            haveBegin = true;
        } else if (arg == "--seed-end" && parseUnsigned(value, n)) {
            options.seedEnd = n;
            haveEnd = true;
        } else if (arg == "--size" && parseUnsigned(value, n) && n <= 4096) {
            options.size = static_cast<int>(n);
        } else if (arg == "--phase") {
            std::size_t start = 0;
            while (start <= value.size()) {
                const std::size_t comma = value.find(',', start);
                const std::size_t stop = comma == std::string_view::npos ? value.size() : comma;
                options.phases.emplace_back(value.substr(start, stop - start));
                start = stop + 1;
            }
        } else {
            usage();
            return 2;
        }
    }
    if (!haveBegin || !haveEnd) {
        usage();
        return 2;
    }
    const forgecli::FuzzResult result = forgecli::runFuzz(options);
    if (!result.error.empty()) {
        std::fprintf(stderr, "forge_fuzz: %s\n", result.error.c_str());
        return 2;
    }
    std::fputs(forgecli::fuzzSummary(options, result).c_str(), stdout);
    return result.violations.empty() ? 0 : 1;
}
