#include "ichor/lint/axiom_coverage.h"
#include "ichor/lint/lint.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef ICHOR_DATA_DIR
#define ICHOR_DATA_DIR "data"
#endif

namespace {

void usage()
{
    std::fputs("usage: ichor_lint [--data DIR] [--only NAME]... [--list] [--coverage-out FILE]\n"
               "  Runs every data validator and prints one report. Exit 0 when clean, 1 on any problem, 2 on bad arguments.\n",
        stderr);
}

}

int main(int argc, char** argv)
{
    std::filesystem::path dataDir = ICHOR_DATA_DIR;
    std::vector<std::string> only;
    std::filesystem::path coverageOut;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        }
        if (arg == "--list") {
            for (const ichor::lint::Validator& v : ichor::lint::validators())
                std::printf("%-12s %s\n", std::string(v.name).c_str(), std::string(v.summary).c_str());
            return 0;
        }
        if (arg == "--coverage-out" && i + 1 < argc) {
            coverageOut = argv[++i];
            continue;
        }
        if ((arg == "--data" || arg == "--only") && i + 1 < argc) {
            if (arg == "--data") dataDir = argv[++i];
            else only.emplace_back(argv[++i]);
            continue;
        }
        usage();
        return 2;
    }

    if (!coverageOut.empty()) {
        std::string markdown;
        std::vector<std::string> problems;
        if (!ichor::lint::generateAxiomCoverage(dataDir, markdown, problems)) {
            for (const std::string& problem : problems) std::fprintf(stderr, "ichor_lint: %s\n", problem.c_str());
            return 1;
        }
        std::ofstream(coverageOut, std::ios::binary) << markdown;
        return 0;
    }

    const ichor::lint::Report report = ichor::lint::runLint(dataDir, only);
    if (!report.error.empty()) {
        std::fprintf(stderr, "ichor_lint: %s\n", report.error.c_str());
        return 2;
    }
    for (const ichor::lint::Problem& p : report.problems) std::printf("%s\n", ichor::lint::describe(p).c_str());
    for (const ichor::lint::ValidatorResult& r : report.results)
        std::printf("%-12s %s\n", r.name.c_str(), r.problems == 0 ? "ok" : (std::to_string(r.problems) + " problem(s)").c_str());
    return report.ok() ? 0 : 1;
}
