// Source audit for hash-order iteration (SEED-121). The same seed must give the same world, and
// iterating a std::unordered_* container visits elements in an implementation-defined order that
// differs between standard libraries, so any such loop feeding hashed state breaks cross-platform
// determinism. This scans the C++ sources for unordered containers and for loops over them.
//
// Two reviewed allowlists make every exception explicit:
//   kDeclarations: an unordered container may exist only if listed here (lookup-only use).
//   kIterations:   a loop over one may exist only if listed here with the reason it is safe.
// A new container or a new loop fails the test until someone reviews it. A stale entry also fails.
//
// Limits: a lexical scan. It sees containers by declared name (and `using` aliases), not through
// `auto` copies or functions returning one, and a name shared by an ordered container can give a
// false positive, which the allowlist then records.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Entry {
    std::string_view file;
    std::string_view name;
    std::string_view reason;
};

// Lookup-only containers (find, contains, operator[], size, clear, erase). None is iterated.
constexpr std::array<Entry, 5> kDeclarations = {{
    {"engine/assets/src/cooker.cpp", "meshIndex", "offline cooker, pointer-keyed lookup of mesh slot; never iterated"},
    {"engine/assets/src/skeleton_cook.cpp", "slotOf", "offline cooker, node-to-joint lookup; never iterated"},
    {"engine/assets/src/skeleton_cook.cpp", "indexOfName", "offline cooker, joint-name lookup; never iterated"},
    {"engine/assets/src/skeleton_cook.cpp", "jointIndex", "offline cooker, joint-name lookup; never iterated"},
    {"engine/sim/include/ichor/sim/scene.h", "index_", "entity id to entt::entity lookup in Scene; only find/emplace/erase/clear"},
}};

// Loops over an unordered container that were reviewed and cannot reach hashed state.
constexpr std::array<Entry, 0> kIterations{};

struct Finding {
    std::string file;
    std::string name;
    int line = 0;
};

bool operator<(const Finding& a, const Finding& b)
{
    return std::tie(a.file, a.name, a.line) < std::tie(b.file, b.name, b.line);
}

std::string stripCommentsAndLiterals(const std::string& src)
{
    std::string out = src;
    const std::size_t n = src.size();
    std::size_t i = 0;
    auto blank = [&](std::size_t from, std::size_t to) {
        for (std::size_t k = from; k < to && k < n; ++k) {
            if (out[k] != '\n') {
                out[k] = ' ';
            }
        }
    };
    while (i < n) {
        if (src.compare(i, 2, "//") == 0) {
            std::size_t e = src.find('\n', i);
            e = e == std::string::npos ? n : e;
            blank(i, e);
            i = e;
        } else if (src.compare(i, 2, "/*") == 0) {
            std::size_t e = src.find("*/", i + 2);
            e = e == std::string::npos ? n : e + 2;
            blank(i, e);
            i = e;
        } else if (src[i] == '"' || src[i] == '\'') {
            const char q = src[i];
            std::size_t e = i + 1;
            while (e < n && src[e] != q && src[e] != '\n') {
                e += src[e] == '\\' ? 2 : 1;
            }
            e = std::min(n, e + 1);
            blank(i + 1, e > i + 1 ? e - 1 : e);
            i = e;
        } else {
            ++i;
        }
    }
    return out;
}

int lineOf(const std::string& text, std::size_t pos)
{
    return 1 + static_cast<int>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(pos), '\n'));
}

bool isIdent(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

// Names of variables, members and parameters declared with an unordered container type.
std::vector<Finding> findDeclarations(const std::string& file, const std::string& clean)
{
    std::set<std::string> typeWords = {"unordered_map", "unordered_set", "unordered_multimap", "unordered_multiset"};
    static const std::regex alias(R"(using\s+(\w+)\s*=\s*(?:std::)?unordered_(?:multi)?(?:map|set)\b)");
    for (std::sregex_iterator it(clean.begin(), clean.end(), alias), end; it != end; ++it) {
        typeWords.insert((*it)[1].str());
    }

    std::vector<Finding> found;
    for (const std::string& word : typeWords) {
        std::size_t pos = 0;
        while ((pos = clean.find(word, pos)) != std::string::npos) {
            const std::size_t start = pos;
            pos += word.size();
            if ((start > 0 && isIdent(clean[start - 1])) || (pos < clean.size() && isIdent(clean[pos]))) {
                continue;
            }
            std::size_t p = pos;
            while (p < clean.size() && std::isspace(static_cast<unsigned char>(clean[p])) != 0) {
                ++p;
            }
            if (p < clean.size() && clean[p] == '<') {
                int depth = 0;
                for (; p < clean.size(); ++p) {
                    depth += clean[p] == '<' ? 1 : clean[p] == '>' ? -1 : 0;
                    if (depth == 0) {
                        ++p;
                        break;
                    }
                }
            }
            for (;;) {
                while (p < clean.size() && (std::isspace(static_cast<unsigned char>(clean[p])) != 0 || clean[p] == '&' || clean[p] == '*')) {
                    ++p;
                }
                if (clean.compare(p, 5, "const") == 0 && !isIdent(clean[p + 5])) {
                    p += 5;
                    continue;
                }
                break;
            }
            std::size_t e = p;
            while (e < clean.size() && isIdent(clean[e])) {
                ++e;
            }
            if (e == p || std::isdigit(static_cast<unsigned char>(clean[p])) != 0) {
                continue;
            }
            std::size_t after = e;
            while (after < clean.size() && std::isspace(static_cast<unsigned char>(clean[after])) != 0) {
                ++after;
            }
            const char next = after < clean.size() ? clean[after] : ';';
            if (next == '(' || next == ':') {
                continue;
            }
            found.push_back({file, clean.substr(p, e - p), lineOf(clean, p)});
        }
    }
    return found;
}

// Range-for, begin()/cbegin() and std::for_each over any of the given names.
std::vector<Finding> findIterations(const std::string& file, const std::string& clean, const std::set<std::string>& names)
{
    std::vector<Finding> found;
    for (const std::string& name : names) {
        const std::string n = name;
        const std::regex rangeFor(R"(\bfor\s*\([^;)]*:\s*(?:\*\s*)?[\w:.>\-\[\]]*\b)" + n + R"(\s*\))");
        const std::regex begin(R"(\b)" + n + R"(\s*(?:\.|->)\s*c?r?begin\s*\()");
        const std::regex forEach(R"(\b(?:for_each|ranges::for_each)\s*\(\s*(?:\w+::)*)" + n + R"(\b)");
        for (const std::regex* re : {&rangeFor, &begin, &forEach}) {
            for (std::sregex_iterator it(clean.begin(), clean.end(), *re), end; it != end; ++it) {
                found.push_back({file, name, lineOf(clean, static_cast<std::size_t>(it->position()))});
            }
        }
    }
    return found;
}

template <typename Entries>
bool listed(const Finding& f, const Entries& entries)
{
    return std::any_of(entries.begin(), entries.end(), [&](const Entry& e) { return e.file == f.file && e.name == f.name; });
}

std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

struct Scan {
    std::vector<Finding> declarations;
    std::vector<Finding> iterations;
    std::size_t files = 0;
};

Scan scanTree(const fs::path& root)
{
    const std::set<std::string> extensions = {".h", ".hpp", ".inl", ".cpp", ".cc", ".cxx"};
    const std::set<std::string> skipDirs = {"build", "vcpkg_installed", "tests", ".git", "third_party", "external"};
    std::vector<std::pair<std::string, std::string>> sources;
    for (const char* top : {"engine", "game", "libworldforge", "forge-cli", "viewer"}) {
        const fs::path dir = root / top;
        if (!fs::is_directory(dir)) {
            continue;
        }
        for (auto it = fs::recursive_directory_iterator(dir); it != fs::recursive_directory_iterator(); ++it) {
            if (it->is_directory() && skipDirs.count(it->path().filename().string()) != 0) {
                it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file() || extensions.count(it->path().extension().string()) == 0) {
                continue;
            }
            sources.emplace_back(fs::relative(it->path(), root).generic_string(), stripCommentsAndLiterals(readFile(it->path())));
        }
    }
    std::sort(sources.begin(), sources.end());

    Scan scan;
    scan.files = sources.size();
    std::set<std::string> names;
    for (const auto& [file, clean] : sources) {
        for (Finding& f : findDeclarations(file, clean)) {
            names.insert(f.name);
            scan.declarations.push_back(std::move(f));
        }
    }
    for (const auto& [file, clean] : sources) {
        for (Finding& f : findIterations(file, clean, names)) {
            scan.iterations.push_back(std::move(f));
        }
    }
    std::sort(scan.declarations.begin(), scan.declarations.end());
    std::sort(scan.iterations.begin(), scan.iterations.end());
    return scan;
}

std::string joinLines(const std::vector<std::string>& lines)
{
    std::string out;
    for (const std::string& l : lines) {
        out += '\n';
        out += "  " + l;
    }
    return out;
}

std::string describe(const Finding& f)
{
    return f.file + ":" + std::to_string(f.line) + " " + f.name;
}

fs::path repoRoot()
{
    return (fs::path(ICHOR_REPO_DATA) / "..").lexically_normal();
}

} // namespace

TEST_CASE("unordered audit scanner flags declarations and loops (red if the scanner is gutted)", "[audit]")
{
    const std::string src = stripCommentsAndLiterals(R"(
        #include <unordered_map>
        using Cache = std::unordered_map<int, std::vector<int>>;
        struct S {
            std::unordered_map<std::string, std::map<int, int>> byName_;
            std::unordered_set<int> seen_;
            Cache cache_;
            const std::unordered_map<int, int>& ref(const std::unordered_set<int>* in);
        };
        // std::unordered_set<int> inCommentOnly;
        const char* s = "std::unordered_set<int> inString";
        void f(S& s) {
            for (const auto& [k, v] : s.byName_) { use(k); }
            for (int x : this->seen_) { use(x); }
            for (auto it = cache_.begin(); it != cache_.end(); ++it) {}
            std::for_each(seen_.begin(), seen_.end(), g);
            auto hit = s.byName_.find("a");
            if (hit == s.byName_.end()) {}
        }
    )");
    const auto decls = findDeclarations("x.cpp", src);
    std::set<std::string> names;
    for (const Finding& d : decls) {
        names.insert(d.name);
    }
    CHECK(names == std::set<std::string>{"byName_", "seen_", "cache_", "in"});

    const auto loops = findIterations("x.cpp", src, names);
    std::set<std::pair<std::string, int>> hit;
    for (const Finding& l : loops) {
        hit.insert({l.name, l.line});
    }
    CHECK(loops.size() == 5);
    CHECK(hit.count({"byName_", 13}) == 1);
    CHECK(hit.count({"seen_", 14}) == 1);
    CHECK(hit.count({"cache_", 15}) == 1);
    CHECK(hit.count({"seen_", 16}) == 1);
    for (const Finding& l : loops) {
        CHECK(l.line != 17);
        CHECK(l.line != 18);
    }
}

TEST_CASE("every unordered container is reviewed and none is iterated", "[audit]")
{
    const Scan scan = scanTree(repoRoot());
    REQUIRE(scan.files > 100);

    const std::string report = [&] {
        std::ostringstream out;
        out << "files scanned: " << scan.files << "\nunordered declarations: " << scan.declarations.size() << "\n";
        for (const Finding& f : scan.declarations) {
            out << "  decl  " << describe(f) << "\n";
        }
        out << "loops over them: " << scan.iterations.size() << "\n";
        for (const Finding& f : scan.iterations) {
            out << "  LOOP  " << describe(f) << "\n";
        }
        return out.str();
    }();
    INFO(report);

    std::vector<std::string> unreviewedDecls;
    std::vector<std::string> unreviewedLoops;
    for (const Finding& f : scan.declarations) {
        if (!listed(f, kDeclarations)) {
            unreviewedDecls.push_back(describe(f));
        }
    }
    for (const Finding& f : scan.iterations) {
        if (!listed(f, kIterations)) {
            unreviewedLoops.push_back(describe(f));
        }
    }
    {
        INFO("Unreviewed unordered container (add a lookup-only entry to kDeclarations, or use an ordered container): " << joinLines(unreviewedDecls));
        CHECK(unreviewedDecls.empty());
    }
    {
        INFO("Loop over an unordered container (sort the keys first, or review it into kIterations): " << joinLines(unreviewedLoops));
        CHECK(unreviewedLoops.empty());
    }

    for (const Entry& e : kDeclarations) {
        const bool present = std::any_of(scan.declarations.begin(), scan.declarations.end(),
            [&](const Finding& f) { return f.file == e.file && f.name == e.name; });
        INFO("stale kDeclarations entry: " << e.file << " " << e.name);
        CHECK(present);
        CHECK(!e.reason.empty());
    }
    for (const Entry& e : kIterations) {
        const bool present = std::any_of(scan.iterations.begin(), scan.iterations.end(),
            [&](const Finding& f) { return f.file == e.file && f.name == e.name; });
        INFO("stale kIterations entry: " << e.file << " " << e.name);
        CHECK(present);
        CHECK(!e.reason.empty());
    }

    const char* outDir = ICHOR_TEST_OUTPUT;
    std::error_code ec;
    fs::create_directories(outDir, ec);
    std::ofstream(fs::path(outDir) / "unordered_audit_report.txt") << report;
}
