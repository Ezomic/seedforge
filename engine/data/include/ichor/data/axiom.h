#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ichor {

// The five slots of doc 01, in priority order A > B > C > D > E.
enum class AxiomSlot { Cataclysm, Law, Powers, Pressure, Anomaly };

std::string_view axiomSlotName(AxiomSlot slot);
char axiomSlotLetter(AxiomSlot slot);

// A directive value: a number, a word, a list of words, or word -> weight.
using AxiomValue = std::variant<double, std::string, std::vector<std::string>, std::map<std::string, double, std::less<>>>;
using AxiomBlock = std::map<std::string, AxiomValue, std::less<>>;

struct Axiom {
    std::string id;
    AxiomSlot slot = AxiomSlot::Cataclysm;
    std::string title;
    std::vector<std::string> tags;
    std::vector<std::string> conflicts;
    // Directive blocks by name (terrain, settlement, ...). Doc 01: at least four are present.
    std::map<std::string, AxiomBlock, std::less<>> blocks;
};

// Parses and validates one axiom file. Every problem is appended to
// `problems` as "<label>: <field path>: <what is wrong>". Returns true, and
// fills `out`, only when there are no problems. Never throws on bad input.
bool parseAxiom(std::string_view label, std::string_view text, Axiom& out, std::vector<std::string>& problems);

// Key-sorted compact JSON with every number written as a double, so the same
// axiom always yields the same text whatever the source file's layout.
std::string axiomCanonicalJson(const Axiom& axiom);
std::uint64_t axiomDigest(const Axiom& axiom);

// data/axioms/**/*.json, one axiom per file. Loading is all or nothing: on any
// problem nothing is replaced and problems() lists every one of them, across
// all files, in file order.
class AxiomSet {
public:
    bool loadDirectory(const std::filesystem::path& dataRoot);

    const std::vector<std::string>& problems() const { return problems_; }
    const std::map<std::string, Axiom, std::less<>>& axioms() const { return axioms_; }
    const Axiom* find(std::string_view id) const;

    // Over every axiom's digest in id order; independent of file names and load order.
    std::uint64_t digest() const;

private:
    std::map<std::string, Axiom, std::less<>> axioms_;
    std::vector<std::string> problems_;
};

}
