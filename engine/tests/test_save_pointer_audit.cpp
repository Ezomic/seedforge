// Source audit for runtime handles in serialised data (SEED-237, doc 14). A save is read before
// any generated actor exists, so a pointer, an entt::entity or any other runtime handle written to
// disk is meaningless on load (at best) and a use-after-free (at worst). Saves carry StableIds and
// re-resolve them. This scans the C++ sources for three things:
//
//   1. Every type with a Reflect<T> (the BinaryWriter schema, which covers scene components and
//      the ledger/wallet/standing records): each reflected member, and each repo struct nested
//      in one, must not be a pointer, smart pointer, std::function, entt handle or integer
//      pointer.
//   2. Every member of the structs declared in engine/save/include (slot, settings, chronicle
//      records), which are written without Reflect.
//   3. The serialiser sources (anything using BinaryWriter, plus engine/save/src): no
//      uintptr_t, and no write() of a bare entt::entity or pointer variable.
//
// kKnownViolations lists reviewed exceptions with a reason; a stale entry fails. Limits: a lexical
// scan. It sees members by declared type text and writers by declared variable name, not through
// `auto` deductions or a type alias that hides a pointer.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
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

struct Known {
    std::string_view file;
    std::string_view what;
    std::string_view reason;
};

// Runtime option structs in the save module: passed to the save functions, never written to disk.
constexpr std::array<Known, 2> kKnownViolations = {{
    {"engine/save/include/ichor/save/atomic_file.h", "AtomicWriteOptions::valid", "validator callback for a write in progress; not part of any file"},
    {"engine/save/include/ichor/save/save_slots.h", "SavePolicy::migrations", "non-owning registry pointer in the policy argument; the policy is never serialised"},
}};

struct Finding {
    std::string file;
    int line = 0;
    std::string what;
};

bool operator<(const Finding& a, const Finding& b)
{
    return std::tie(a.file, a.line, a.what) < std::tie(b.file, b.line, b.what);
}

struct Source {
    std::string file;
    std::string clean;
};

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

std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string lastComponent(const std::string& qualified)
{
    const std::size_t p = qualified.rfind("::");
    return p == std::string::npos ? qualified : qualified.substr(p + 2);
}

// Index just past the brace that closes the one at `open`.
std::size_t matchBrace(const std::string& text, std::size_t open)
{
    int depth = 0;
    for (std::size_t i = open; i < text.size(); ++i) {
        depth += text[i] == '{' ? 1 : text[i] == '}' ? -1 : 0;
        if (depth == 0) {
            return i + 1;
        }
    }
    return text.size();
}

// A runtime handle: something whose value means nothing once the process that wrote it is gone.
const std::regex& handleType()
{
    static const std::regex re(
        R"(\*|\bentt::(?:entity|handle|basic_handle|registry)\b|\b(?:shared_ptr|unique_ptr|weak_ptr|reference_wrapper)\b|\bstd::function\b|\b(?:u?intptr_t|ptrdiff_t)\b|\w*Handle\b)");
    return re;
}

struct Member {
    std::string type;
    std::string name;
    int line = 0;
};

// Data members declared directly in a struct body (methods, statics, aliases and nested types are skipped).
std::vector<Member> membersOf(const std::string& clean, std::size_t bodyBegin, std::size_t bodyEnd)
{
    static const std::regex skip(R"(\b(?:static|using|typedef|friend|template|operator|enum|struct|class|union|return)\b)");
    std::vector<Member> members;
    std::string stmt;
    std::size_t stmtStart = bodyBegin;
    for (std::size_t i = bodyBegin; i < bodyEnd; ++i) {
        const char c = clean[i];
        if (c == '{') {
            const std::size_t close = matchBrace(clean, i);
            const bool isBlock = stmt.find('(') != std::string::npos || std::regex_search(stmt, skip);
            if (isBlock) {
                stmt.clear();
                stmtStart = close;
            } else {
                stmt += "{}";
            }
            i = close - 1;
            continue;
        }
        if (c == ':' && (i + 1 >= bodyEnd || clean[i + 1] != ':') && (i == 0 || clean[i - 1] != ':')) {
            const std::string t = stmt;
            if (t.find("public") != std::string::npos || t.find("private") != std::string::npos || t.find("protected") != std::string::npos) {
                stmt.clear();
                stmtStart = i + 1;
                continue;
            }
        }
        if (c != ';') {
            if (stmt.empty() && std::isspace(static_cast<unsigned char>(c)) != 0) {
                stmtStart = i + 1;
                continue;
            }
            stmt += c;
            continue;
        }

        std::string flat;
        int angle = 0;
        for (char ch : stmt) {
            angle += ch == '<' ? 1 : ch == '>' ? -1 : 0;
            if (angle == 0 && ch != '>') {
                flat += ch;
            }
        }
        const bool method = flat.find('(') != std::string::npos;
        if (!method && !std::regex_search(stmt, skip)) {
            std::string decl = stmt;
            const std::size_t init = decl.find_first_of("={");
            if (init != std::string::npos) {
                decl.erase(init);
            }
            const std::size_t bracket = decl.find('[');
            if (bracket != std::string::npos) {
                decl.erase(bracket);
            }
            static const std::regex nameAtEnd(R"((\w+)\s*$)");
            std::smatch m;
            if (std::regex_search(decl, m, nameAtEnd)) {
                members.push_back({decl.substr(0, static_cast<std::size_t>(m.position(1))), m[1].str(), lineOf(clean, stmtStart)});
            }
        }
        stmt.clear();
        stmtStart = i + 1;
    }
    return members;
}

struct StructDef {
    std::string file;
    std::vector<Member> members;
};

using StructIndex = std::multimap<std::string, StructDef>;

StructIndex indexStructs(const std::vector<Source>& sources)
{
    StructIndex index;
    static const std::regex head(R"(\b(?:struct|class)\s+(\w+)\s*(?:final\s*)?(?::[^;{]*)?\{)");
    for (const Source& s : sources) {
        for (std::sregex_iterator it(s.clean.begin(), s.clean.end(), head), end; it != end; ++it) {
            const std::size_t open = static_cast<std::size_t>(it->position() + it->length() - 1);
            const std::size_t close = matchBrace(s.clean, open);
            index.emplace((*it)[1].str(), StructDef{s.file, membersOf(s.clean, open + 1, close - 1)});
        }
    }
    return index;
}

struct ReflectedType {
    std::string name;
    std::string file;
    std::set<std::string> fields;
};

// Reflect<T> specialisations and the member names each lists as &Alias::member.
std::vector<ReflectedType> findReflected(const std::vector<Source>& sources)
{
    std::vector<ReflectedType> found;
    static const std::regex head(R"(struct\s+Reflect\s*<\s*([\w:]+)\s*>\s*\{)");
    static const std::regex fieldRef(R"(&\s*\w+(?:::\w+)*::(\w+))");
    for (const Source& s : sources) {
        for (std::sregex_iterator it(s.clean.begin(), s.clean.end(), head), end; it != end; ++it) {
            const std::size_t open = static_cast<std::size_t>(it->position() + it->length() - 1);
            const std::string body = s.clean.substr(open, matchBrace(s.clean, open) - open);
            ReflectedType t{lastComponent((*it)[1].str()), s.file, {}};
            for (std::sregex_iterator f(body.begin(), body.end(), fieldRef), fe; f != fe; ++f) {
                t.fields.insert((*f)[1].str());
            }
            found.push_back(std::move(t));
        }
    }
    return found;
}

void checkMembers(const std::string& owner, const StructDef& def, const std::set<std::string>* only, std::vector<Finding>& out)
{
    for (const Member& m : def.members) {
        if (only != nullptr && only->count(m.name) == 0) {
            continue;
        }
        if (std::regex_search(m.type, handleType())) {
            std::string type = m.type;
            while (!type.empty() && std::isspace(static_cast<unsigned char>(type.back())) != 0) {
                type.pop_back();
            }
            out.push_back({def.file, m.line, owner + "::" + m.name + " (" + type + ")"});
        }
    }
}

// Reflected types, then every repo struct reachable through their reflected members.
std::vector<Finding> auditReflected(const std::vector<Source>& sources)
{
    const StructIndex index = indexStructs(sources);
    std::vector<Finding> out;
    std::set<std::string> visited;
    std::vector<std::string> pending;
    static const std::regex ident(R"([A-Za-z_]\w*)");

    std::set<std::string> reflectedNames;
    for (const ReflectedType& t : findReflected(sources)) {
        reflectedNames.insert(t.name);
    }
    const auto enqueueNested = [&](const std::string& type) {
        for (std::sregex_iterator it(type.begin(), type.end(), ident), end; it != end; ++it) {
            const std::string word = it->str();
            if (index.count(word) != 0 && reflectedNames.count(word) == 0 && visited.insert(word).second) {
                pending.push_back(word);
            }
        }
    };

    for (const ReflectedType& t : findReflected(sources)) {
        const auto range = index.equal_range(t.name);
        for (auto it = range.first; it != range.second; ++it) {
            checkMembers(t.name, it->second, &t.fields, out);
            for (const Member& m : it->second.members) {
                if (t.fields.count(m.name) != 0) {
                    enqueueNested(m.type);
                }
            }
        }
        visited.insert(t.name);
    }
    while (!pending.empty()) {
        const std::string name = pending.back();
        pending.pop_back();
        const auto range = index.equal_range(name);
        for (auto it = range.first; it != range.second; ++it) {
            checkMembers(name, it->second, nullptr, out);
            for (const Member& m : it->second.members) {
                enqueueNested(m.type);
            }
        }
    }
    return out;
}

bool isUnder(const std::string& file, std::string_view prefix)
{
    return file.compare(0, prefix.size(), prefix) == 0;
}

// Every struct declared in the save module's headers (written without Reflect).
std::vector<Finding> auditSaveHeaders(const std::vector<Source>& sources)
{
    std::vector<Source> headers;
    for (const Source& s : sources) {
        if (isUnder(s.file, "engine/save/include/") && s.file.ends_with(".h")) {
            headers.push_back(s);
        }
    }
    std::vector<Finding> out;
    for (const auto& [name, def] : indexStructs(headers)) {
        checkMembers(name, def, nullptr, out);
    }
    return out;
}

// Source of anything that writes bytes to a save: no integer pointers, no bare handle passed to write().
std::vector<Finding> auditWriters(const std::vector<Source>& sources)
{
    static const std::regex intPtr(R"(\b(?:u?intptr_t)\b)");
    static const std::regex entityDecl(R"(\bentt::entity\s*(?:&\s*)?(\w+)\s*[=;,){]|\bstd::vector\s*<\s*entt::entity\s*>\s*(\w+)\s*[=;,){])");
    static const std::regex pointerDecl(R"(\b[\w:]+(?:<[^<>;()]*>)?\s*\*\s*(?:const\s+)?(\w+)\s*[=;,)])");
    std::vector<Finding> out;
    for (const Source& s : sources) {
        const bool writer = s.clean.find("BinaryWriter") != std::string::npos || isUnder(s.file, "engine/save/src/");
        if (!writer || isUnder(s.file, "engine/core/include/ichor/core/binary.h")) {
            continue;
        }
        for (std::sregex_iterator it(s.clean.begin(), s.clean.end(), intPtr), end; it != end; ++it) {
            out.push_back({s.file, lineOf(s.clean, static_cast<std::size_t>(it->position())), "integer pointer in a serialiser"});
        }
        std::set<std::string> handles;
        for (std::sregex_iterator it(s.clean.begin(), s.clean.end(), entityDecl), end; it != end; ++it) {
            handles.insert((*it)[1].matched ? (*it)[1].str() : (*it)[2].str());
        }
        for (std::sregex_iterator it(s.clean.begin(), s.clean.end(), pointerDecl), end; it != end; ++it) {
            const std::string name = (*it)[1].str();
            const std::string decl = it->str();
            if (decl.find("return") == std::string::npos && decl.find("reinterpret_cast") == std::string::npos) {
                handles.insert(name);
            }
        }
        for (const std::string& name : handles) {
            const std::regex write(R"(\bwrite\s*\(\s*)" + name + R"(\s*\))");
            for (std::sregex_iterator it(s.clean.begin(), s.clean.end(), write), end; it != end; ++it) {
                out.push_back({s.file, lineOf(s.clean, static_cast<std::size_t>(it->position())), "write(" + name + ") of a pointer or entt::entity"});
            }
        }
    }
    return out;
}

std::vector<Source> loadSources(const fs::path& root)
{
    const std::set<std::string> extensions = {".h", ".hpp", ".inl", ".cpp", ".cc", ".cxx"};
    const std::set<std::string> skipDirs = {"build", "vcpkg_installed", "tests", ".git", "third_party", "external"};
    std::vector<Source> sources;
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
            sources.push_back({fs::relative(it->path(), root).generic_string(), stripCommentsAndLiterals(readFile(it->path()))});
        }
    }
    std::sort(sources.begin(), sources.end(), [](const Source& a, const Source& b) { return a.file < b.file; });
    return sources;
}

std::string describe(const Finding& f)
{
    return f.file + ":" + std::to_string(f.line) + " " + f.what;
}

bool known(const Finding& f)
{
    return std::any_of(kKnownViolations.begin(), kKnownViolations.end(),
        [&](const Known& k) { return k.file == f.file && f.what.find(k.what) != std::string::npos; });
}

fs::path repoRoot()
{
    return (fs::path(ICHOR_REPO_DATA) / "..").lexically_normal();
}

constexpr const char* kBadFixture = R"(
    struct Actor { int hp; };
    struct Bad {
        std::uint32_t good = 0;
        Actor* target = nullptr;
        entt::entity owner{};
        std::shared_ptr<Actor> keep;
        std::function<void()> callback;
        std::uintptr_t address = 0;
        NavHandle path;
        glm::vec3 position{0.0f};
        std::vector<std::uint64_t> ids;
        bool transientPointer(Actor* a) const;
        Actor* unreflected = nullptr;
    };
    struct Nested { Bad inner; };
    template <> struct Reflect<Nested> {
        static constexpr auto fields() { return std::make_tuple(field("inner", &Nested::inner)); }
    };
    template <> struct Reflect<Bad> {
        static constexpr auto fields() { return std::make_tuple(field("good", &Bad::good), field("target", &Bad::target),
            field("owner", &Bad::owner), field("keep", &Bad::keep), field("callback", &Bad::callback),
            field("address", &Bad::address), field("path", &Bad::path), field("ids", &Bad::ids)); }
    };
)";

constexpr const char* kBadWriter = R"(
    void save(BinaryWriter& w, const entt::registry& registry) {
        std::vector<entt::entity> entities;
        const Actor* actor = find();
        entt::entity owner = find();
        w.write(entities);
        w.write(actor);
        w.write(owner);
        w.write(*actor);
        w.write(reinterpret_cast<std::uintptr_t>(actor));
    }
)";

constexpr const char* kGoodWriter = R"(
    void save(BinaryWriter& w, const entt::registry& registry) {
        std::vector<entt::entity> entities;
        const auto* mesh = registry.try_get<MeshRef>(e);
        if (mesh) w.write(*mesh);
        w.write(registry.get<StableId>(entities[0]));
        const char* p = reinterpret_cast<const char*>(buffer);
    }
)";

} // namespace

TEST_CASE("pointer audit scanner flags handles in a bad fixture (red if the scanner is gutted)", "[audit]")
{
    const std::vector<Source> bad = {{"fixture/bad.h", stripCommentsAndLiterals(kBadFixture)}};

    const std::vector<Finding> found = auditReflected(bad);
    std::set<std::string> names;
    for (const Finding& f : found) {
        names.insert(f.what.substr(0, f.what.find(' ')));
    }
    CHECK(names == std::set<std::string>{"Bad::target", "Bad::owner", "Bad::keep", "Bad::callback", "Bad::address", "Bad::path"});
    CHECK(names.count("Bad::unreflected") == 0);
    CHECK(names.count("Bad::good") == 0);
    CHECK(names.count("Bad::ids") == 0);

    const std::vector<Source> badWriter = {{"fixture/bad_writer.cpp", stripCommentsAndLiterals(kBadWriter)}};
    const std::vector<Finding> writes = auditWriters(badWriter);
    std::set<std::string> writeKinds;
    for (const Finding& f : writes) {
        writeKinds.insert(f.what);
    }
    CHECK(writeKinds == std::set<std::string>{"write(entities) of a pointer or entt::entity", "write(actor) of a pointer or entt::entity",
                            "write(owner) of a pointer or entt::entity", "integer pointer in a serialiser"});

    const std::vector<Source> goodWriter = {{"fixture/good_writer.cpp", stripCommentsAndLiterals(kGoodWriter)}};
    CHECK(auditWriters(goodWriter).empty());

    const std::vector<Source> badHeader = {{"engine/save/include/bad_slot.h", stripCommentsAndLiterals("struct Slot { Actor* a; std::string name; };")}};
    CHECK(auditSaveHeaders(badHeader).size() == 1);
}

TEST_CASE("serialised data holds ids, never pointers or runtime handles", "[audit]")
{
    const std::vector<Source> sources = loadSources(repoRoot());
    REQUIRE(sources.size() > 100);

    const std::vector<ReflectedType> reflected = findReflected(sources);
    REQUIRE(reflected.size() >= 20);
    const std::set<std::string> reflectedNames = [&] {
        std::set<std::string> names;
        for (const ReflectedType& t : reflected) {
            names.insert(t.name);
        }
        return names;
    }();
    for (const char* mustCover : {"Transform", "Combatant", "Fact", "StableId"}) {
        INFO("expected a Reflect<" << mustCover << ">");
        CHECK(reflectedNames.count(mustCover) == 1);
    }

    std::vector<Finding> all = auditReflected(sources);
    for (Finding& f : auditSaveHeaders(sources)) {
        all.push_back(std::move(f));
    }
    for (Finding& f : auditWriters(sources)) {
        all.push_back(std::move(f));
    }
    std::sort(all.begin(), all.end());

    std::ostringstream report;
    report << "files scanned: " << sources.size() << "\nreflected types: " << reflected.size() << "\nfindings: " << all.size() << "\n";
    std::vector<std::string> unreviewed;
    for (const Finding& f : all) {
        report << (known(f) ? "  known  " : "  BAD    ") << describe(f) << "\n";
        if (!known(f)) {
            unreviewed.push_back(describe(f));
        }
    }
    INFO(report.str());

    std::string joined;
    for (const std::string& line : unreviewed) {
        joined += "\n  " + line;
    }
    INFO("Serialised data must hold StableIds, not handles (doc 14): " << joined);
    CHECK(unreviewed.empty());

    for (const Known& k : kKnownViolations) {
        const bool present = std::any_of(all.begin(), all.end(), [&](const Finding& f) { return k.file == f.file && f.what.find(k.what) != std::string::npos; });
        INFO("stale kKnownViolations entry: " << k.file << " " << k.what);
        CHECK(present);
        CHECK(!k.reason.empty());
    }

    const char* outDir = ICHOR_TEST_OUTPUT;
    std::error_code ec;
    fs::create_directories(outDir, ec);
    std::ofstream(fs::path(outDir) / "save_pointer_audit_report.txt") << report.str();
}
