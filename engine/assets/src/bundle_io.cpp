#include "ichor/assets/bundle_io.h"

#include "ichor/assets/bundle.h"
#include "ichor/core/log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>

#include <glm/geometric.hpp>

namespace ichor {

namespace {

constexpr std::array<char, 4> kMagic{'I', 'S', 'C', 'N'};
// 4: a texture may be block-compressed (BC7, BC5, BC4), its mips stored as whole blocks. Version 3 bundles are refused, not converted.
// 5: a clip carries its foot contacts. Version 4 bundles are refused, not converted.
constexpr std::uint32_t kVersion = 5;
// Sockets, LOD groups and collision follow the nodes under this tag, and only when there are any,
// so a bundle without them is the same bytes it always was.
constexpr std::array<char, 4> kPieceTag{'I', 'P', 'C', '1'};

class Writer {
public:
    explicit Writer(std::ofstream& out)
        : out_(out)
    {
    }

    template <class T>
    void pod(const T& value)
    {
        out_.write(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    void string(const std::string& value)
    {
        pod(static_cast<std::uint32_t>(value.size()));
        out_.write(value.data(), static_cast<std::streamsize>(value.size()));
    }

    template <class T>
    void pods(const std::vector<T>& values)
    {
        pod(static_cast<std::uint32_t>(values.size()));
        out_.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(T)));
    }

    void strings(const std::vector<std::string>& values)
    {
        pod(static_cast<std::uint32_t>(values.size()));
        for (const auto& value : values) {
            string(value);
        }
    }

private:
    std::ofstream& out_;
};

class Reader {
public:
    Reader(std::ifstream& in, std::uintmax_t fileSize)
        : in_(in)
        , remaining_(fileSize)
    {
    }

    template <class T>
    bool pod(T& value)
    {
        if (!take(sizeof(T))) return false;
        in_.read(reinterpret_cast<char*>(&value), sizeof(T));
        return in_.good();
    }

    // Every count is checked against the bytes left in the file before anything
    // is allocated, so a corrupt header cannot request gigabytes.
    bool count(std::uint32_t& value, std::size_t elementSize)
    {
        return pod(value) && value <= remaining_ / std::max<std::size_t>(elementSize, 1);
    }

    bool string(std::string& value)
    {
        std::uint32_t size = 0;
        if (!count(size, 1) || !take(size)) return false;
        value.resize(size);
        in_.read(value.data(), size);
        return in_.good();
    }

    template <class T>
    bool pods(std::vector<T>& values)
    {
        std::uint32_t n = 0;
        if (!count(n, sizeof(T)) || !take(static_cast<std::size_t>(n) * sizeof(T))) return false;
        values.resize(n);
        in_.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(n * sizeof(T)));
        return in_.good() || (n == 0);
    }

    bool atEnd() const { return remaining_ == 0; }

    bool strings(std::vector<std::string>& values)
    {
        std::uint32_t n = 0;
        if (!count(n, 4)) return false;
        values.resize(n);
        for (auto& value : values) {
            if (!string(value)) return false;
        }
        return true;
    }

private:
    bool take(std::size_t bytes)
    {
        if (bytes > remaining_) return false;
        remaining_ -= bytes;
        return true;
    }

    std::ifstream& in_;
    std::size_t remaining_;
};

bool validate(const SceneBundle& bundle, const PieceData& pieces, const std::string& file)
{
    const auto fail = [&](const std::string& why) {
        logf(LogLevel::Error, "{}: {}", file, why);
        return false;
    };
    for (const Texture& t : bundle.textures) {
        if (t.width == 0 || t.height == 0 || t.width > kMaxTextureEdge || t.height > kMaxTextureEdge || t.mipCount != mipLevelCount(t.width, t.height)) return fail("texture dimensions");
        if (!isKnownTextureFormat(t.format)) return fail("texture format");
        if (!isTextureLayoutValid(t)) return fail("texture pixel size");
    }
    if (bundle.materials.empty()) return fail("no materials");
    const auto textureOk = [&](std::uint32_t index) { return index == kNoTexture || index < bundle.textures.size(); };
    for (const Material& m : bundle.materials) {
        if (!textureOk(m.baseColorTexture) || !textureOk(m.metallicRoughnessTexture) || !textureOk(m.normalTexture)) return fail("material texture index");
    }
    for (const Skeleton& s : bundle.skeletons) {
        if (s.jointNames.empty() || s.jointNames.size() != s.inverseBind.size() || s.ozzBytes.empty()) return fail("skeleton shape");
        for (const glm::mat4& m : s.inverseBind) {
            for (int c = 0; c < 4; ++c) {
                for (int r = 0; r < 4; ++r) {
                    if (!std::isfinite(m[c][r])) return fail("inverse bind matrix");
                }
            }
        }
    }
    for (const AnimationClip& c : bundle.clips) {
        if (c.skeleton >= bundle.skeletons.size() || !(c.duration > 0.0f) || !std::isfinite(c.duration) || c.ozzBytes.empty()) return fail("clip shape");
        for (const FootContact& f : c.footContacts) {
            if (!(f.time >= 0.0f && f.time <= c.duration) || f.foot > 1) return fail("foot contact");
        }
    }
    for (const Mesh& m : bundle.meshes) {
        if (m.vertices.empty() || m.indices.size() % 3 != 0 || m.submeshes.empty()) return fail("mesh shape");
        if (m.skeleton != kNoSkeleton && m.skeleton >= bundle.skeletons.size()) return fail("mesh skeleton index");
        const std::size_t jointCount = m.skeleton == kNoSkeleton ? 1 : bundle.skeletons[m.skeleton].jointNames.size();
        for (const Vertex& v : m.vertices) {
            std::uint32_t sum = 0;
            for (int k = 0; k < 4; ++k) {
                if (v.joints[k] >= jointCount) return fail("vertex joint index");
                sum += v.weights[k];
            }
            if (sum == 0) return fail("vertex without weights");
        }
        for (std::uint32_t index : m.indices) {
            if (index >= m.vertices.size()) return fail("index out of range");
        }
        for (const Submesh& s : m.submeshes) {
            if (s.material >= bundle.materials.size() || s.firstIndex > m.indices.size() || s.indexCount > m.indices.size() - s.firstIndex) return fail("submesh range");
        }
    }
    for (const SoundClip& s : bundle.sounds) {
        if (s.sampleRate < 8000 || s.sampleRate > 192000 || s.samples.size() < 2) return fail("sound shape");
    }
    for (const Node& n : bundle.nodes) {
        if (n.mesh >= bundle.meshes.size()) return fail("node mesh index");
    }
    for (const Socket& s : pieces.sockets) {
        const float length = glm::length(s.rotation);
        if (s.name.empty() || !std::isfinite(s.position.x + s.position.y + s.position.z) || !(length > 0.5f && length < 1.5f)) return fail("socket");
    }
    for (const LodGroup& g : pieces.lodGroups) {
        if (g.levels.size() < 2 || g.levels.front().distance != 0.0f) return fail("lod group shape");
        float previous = 0.0f;
        for (const LodLevel& level : g.levels) {
            if (level.mesh >= bundle.meshes.size() || !std::isfinite(level.distance) || level.distance < previous) return fail("lod level");
            previous = level.distance;
        }
    }
    for (const CollisionShape& c : pieces.collision) {
        if (c.kind != CollisionKind::Box && c.kind != CollisionKind::Convex) return fail("collision kind");
        const glm::vec3 extent = c.boundsMax - c.boundsMin;
        if (!std::isfinite(extent.x + extent.y + extent.z) || extent.x < 0.0f || extent.y < 0.0f || extent.z < 0.0f) return fail("collision bounds");
        if ((c.kind == CollisionKind::Box) != c.points.empty()) return fail("collision points");
        for (const glm::vec3& p : c.points) {
            if (!std::isfinite(p.x + p.y + p.z)) return fail("collision point");
        }
    }
    return true;
}

}

bool writeBundle(const std::filesystem::path& path, const SceneBundle& bundle)
{
    return writeBundle(path, bundle, PieceData{});
}

bool readBundle(const std::filesystem::path& path, SceneBundle& bundle)
{
    PieceData discarded;
    return readBundle(path, bundle, discarded);
}

bool writeBundle(const std::filesystem::path& path, const SceneBundle& bundle, const PieceData& pieces)
{
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        logf(LogLevel::Error, "cannot write {}", path.string());
        return false;
    }
    Writer w(out);
    w.pod(kMagic);
    w.pod(kVersion);

    w.pod(static_cast<std::uint32_t>(bundle.textures.size()));
    for (const auto& t : bundle.textures) {
        w.string(t.name);
        w.pod(t.width);
        w.pod(t.height);
        w.pod(t.mipCount);
        w.pod(t.format);
        w.pods(t.pixels);
    }

    w.pod(static_cast<std::uint32_t>(bundle.materials.size()));
    for (const auto& m : bundle.materials) {
        w.string(m.name);
        w.pod(m.baseColorFactor);
        w.pod(m.metallicFactor);
        w.pod(m.roughnessFactor);
        w.pod(m.baseColorTexture);
        w.pod(m.metallicRoughnessTexture);
        w.pod(m.normalTexture);
    }

    w.pod(static_cast<std::uint32_t>(bundle.skeletons.size()));
    for (const auto& s : bundle.skeletons) {
        w.string(s.name);
        w.strings(s.jointNames);
        w.pods(s.inverseBind);
        w.pods(s.ozzBytes);
    }

    w.pod(static_cast<std::uint32_t>(bundle.clips.size()));
    for (const auto& c : bundle.clips) {
        w.string(c.name);
        w.pod(c.skeleton);
        w.pod(c.duration);
        w.pods(c.ozzBytes);
        w.pods(c.footContacts);
    }

    w.pod(static_cast<std::uint32_t>(bundle.meshes.size()));
    for (const auto& m : bundle.meshes) {
        w.string(m.name);
        w.pods(m.vertices);
        w.pods(m.indices);
        w.pods(m.submeshes);
        w.pod(m.boundsMin);
        w.pod(m.boundsMax);
        w.pod(m.skeleton);
    }

    w.pod(static_cast<std::uint32_t>(bundle.sounds.size()));
    for (const auto& s : bundle.sounds) {
        w.string(s.name);
        w.pod(s.sampleRate);
        w.pods(s.samples);
    }

    w.pod(static_cast<std::uint32_t>(bundle.nodes.size()));
    for (const auto& n : bundle.nodes) {
        w.string(n.name);
        w.pod(n.mesh);
        w.pod(n.world);
    }

    if (!pieces.empty()) {
        w.pod(kPieceTag);
        w.pod(static_cast<std::uint32_t>(pieces.sockets.size()));
        for (const auto& s : pieces.sockets) {
            w.string(s.piece);
            w.string(s.name);
            w.pod(s.position);
            w.pod(s.rotation);
        }
        w.pod(static_cast<std::uint32_t>(pieces.lodGroups.size()));
        for (const auto& g : pieces.lodGroups) {
            w.string(g.piece);
            w.pods(g.levels);
        }
        w.pod(static_cast<std::uint32_t>(pieces.collision.size()));
        for (const auto& c : pieces.collision) {
            w.string(c.piece);
            w.string(c.name);
            w.pod(c.kind);
            w.pod(c.boundsMin);
            w.pod(c.boundsMax);
            w.pods(c.points);
        }
    }
    return out.good();
}

bool readBundle(const std::filesystem::path& path, SceneBundle& bundle, PieceData& pieces)
{
    std::error_code ec;
    const std::uintmax_t fileSize = std::filesystem::file_size(path, ec);
    std::ifstream in(path, std::ios::binary);
    if (ec || !in) {
        logf(LogLevel::Error, "cannot open {}", path.string());
        return false;
    }
    Reader r(in, fileSize);
    pieces = {};
    std::array<char, 4> magic{};
    std::uint32_t version = 0;
    if (!r.pod(magic) || magic != kMagic || !r.pod(version) || version != kVersion) {
        logf(LogLevel::Error, "{} is not a version {} scene bundle; cook it again with ichor_cook", path.string(), kVersion);
        return false;
    }

    std::uint32_t count = 0;
    if (!r.count(count, 24)) return false;
    bundle.textures.resize(count);
    for (auto& t : bundle.textures) {
        if (!r.string(t.name) || !r.pod(t.width) || !r.pod(t.height) || !r.pod(t.mipCount) || !r.pod(t.format) || !r.pods(t.pixels)) {
            return false;
        }
    }

    if (!r.count(count, 44)) return false;
    bundle.materials.resize(count);
    for (auto& m : bundle.materials) {
        if (!r.string(m.name) || !r.pod(m.baseColorFactor) || !r.pod(m.metallicFactor) || !r.pod(m.roughnessFactor)
            || !r.pod(m.baseColorTexture) || !r.pod(m.metallicRoughnessTexture) || !r.pod(m.normalTexture)) {
            return false;
        }
    }

    if (!r.count(count, 16)) return false;
    bundle.skeletons.resize(count);
    for (auto& s : bundle.skeletons) {
        if (!r.string(s.name) || !r.strings(s.jointNames) || !r.pods(s.inverseBind) || !r.pods(s.ozzBytes)) {
            return false;
        }
    }

    if (!r.count(count, 16)) return false;
    bundle.clips.resize(count);
    for (auto& c : bundle.clips) {
        if (!r.string(c.name) || !r.pod(c.skeleton) || !r.pod(c.duration) || !r.pods(c.ozzBytes) || !r.pods(c.footContacts)) {
            return false;
        }
    }

    if (!r.count(count, 44)) return false;
    bundle.meshes.resize(count);
    for (auto& m : bundle.meshes) {
        if (!r.string(m.name) || !r.pods(m.vertices) || !r.pods(m.indices) || !r.pods(m.submeshes) || !r.pod(m.boundsMin) || !r.pod(m.boundsMax) || !r.pod(m.skeleton)) {
            return false;
        }
    }

    if (!r.count(count, 12)) return false;
    bundle.sounds.resize(count);
    for (auto& s : bundle.sounds) {
        if (!r.string(s.name) || !r.pod(s.sampleRate) || !r.pods(s.samples)) {
            return false;
        }
    }

    if (!r.count(count, 72)) return false;
    bundle.nodes.resize(count);
    for (auto& n : bundle.nodes) {
        if (!r.string(n.name) || !r.pod(n.mesh) || !r.pod(n.world)) {
            return false;
        }
    }

    if (!r.atEnd()) {
        std::array<char, 4> tag{};
        if (!r.pod(tag) || tag != kPieceTag) {
            logf(LogLevel::Error, "{}: unknown data after the nodes", path.string());
            return false;
        }
        if (!r.count(count, 36)) return false;
        pieces.sockets.resize(count);
        for (auto& s : pieces.sockets) {
            if (!r.string(s.piece) || !r.string(s.name) || !r.pod(s.position) || !r.pod(s.rotation)) {
                return false;
            }
        }
        if (!r.count(count, 8)) return false;
        pieces.lodGroups.resize(count);
        for (auto& g : pieces.lodGroups) {
            if (!r.string(g.piece) || !r.pods(g.levels)) {
                return false;
            }
        }
        if (!r.count(count, 40)) return false;
        pieces.collision.resize(count);
        for (auto& c : pieces.collision) {
            if (!r.string(c.piece) || !r.string(c.name) || !r.pod(c.kind) || !r.pod(c.boundsMin) || !r.pod(c.boundsMax) || !r.pods(c.points)) {
                return false;
            }
        }
        if (!r.atEnd()) {
            logf(LogLevel::Error, "{}: trailing bytes", path.string());
            return false;
        }
    }
    return validate(bundle, pieces, path.string());
}

}
