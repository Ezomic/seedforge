#pragma once

#include "ichor/data/axiom.h"
#include "ichor/data/data_set.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// One declared reference: `field` of every record in `source` names records of `target`.
// `source` is a DataSet table, or "axioms". For axioms `field` is "conflicts" or
// "<block>.<key>"; a list contributes each word and a word -> weight map each key.
// `target` is a DataSet table or "axioms".
struct RefRule {
    std::string_view source;
    std::string_view field;
    std::string_view target;
};

// The shipped reference list. The nested documents (conflicts/*.json pairs and
// synergies/*.json axiom lists) always point at "axioms" and are checked separately.
const std::vector<RefRule>& referenceRules();

struct DanglingRef {
    std::string file;   // data-root-relative
    std::string record; // record id, or the pair / synergy for the nested documents
    std::string field;
    std::string value;  // the id that does not exist
    std::string target;
};

// "<file>: <record>: <field>: no <target> record \"<value>\"".
std::string describe(const DanglingRef& ref);

struct XrefReport {
    std::vector<DanglingRef> dangling; // every one, sorted by file, record, field, value
    // Rules whose target table has no records at all yet, listed as "source.field -> target".
    // They are not checked; the day the table appears they are enforced.
    std::vector<std::string> unchecked;
    // Nested documents (conflicts/*.json, synergies/*.json) that could not be read or are not the expected shape, so their references could not be followed.
    std::vector<std::string> unreadable;
    std::size_t checked = 0;

    bool ok() const { return dangling.empty() && unreadable.empty(); }
};

XrefReport checkReferences(const DataSet& data, const AxiomSet& axioms, const std::filesystem::path& dataRoot);
XrefReport checkReferences(const DataSet& data, const AxiomSet& axioms, const std::filesystem::path& dataRoot, const std::vector<RefRule>& rules);

}
