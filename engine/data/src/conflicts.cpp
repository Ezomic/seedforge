#include "ichor/data/conflicts.h"

#include "ichor/core/hash.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>

namespace ichor {

namespace {

using Json = nlohmann::json;
using Pair = std::pair<std::string, std::string>;

constexpr std::uintmax_t kMaxFileBytes = 4 * 1024 * 1024;

std::string at(std::size_t index)
{
    return "pairs[" + std::to_string(index) + "]";
}

}

bool ConflictList::loadDataRoot(const std::filesystem::path& dataRoot, const AxiomSet& pool)
{
    const std::filesystem::path file = dataRoot / "conflicts" / "conflicts.json";
    const std::string label = "conflicts/conflicts.json";
    std::error_code code;
    const std::uintmax_t size = std::filesystem::file_size(file, code);
    if (code) {
        problems_ = {label + ": cannot read file: " + code.message()};
        return false;
    }
    if (size > kMaxFileBytes) {
        problems_ = {label + ": file is larger than " + std::to_string(kMaxFileBytes) + " bytes"};
        return false;
    }
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        problems_ = {label + ": cannot read file"};
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return loadText(label, text, pool);
}

bool ConflictList::loadText(std::string_view label, std::string_view text, const AxiomSet& pool)
{
    std::vector<std::string> problems;
    std::vector<Pair> pairs;
    const auto add = [&](const std::string& path, const std::string& what) { problems.push_back(std::string(label) + ": " + path + ": " + what); };
    const auto finish = [&] {
        const bool ok = problems.empty();
        problems_ = std::move(problems);
        if (ok) {
            pairs_ = std::move(pairs);
        }
        return ok;
    };

    const Json json = Json::parse(text.begin(), text.end(), nullptr, false);
    if (json.is_discarded()) {
        add("(file)", "not valid JSON");
        return finish();
    }
    if (!json.is_object()) {
        add("(file)", "must be an object");
        return finish();
    }
    for (const auto& [key, value] : json.items()) {
        if (key != "pairs") {
            add(key, "unknown key");
        }
    }
    const auto list = json.find("pairs");
    if (list == json.end()) {
        add("pairs", "missing");
        return finish();
    }
    if (!list->is_array()) {
        add("pairs", "must be an array");
        return finish();
    }

    std::map<Pair, std::size_t> seen;
    for (std::size_t i = 0; i < list->size(); ++i) {
        const Json& entry = (*list)[i];
        if (!entry.is_array() || entry.size() != 2) {
            add(at(i), "must be an array of two ids");
            continue;
        }
        bool valid = true;
        for (std::size_t side = 0; side < 2; ++side) {
            const std::string where = at(i) + "[" + std::to_string(side) + "]";
            if (!entry[side].is_string()) {
                add(where, "must be a string");
                valid = false;
            } else if (pool.find(entry[side].get_ref<const std::string&>()) == nullptr) {
                add(where, "unknown axiom \"" + entry[side].get<std::string>() + "\"");
                valid = false;
            }
        }
        if (!valid) {
            continue;
        }
        Pair pair(entry[0].get<std::string>(), entry[1].get<std::string>());
        if (pair.first == pair.second) {
            add(at(i), "an axiom cannot conflict with itself");
            continue;
        }
        if (pair.second < pair.first) {
            std::swap(pair.first, pair.second);
        }
        const auto [place, fresh] = seen.emplace(pair, i);
        if (!fresh) {
            add(at(i), "duplicate of " + at(place->second));
            continue;
        }
        pairs.push_back(std::move(pair));
    }
    std::sort(pairs.begin(), pairs.end());
    return finish();
}

bool ConflictList::mayCoexist(std::string_view a, std::string_view b) const
{
    if (a == b) {
        return false;
    }
    if (b < a) {
        std::swap(a, b);
    }
    const auto it = std::lower_bound(pairs_.begin(), pairs_.end(), std::make_pair(a, b), [](const Pair& pair, const std::pair<std::string_view, std::string_view>& key) {
        return std::pair<std::string_view, std::string_view>(pair.first, pair.second) < key;
    });
    return it == pairs_.end() || it->first != a || it->second != b;
}

std::vector<SetViolation> ConflictList::checkSet(const std::vector<std::string>& ids) const
{
    std::vector<SetViolation> found;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        for (std::size_t j = i + 1; j < ids.size(); ++j) {
            if (ids[i] == ids[j]) {
                found.push_back({i, j, SetViolation::Kind::Repeated});
            } else if (!mayCoexist(ids[i], ids[j])) {
                found.push_back({i, j, SetViolation::Kind::Blocked});
            }
        }
    }
    return found;
}

std::vector<SetViolation> ConflictList::checkAxioms(const std::vector<const Axiom*>& axioms) const
{
    std::vector<std::string> ids;
    ids.reserve(axioms.size());
    for (const Axiom* axiom : axioms) {
        ids.push_back(axiom->id);
    }
    return checkSet(ids);
}

std::string ConflictList::canonicalJson() const
{
    Json pairs = Json::array();
    for (const auto& [a, b] : pairs_) {
        pairs.push_back(Json::array({a, b}));
    }
    Json json = Json::object();
    json["pairs"] = std::move(pairs);
    return json.dump(-1, ' ', false, Json::error_handler_t::replace);
}

std::uint64_t ConflictList::digest() const
{
    return hash64(canonicalJson());
}

}
