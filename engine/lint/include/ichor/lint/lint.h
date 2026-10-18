#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ichor::lint {

struct Problem {
    std::string validator;
    std::string file; // data-root-relative where the validator knows it
    std::string id;   // record id where the validator reports one separately, else empty
    std::string message;
};

// "[validator] file: id: message"; the empty parts are left out.
std::string describe(const Problem& problem);

struct Validator {
    std::string_view name;
    std::string_view summary;
    // Appends every problem it finds under `dataRoot`. Never throws on bad data.
    void (*run)(const std::filesystem::path& dataRoot, std::vector<Problem>& out);
};

// Adding a validator is one entry in this list (src/lint.cpp). Order is the report order.
const std::vector<Validator>& validators();

struct ValidatorResult {
    std::string name;
    std::size_t problems = 0;
};

struct Report {
    std::vector<ValidatorResult> results; // one per validator that ran, in registry order
    std::vector<Problem> problems;        // by registry order, then file, id, message
    std::string error;                    // a bad data root or an unknown validator name; nothing ran

    bool ok() const { return error.empty() && problems.empty(); }
};

// Runs the named validators, or all when `only` is empty.
Report runLint(const std::filesystem::path& dataRoot, const std::vector<std::string>& only = {});

}
