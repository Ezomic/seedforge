#include "ichor/stage0/fight_report.h"
#include "ichor/stage0/session_dashboard.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

namespace {

constexpr std::uintmax_t kMaxLogBytes = 64ull * 1024 * 1024;

// A missing or oversized sidecar leaves `out` empty; the table then falls back to one unlabelled session.
void readLedger(const std::filesystem::path& path, std::string& out)
{
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    std::ifstream in(path, std::ios::binary);
    if (!error && size <= kMaxLogBytes && in) {
        out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
}

}

int main(int argc, char** argv)
{
    const bool table = argc == 3 && std::string_view(argv[2]) == "--table";
    if (argc != 2 && !table) {
        std::fputs("usage: stage0_report fights.csv [--table]\n", stderr);
        return 2;
    }
    const std::filesystem::path path = argv[1];
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error) {
        std::fprintf(stderr, "stage0_report: cannot read %s\n", argv[1]);
        return 1;
    }
    if (size > kMaxLogBytes) {
        std::fprintf(stderr, "stage0_report: %s is larger than %llu bytes\n", argv[1], static_cast<unsigned long long>(kMaxLogBytes));
        return 1;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "stage0_report: cannot open %s\n", argv[1]);
        return 1;
    }
    const std::string csv((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto rows = ichor::stage0::parseFightLog(csv);
    if (table) {
        std::filesystem::path sidecar = path;
        sidecar.replace_extension(".sessions.csv");
        std::string ledger;
        readLedger(sidecar, ledger);
        std::fputs(ichor::stage0::formatDashboard(ichor::stage0::buildDashboard(rows, ledger)).c_str(), stdout);
        return 0;
    }
    if (rows.empty() && !csv.empty()) {
        std::fprintf(stderr, "stage0_report: no valid fight rows in %s\n", argv[1]);
    }
    std::fputs(ichor::stage0::formatReport(ichor::stage0::summarise(rows)).c_str(), stdout);
    return 0;
}
