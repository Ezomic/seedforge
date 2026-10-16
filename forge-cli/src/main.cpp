#include <forgecli/forge.h>

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
    std::fputs("usage: ichor_forge_cli --seed N [--seed-end M] [--size S] [--phase a,b] [--out DIR] [--list]\n"
               "  --seed-end   last seed of an inclusive batch range (default: --seed)\n"
               "  --size       square grid size (default 256)\n"
               "  --phase      comma separated phases (default: all)\n"
               "  --out        write PNGs and manifest.json here; without it the manifest goes to stdout\n",
        stderr);
}

}

int main(int argc, char** argv)
{
    forgecli::RunOptions options;
    bool haveSeed = false;
    bool haveEnd = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--list") {
            for (const auto& p : forgecli::phaseRegistry()) {
                std::printf("%.*s\t%.*s\n", static_cast<int>(p.name.size()), p.name.data(), static_cast<int>(p.description.size()), p.description.data());
            }
            return 0;
        }
        if (i + 1 >= argc) {
            usage();
            return 2;
        }
        const std::string_view value = argv[++i];
        std::uint64_t n = 0;
        if (arg == "--seed" && parseUnsigned(value, n)) {
            options.seedBegin = n;
            haveSeed = true;
        } else if (arg == "--seed-end" && parseUnsigned(value, n)) {
            options.seedEnd = n;
            haveEnd = true;
        } else if (arg == "--size" && parseUnsigned(value, n) && n <= 1000000) {
            options.size = static_cast<int>(n);
        } else if (arg == "--out") {
            options.outDir = value;
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
    if (!haveSeed) {
        usage();
        return 2;
    }
    if (!haveEnd) options.seedEnd = options.seedBegin;

    const forgecli::RunResult result = forgecli::runForge(options);
    if (!result.error.empty()) {
        std::fprintf(stderr, "forge_cli: %s\n", result.error.c_str());
        return 1;
    }
    if (options.outDir.empty()) std::fputs(result.manifest.c_str(), stdout);
    int failed = 0;
    for (const auto& s : result.seeds) {
        if (!s.error.empty()) {
            std::fprintf(stderr, "seed %llu failed: %s\n", static_cast<unsigned long long>(s.seed), s.error.c_str());
            ++failed;
        }
    }
    return failed ? 1 : 0;
}
