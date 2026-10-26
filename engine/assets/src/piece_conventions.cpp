#include "ichor/assets/piece_conventions.h"

#include <cgltf.h>

#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <set>
#include <string_view>

namespace ichor {

namespace {

constexpr std::string_view kSocketPrefix = "SOCKET_";
constexpr std::string_view kBoxPrefix = "UBX_";
constexpr std::string_view kConvexPrefix = "UCX_";
constexpr std::string_view kLodMarker = "_LOD";

// LODn takes over at this many bounding radii of LOD0 (the radius is floored at kMinRadius metres).
constexpr float kLodRadii[kMaxLodLevels] = {0.0f, 16.0f, 40.0f};
constexpr float kMinRadius = 0.5f;

bool startsWith(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

bool startsWithNoCase(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size()
        && std::equal(prefix.begin(), prefix.end(), text.begin(), [](char a, char b) { return std::toupper(static_cast<unsigned char>(a)) == std::toupper(static_cast<unsigned char>(b)); });
}

bool validName(std::string_view text)
{
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) { return std::isgraph(static_cast<unsigned char>(c)) != 0; });
}

std::string nodeName(const cgltf_node& node)
{
    return node.name != nullptr ? node.name : "";
}

struct LodName {
    std::string piece;
    long level = -1; // -1: no suffix
    bool malformed = false;
};

// SM_Foo_LOD1 -> {SM_Foo, 1}. "_LOD" followed by anything but digits is not a LOD suffix at all.
LodName parseLod(std::string_view name)
{
    LodName out;
    out.piece = std::string(name);
    const std::size_t at = name.rfind(kLodMarker);
    if (at == std::string_view::npos) return out;
    const std::string_view digits = name.substr(at + kLodMarker.size());
    if (digits.empty() || digits.size() > 3 || !std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        return out;
    }
    out.piece = std::string(name.substr(0, at));
    out.level = std::stol(std::string(digits));
    out.malformed = out.piece.empty() || out.level >= static_cast<long>(kMaxLodLevels);
    return out;
}

bool finite(const glm::mat4& m)
{
    const float* p = glm::value_ptr(m);
    return std::all_of(p, p + 16, [](float f) { return std::isfinite(f); });
}

glm::mat4 worldOf(const cgltf_node& node)
{
    float world[16];
    cgltf_node_transform_world(&node, world);
    return glm::make_mat4(world);
}

// Every position of a mesh, all primitives; false when it has none or one cannot be read.
bool readPositions(const cgltf_mesh& mesh, std::vector<glm::vec3>& out)
{
    out.clear();
    for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
        for (cgltf_size a = 0; a < mesh.primitives[p].attributes_count; ++a) {
            const cgltf_attribute& attr = mesh.primitives[p].attributes[a];
            if (attr.type != cgltf_attribute_type_position || attr.data == nullptr) continue;
            for (cgltf_size i = 0; i < attr.data->count; ++i) {
                float v[3];
                if (!cgltf_accessor_read_float(attr.data, i, v, 3)) return false;
                out.push_back(glm::make_vec3(v));
            }
        }
    }
    return !out.empty();
}

glm::mat4 inverseOrIdentity(const glm::mat4& m)
{
    if (!finite(m)) return glm::mat4(1.0f);
    const float det = glm::determinant(m);
    if (!std::isfinite(det) || std::abs(det) < 1e-12f) return glm::mat4(1.0f);
    return glm::inverse(m);
}

bool rotationOf(const glm::mat4& m, glm::quat& out)
{
    glm::mat3 basis(m);
    for (int c = 0; c < 3; ++c) {
        const float length = glm::length(basis[c]);
        if (!(length > 1e-6f)) return false;
        basis[c] /= length;
    }
    if (!(glm::determinant(basis) > 0.0f)) return false;
    out = glm::normalize(glm::quat_cast(basis));
    return std::isfinite(out.w) && std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
}

}

void scanConventions(const cgltf_data& data, ConventionScan& scan)
{
    scan = {};
    scan.roles.assign(data.nodes_count, NodeRole::Plain);
    const auto issue = [&](std::string text) { scan.issues.push_back(std::move(text)); };

    // Pieces are the mesh nodes that stay placed; a LOD suffix is not part of the piece name.
    std::map<std::string, std::size_t> pieceNodes;
    for (cgltf_size n = 0; n < data.nodes_count; ++n) {
        const std::string name = nodeName(data.nodes[n]);
        const bool conventional = startsWith(name, kSocketPrefix) || startsWith(name, kBoxPrefix) || startsWith(name, kConvexPrefix);
        if (data.nodes[n].mesh != nullptr && !conventional) {
            const LodName lod = parseLod(name);
            const bool lodSuffix = lod.level >= 0 && !lod.malformed;
            const auto [at, added] = pieceNodes.emplace(lodSuffix ? lod.piece : name, n);
            if (!added && lodSuffix && lod.level == 0) at->second = n;
        }
    }

    // Collision: UBX_<piece> and UCX_<piece> meshes.
    for (cgltf_size n = 0; n < data.nodes_count; ++n) {
        const cgltf_node& node = data.nodes[n];
        const std::string name = nodeName(node);
        const bool box = startsWith(name, kBoxPrefix);
        if (!box && !startsWith(name, kConvexPrefix)) continue;
        const std::string piece = name.substr(kBoxPrefix.size());
        if (node.mesh == nullptr) {
            issue(std::format("{}: collision node carries no mesh, ignored", name));
            continue;
        }
        if (!validName(piece)) {
            issue(std::format("{}: collision node names no piece, ignored", name));
            continue;
        }
        std::vector<glm::vec3> points;
        if (!readPositions(*node.mesh, points)) {
            issue(std::format("{}: collision mesh has no readable positions, ignored", name));
            continue;
        }
        const auto owner = pieceNodes.find(piece);
        if (owner == pieceNodes.end()) {
            issue(std::format("{}: no piece named {} in this file, shape kept in file space", name, piece));
        }
        const glm::mat4 toPiece = (owner != pieceNodes.end() ? inverseOrIdentity(worldOf(data.nodes[owner->second])) : glm::mat4(1.0f)) * worldOf(node);
        if (!finite(toPiece)) {
            issue(std::format("{}: non-finite transform, ignored", name));
            continue;
        }
        CollisionShape shape;
        shape.piece = piece;
        shape.name = name;
        shape.kind = box ? CollisionKind::Box : CollisionKind::Convex;
        shape.boundsMin = glm::vec3(std::numeric_limits<float>::max());
        shape.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
        for (const glm::vec3& point : points) {
            const glm::vec3 moved = glm::vec3(toPiece * glm::vec4(point, 1.0f));
            shape.boundsMin = glm::min(shape.boundsMin, moved);
            shape.boundsMax = glm::max(shape.boundsMax, moved);
            if (!box) shape.points.push_back(moved);
        }
        const glm::vec3 extent = shape.boundsMax - shape.boundsMin;
        if (!std::isfinite(extent.x + extent.y + extent.z) || !std::isfinite(shape.boundsMin.x + shape.boundsMin.y + shape.boundsMin.z)) {
            issue(std::format("{}: non-finite vertices, ignored", name));
            continue;
        }
        scan.roles[n] = NodeRole::Collision;
        scan.collision.push_back(std::move(shape));
    }

    // Sockets: SOCKET_<name> empties, attached to the nearest mesh-carrying ancestor.
    std::set<std::pair<std::string, std::string>> socketKeys;
    for (cgltf_size n = 0; n < data.nodes_count; ++n) {
        const cgltf_node& node = data.nodes[n];
        const std::string name = nodeName(node);
        if (!startsWith(name, kSocketPrefix)) {
            if (startsWithNoCase(name, kSocketPrefix)) {
                issue(std::format("{}: the socket prefix is exactly SOCKET_, node treated as ordinary", name));
            }
            continue;
        }
        const std::string socketName = name.substr(kSocketPrefix.size());
        if (node.mesh != nullptr) {
            issue(std::format("{}: a socket is an empty, this node carries a mesh and stays a piece", name));
            continue;
        }
        if (!validName(socketName)) {
            issue(std::format("{}: socket has no usable name, ignored", name));
            continue;
        }
        std::string piece;
        glm::mat4 frame(1.0f);
        const cgltf_node* ancestor = node.parent;
        for (cgltf_size depth = 0; ancestor != nullptr && depth <= data.nodes_count; ++depth, ancestor = ancestor->parent) {
            if (ancestor->mesh != nullptr && scan.roles[static_cast<std::size_t>(ancestor - data.nodes)] == NodeRole::Plain) {
                const std::string ancestorName = nodeName(*ancestor);
                const LodName lod = parseLod(ancestorName);
                piece = lod.level >= 0 && !lod.malformed ? lod.piece : ancestorName;
                frame = inverseOrIdentity(worldOf(*ancestor));
                break;
            }
        }
        const glm::mat4 local = frame * worldOf(node);
        Socket socket;
        socket.piece = piece;
        socket.name = socketName;
        if (!finite(local) || !rotationOf(local, socket.rotation)) {
            issue(std::format("{}: degenerate or mirrored transform, ignored", name));
            continue;
        }
        socket.position = glm::vec3(local[3]);
        if (!socketKeys.emplace(piece, socketName).second) {
            issue(std::format("{}: second socket of that name on {}, ignored", name, piece.empty() ? "the file" : piece));
            continue;
        }
        scan.roles[n] = NodeRole::Socket;
        scan.sockets.push_back(std::move(socket));
    }

    // LODs: <piece>_LOD0.. groups. A group that is not whole stays ordinary meshes so nothing disappears.
    std::map<std::string, std::map<long, std::size_t>> groups;
    for (cgltf_size n = 0; n < data.nodes_count; ++n) {
        const cgltf_node& node = data.nodes[n];
        if (node.mesh == nullptr || scan.roles[n] != NodeRole::Plain) continue;
        const std::string name = nodeName(node);
        const LodName lod = parseLod(name);
        if (lod.level < 0) continue;
        if (lod.malformed) {
            issue(std::format("{}: a LOD suffix is _LOD0 to _LOD{} on a named piece, node treated as ordinary", name, kMaxLodLevels - 1));
            continue;
        }
        if (!groups[lod.piece].emplace(lod.level, n).second) {
            issue(std::format("{}: second mesh for that LOD of {}, node treated as ordinary", name, lod.piece));
        }
    }
    for (const auto& [piece, levels] : groups) {
        const bool contiguous = levels.begin()->first == 0 && levels.rbegin()->first == static_cast<long>(levels.size()) - 1;
        if (levels.size() < 2 || !contiguous) {
            issue(std::format("{}: LODs start at LOD0, follow without gaps and have at least two levels, group treated as ordinary", piece));
            continue;
        }
        ConventionScan::Lod lod;
        lod.piece = piece;
        std::vector<glm::vec3> positions;
        float radius = kMinRadius;
        if (readPositions(*data.nodes[levels.begin()->second].mesh, positions)) {
            glm::vec3 lo(std::numeric_limits<float>::max());
            glm::vec3 hi(std::numeric_limits<float>::lowest());
            for (const glm::vec3& p : positions) {
                lo = glm::min(lo, p);
                hi = glm::max(hi, p);
            }
            const float measured = glm::length(hi - lo) * 0.5f;
            if (std::isfinite(measured)) radius = std::max(measured, kMinRadius);
        }
        for (const auto& [level, node] : levels) {
            lod.nodes.push_back(node);
            lod.distances.push_back(kLodRadii[level] * radius);
            if (level > 0) scan.roles[node] = NodeRole::LodExtra;
        }
        scan.lods.push_back(std::move(lod));
    }
}

std::vector<std::string> describeConventions(const ConventionScan& scan)
{
    std::vector<std::string> lines;
    for (const Socket& s : scan.sockets) {
        lines.push_back(std::format("socket {} on {} at ({:.3f}, {:.3f}, {:.3f})", s.name, s.piece.empty() ? "<file>" : s.piece, s.position.x, s.position.y, s.position.z));
    }
    for (const ConventionScan::Lod& l : scan.lods) {
        std::string text = std::format("lod {}:", l.piece);
        for (std::size_t i = 0; i < l.distances.size(); ++i) {
            text += std::format(" LOD{}@{:.2f}m", i, l.distances[i]);
        }
        lines.push_back(std::move(text));
    }
    for (const CollisionShape& c : scan.collision) {
        lines.push_back(std::format("collision {} {} of {}, {} points", c.name, c.kind == CollisionKind::Box ? "box" : "convex", c.piece, c.points.size()));
    }
    for (const std::string& i : scan.issues) {
        lines.push_back("issue " + i);
    }
    return lines;
}

}
