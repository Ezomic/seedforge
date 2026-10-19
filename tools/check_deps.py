#!/usr/bin/env python3
"""Dependency policy check (doc 17, "Dependencies are a written-down list").

Fails when:
  1. engine/vcpkg.json and the doc 17 stack table disagree.
  2. a CMake file finds or links a third-party package the manifest does not provide.
  3. the module layering is broken: core depends on anything, a headless library reaches
     a runtime-only target or package, a rules library reaches the engine, or targets
     link in a cycle.
  4. libworldforge (pure C++, zero engine dependencies) includes or links engine code.

Usage: check_deps.py [--root DIR]    check the tree (default: this repository)
       check_deps.py --self-test     prove every rule goes red on a deliberate violation
"""
import argparse
import copy
import json
import re
import sys
from pathlib import Path

# vcpkg port -> words that must appear in the doc 17 stack table while the port is in the manifest.
PORT_TERMS = {
    "catch2": ["Catch2"],
    "glm": ["GLM"],
    "cgltf": ["cgltf"],
    "stb": ["stb_image"],
    "meshoptimizer": ["meshoptimizer"],
    "entt": ["EnTT"],
    "joltphysics": ["Jolt"],
    "recastnavigation": ["Recast"],
    "sol2": ["sol2"],
    "nlohmann-json": ["nlohmann-json"],
    "rmlui": ["RmlUi"],
    "lua": ["Lua"],
    "ozz-animation": ["ozz-animation"],
    "miniaudio": ["miniaudio"],
    "sdl3": ["SDL3"],
    "vulkan-headers": ["Vulkan"],
    "vulkan-loader": ["Vulkan"],
    "vk-bootstrap": ["vk-bootstrap"],
    "vulkan-memory-allocator": ["VMA"],
    "imgui": ["Dear ImGui"],
    "tracy": ["Tracy"],
    "directx-dxc": ["DXC"],
}

# CMake package name or target namespace -> vcpkg port that provides it.
PACKAGE_PORTS = {
    "Catch2": "catch2",
    "glm": "glm",
    "EnTT": "entt",
    "Jolt": "joltphysics",
    "RecastNavigation": "recastnavigation",
    "sol2": "sol2",
    "nlohmann_json": "nlohmann-json",
    "RmlUi": "rmlui",
    "unofficial::lua": "lua",
    "ozz": "ozz-animation",
    "meshoptimizer": "meshoptimizer",
    "SDL3": "sdl3",
    "Vulkan": "vulkan-loader",
    "vk-bootstrap": "vk-bootstrap",
    "GPUOpen": "vulkan-memory-allocator",
    "imgui": "imgui",
    "Tracy": "tracy",
}

# find_package names that differ from the target namespace.
FIND_PACKAGE_PORTS = {
    "ozz-animation": "ozz-animation",
    "Stb": "stb",
    "directx-dxc": "directx-dxc",
    "recastnavigation": "recastnavigation",
    "unofficial-lua": "lua",
    "lua": "lua",
    "VulkanMemoryAllocator": "vulkan-memory-allocator",
}

# Host tools CMake finds on the machine; they are not vcpkg dependencies of the engine.
HOST_PACKAGES = {"Python3"}

# Targets that exist only in the runtime build; nothing headless may reach them.
RUNTIME_TARGETS = {"ichor_platform_sdl", "ichor_gfx_vulkan", "ichor_editor_ui", "ichor_profile"}

# Rules libraries: game logic that must stay testable without the engine around it.
RULES_LIBS = {
    "ichor_data", "ichor_economy", "ichor_factions", "ichor_graph", "ichor_ledger",
    "ichor_progression", "ichor_traversal", "ichor_npc",
}
ENGINE_TARGETS = {
    "ichor_world", "ichor_gfx_api", "ichor_gfx_null", "ichor_gfx_vulkan", "ichor_platform_api",
    "ichor_platform_null", "ichor_platform_sdl", "ichor_ui", "ichor_script", "ichor_loop",
    "ichor_editor", "ichor_editor_ui", "ichor_audio", "ichor_assets", "ichor_anim", "ichor_miniaudio",
}
RULES_PORTS_ALLOWED = {"nlohmann-json", "glm"}

# What libworldforge may never include: our engine and the libraries only the engine uses.
WORLDFORGE_FORBIDDEN_INCLUDE = re.compile(
    r'#\s*include\s*[<"](ichor/|SDL|vulkan|vk_|Jolt/|entt/|RmlUi/|sol/|lua|ozz/|imgui|miniaudio|tracy/)',
    re.IGNORECASE,
)
WORLDFORGE_CMAKE_ENGINE = re.compile(r"\bichor_\w+|\.\./engine|\bengine/")

CMAKE_SKIP_DIRS = {"build", "node_modules", ".git", ".claude", "vcpkg_installed"}


class Inputs:
    def __init__(self, manifest, doc, cmake, worldforge):
        self.manifest = manifest
        self.doc = doc
        self.cmake = cmake
        self.worldforge = worldforge


def load(root):
    root = Path(root)
    manifest = json.loads((root / "engine/vcpkg.json").read_text(encoding="utf-8"))
    doc = (root / "docs/17-tech-stack.md").read_text(encoding="utf-8")
    cmake = {}
    for base in ("engine", "game"):
        for p in (root / base).rglob("CMakeLists.txt"):
            rel = p.relative_to(root)
            if CMAKE_SKIP_DIRS.isdisjoint(rel.parts):
                cmake[rel.as_posix()] = p.read_text(encoding="utf-8")
    worldforge = {}
    wf = root / "libworldforge"
    if wf.is_dir():
        for p in wf.rglob("*"):
            if p.is_file() and p.suffix in {".h", ".hpp", ".cpp", ".cc", ".cmake", ".txt"}:
                worldforge[p.relative_to(root).as_posix()] = p.read_text(encoding="utf-8", errors="replace")
    return Inputs(manifest, doc, cmake, worldforge)


def manifest_ports(manifest):
    def name(d):
        return d if isinstance(d, str) else d["name"]

    base = {name(d) for d in manifest.get("dependencies", [])}
    runtime = set()
    for feature in manifest.get("features", {}).values():
        runtime |= {name(d) for d in feature.get("dependencies", [])}
    return base, runtime - base


def stack_table(doc):
    m = re.search(r"^## The stack\s*$(.*?)^## ", doc, re.S | re.M)
    if not m:
        return None
    return "\n".join(line for line in m.group(1).splitlines() if line.lstrip().startswith("|"))


def check_manifest_vs_doc(inp):
    errors = []
    table = stack_table(inp.doc)
    if table is None:
        return ["docs/17-tech-stack.md: no '## The stack' section found"]
    lowered = table.lower()
    base, runtime = manifest_ports(inp.manifest)
    present = base | runtime
    covered = {t for p in present for t in PORT_TERMS.get(p, [])}
    for port in sorted(present):
        if port not in PORT_TERMS:
            errors.append(
                f"vcpkg.json lists '{port}' but tools/check_deps.py has no record of it: give it a one-line why "
                f"in the doc 17 stack table, then add it to PORT_TERMS (and PACKAGE_PORTS if CMake links it)")
    for port, terms in sorted(PORT_TERMS.items()):
        for term in terms:
            if port in present and term.lower() not in lowered:
                errors.append(f"vcpkg.json lists '{port}' but the doc 17 stack table never mentions '{term}'")
            if port not in present and term not in covered and term.lower() in lowered:
                errors.append(f"the doc 17 stack table mentions '{term}' but vcpkg.json no longer lists '{port}'")
    return errors


def strip_comments(text):
    return re.sub(r"#[^\n]*", "", text)


def parse_cmake(cmake):
    libs, links, packages = set(), [], []
    for path, text in sorted(cmake.items()):
        text = strip_comments(text)
        for m in re.finditer(r"add_library\(\s*([\w:.+-]+)", text):
            libs.add(m.group(1))
        for m in re.finditer(r"find_package\(\s*([\w:.+-]+)", text):
            packages.append((path, m.group(1)))
        for m in re.finditer(r"target_link_libraries\(\s*([\w:.+-]+)([^)]*)\)", text):
            items = [i for i in m.group(2).split()
                     if i not in {"PUBLIC", "PRIVATE", "INTERFACE"} and not i.startswith("$")]
            links.append((path, m.group(1), items))
    return libs, links, packages


def port_of(item):
    """Third-party port behind a link item: '' for one of ours, None when nothing provides it."""
    if item.startswith("ichor_") and "::" not in item:
        return ""
    if item in ("worldforge", "worldforge::worldforge"):
        return ""
    for prefix in sorted(PACKAGE_PORTS, key=len, reverse=True):
        if item == prefix or item.startswith(prefix + "::"):
            return PACKAGE_PORTS[prefix]
    return None


def check_cmake(inp):
    errors = []
    base, runtime = manifest_ports(inp.manifest)
    present = base | runtime
    libs, links, packages = parse_cmake(inp.cmake)

    for path, pkg in packages:
        if pkg in HOST_PACKAGES:
            continue
        port = FIND_PACKAGE_PORTS.get(pkg) or PACKAGE_PORTS.get(pkg)
        if port is None:
            errors.append(f"{path}: find_package({pkg}) is not a package vcpkg.json provides; "
                          f"add the dependency (and its why in doc 17) first")
        elif port not in present:
            errors.append(f"{path}: find_package({pkg}) needs '{port}', which vcpkg.json does not list")

    edges, ports = {}, {}
    for path, target, items in links:
        for item in items:
            port = port_of(item)
            if port is None:
                errors.append(f"{path}: {target} links '{item}', which no vcpkg.json dependency provides")
            elif port == "":
                edges.setdefault(target, set()).add(item)
            elif port not in present:
                errors.append(f"{path}: {target} links '{item}' but vcpkg.json does not list '{port}'")
            else:
                ports.setdefault(target, set()).add(port)

    for target in sorted(libs):
        deps = edges.get(target, set()) - {"ichor_warnings"}
        own_ports = ports.get(target, set())
        if target == "ichor_core" and (deps or own_ports):
            errors.append(f"ichor_core links {sorted(deps | own_ports)}: core is the bottom layer and depends on nothing")
        if target.startswith("ichor_") and target not in RUNTIME_TARGETS:
            bad = deps & RUNTIME_TARGETS
            if bad:
                errors.append(f"{target} links runtime-only targets {sorted(bad)}: "
                              f"headless libraries must build without a window or GPU")
            bad_ports = own_ports & runtime
            if bad_ports:
                errors.append(f"{target} links runtime-only packages {sorted(bad_ports)}: "
                              f"those exist only in the runtime build")
        if target in RULES_LIBS:
            bad = deps & ENGINE_TARGETS
            if bad:
                errors.append(f"rules library {target} links engine targets {sorted(bad)}: "
                              f"rules stay testable without rendering, audio, windowing or assets")
            bad_ports = own_ports - RULES_PORTS_ALLOWED
            if bad_ports:
                errors.append(f"rules library {target} links {sorted(bad_ports)}: "
                              f"only {sorted(RULES_PORTS_ALLOWED)} are allowed there")

    state = {}

    def visit(node, stack):
        if state.get(node) == 2:
            return
        if state.get(node) == 1:
            errors.append("link cycle: " + " -> ".join(stack[stack.index(node):] + [node]))
            return
        state[node] = 1
        for nxt in sorted(edges.get(node, set()) - {"ichor_warnings"}):
            visit(nxt, stack + [node])
        state[node] = 2

    for node in sorted(edges):
        visit(node, [])
    return errors


def check_worldforge(inp):
    errors = []
    for path, text in sorted(inp.worldforge.items()):
        is_cmake = path.endswith((".txt", ".cmake"))
        lines = strip_comments(text).splitlines() if is_cmake else text.splitlines()
        pattern = WORLDFORGE_CMAKE_ENGINE if is_cmake else WORLDFORGE_FORBIDDEN_INCLUDE
        for n, line in enumerate(lines, 1):
            if pattern.search(line):
                what = "references the engine" if is_cmake else "includes engine code"
                errors.append(f"{path}:{n}: libworldforge {what}: {line.strip()}")
    return errors


def check(inp):
    return check_manifest_vs_doc(inp) + check_cmake(inp) + check_worldforge(inp)


def self_test(inp):
    def append_cmake(path, text):
        def f(c):
            c.cmake[path] += "\n" + text + "\n"
        return f

    def set_worldforge(path, text):
        return lambda c: c.worldforge.update({path: text})

    cases = [
        ("unrecorded vcpkg dependency", "has no record of it",
         lambda c: c.manifest["dependencies"].append("boost-asio")),
        ("dependency missing from the doc table", "never mentions",
         lambda c: setattr(c, "doc", c.doc.replace("EnTT", "ECS lib"))),
        ("doc table keeps a removed dependency", "no longer lists",
         lambda c: c.manifest["dependencies"].remove("entt")),
        ("find_package of an unlisted package", "find_package(Boost)",
         append_cmake("engine/core/CMakeLists.txt", "find_package(Boost CONFIG REQUIRED)")),
        ("link to an unknown package", "no vcpkg.json dependency provides",
         append_cmake("engine/graph/CMakeLists.txt", "target_link_libraries(ichor_graph PRIVATE fmt::fmt)")),
        ("core depends on another module", "bottom layer",
         append_cmake("engine/core/CMakeLists.txt", "target_link_libraries(ichor_core PUBLIC ichor_data)")),
        ("headless library links SDL", "runtime-only packages",
         append_cmake("engine/sim/CMakeLists.txt", "target_link_libraries(ichor_sim PUBLIC SDL3::SDL3)")),
        ("headless library links a runtime target", "runtime-only targets",
         append_cmake("engine/save/CMakeLists.txt", "target_link_libraries(ichor_save PUBLIC ichor_gfx_vulkan)")),
        ("rules library reaches the renderer", "rules library ichor_economy links engine",
         append_cmake("engine/economy/CMakeLists.txt", "target_link_libraries(ichor_economy PUBLIC ichor_gfx_api)")),
        ("rules library links physics", "rules library ichor_ledger links ['joltphysics']",
         append_cmake("engine/ledger/CMakeLists.txt", "target_link_libraries(ichor_ledger PRIVATE Jolt::Jolt)")),
        ("link cycle", "link cycle",
         append_cmake("engine/economy/CMakeLists.txt", "target_link_libraries(ichor_data PUBLIC ichor_economy)")),
        ("libworldforge includes the engine", "includes engine code",
         set_worldforge("libworldforge/src/a.cpp", "#include <ichor/core/dmath.h>\n")),
        ("libworldforge includes SDL", "includes engine code",
         set_worldforge("libworldforge/src/a.cpp", "#include <SDL3/SDL.h>\n")),
        ("libworldforge links an engine target", "references the engine",
         set_worldforge("libworldforge/CMakeLists.txt", "target_link_libraries(wf PUBLIC ichor_core)\n")),
    ]
    base = check(inp)
    if base:
        print("self-test needs a clean tree first:\n  " + "\n  ".join(base))
        return 1
    missed = 0
    for name, expect, mutate in cases:
        broken = copy.deepcopy(inp)
        mutate(broken)
        caught = any(expect in e for e in check(broken))
        print(f"  {name}: {'red as expected' if caught else 'STAYED GREEN'}")
        missed += not caught
    if missed:
        print(f"self-test: {missed} rule(s) did not catch their violation")
        return 1
    print(f"self-test: all {len(cases)} violations caught")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=str(Path(__file__).resolve().parent.parent))
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    inp = load(args.root)
    if args.self_test:
        return self_test(inp)
    errors = check(inp)
    if errors:
        print("Dependency policy violations (doc 17):")
        for e in errors:
            print(f"  - {e}")
        return 1
    print("dependency policy: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
