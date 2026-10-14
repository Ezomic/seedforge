#pragma once

#include "ichor/data/axiom.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ichor {

// One problem found by ConflictList::checkSet. `first` and `second` are
// positions in the checked set, first < second.
struct SetViolation {
    enum class Kind { Blocked, Repeated };
    std::size_t first = 0;
    std::size_t second = 0;
    Kind kind = Kind::Blocked;
};

// Doc 01 rule 3: the hard blocklist of axiom pairs that must never coexist.
// data/conflicts/conflicts.json is {"pairs": [["id", "id"], ...]}. Pairs are
// unordered and validated against the axiom pool. Loading is all or nothing:
// on any problem the previous list is kept and problems() lists every one.
class ConflictList {
public:
    bool loadDataRoot(const std::filesystem::path& dataRoot, const AxiomSet& pool);
    bool loadText(std::string_view label, std::string_view text, const AxiomSet& pool);

    const std::vector<std::string>& problems() const { return problems_; }
    std::size_t size() const { return pairs_.size(); }

    // Symmetric. An axiom never coexists with itself. Binary search over the sorted pairs.
    bool mayCoexist(std::string_view a, std::string_view b) const;
    bool mayCoexist(const Axiom& a, const Axiom& b) const { return mayCoexist(a.id, b.id); }

    // Every blocked pair and every id that appears twice, in set order.
    std::vector<SetViolation> checkSet(const std::vector<std::string>& ids) const;
    std::vector<SetViolation> checkAxioms(const std::vector<const Axiom*>& axioms) const;

    // Sorted pairs, each written lower id first; independent of source layout and order.
    std::string canonicalJson() const;
    std::uint64_t digest() const;

private:
    std::vector<std::pair<std::string, std::string>> pairs_;
    std::vector<std::string> problems_;
};

}
