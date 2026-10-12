#include "ichor/assets/bundle_io.h"

#include "ichor/core/log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>

namespace ichor {

namespace {

constexpr std::array<char, 4> kMagic{'I', 'S', 'C', 'N'};
constexpr std::uint32_t kVersion = 2;

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

bool validate(const SceneBundle& bundle, const std::string& file)
{
    const auto fail = [&](const std::string& why) {
        logf(LogLevel::Error, "{}: {}", file, why);
        return false;
    };
    for (const Texture& t : bundle.textures) {
        if (t.width == 0 || t.height == 0 || t.mipCount != mipLevelCount(t.width, t.height)) return fail("texture dimensions");
        if (t.format != TextureFormat::Rgba8Srgb && t.format != TextureFormat::Rgba8Unorm) return fail("texture format");
        std::size_t expected = 0;
        for (std::uint32_t w = t.width, h = t.height, mip = 0; mip < t.mipCount; ++mip, w = std::max(1u, w / 2), h = std::max(1u, h / 2)) {
            expected += static_cast<std::size_t>(w) * h * 4;
        }
        if (t.pixels.size() != expected) return fail("texture pixel size");
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
    for (const Node& n : bundle.nodes) {
        if (n.mesh >= bundle.meshes.size()) return fail("node mesh index");
    }
    return true;
}

}

bool writeBundle(const std::filesystem::path& path, const SceneBundle& bundle)
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

    w.pod(static_cast<std::uint32_t>(bundle.nodes.size()));
    for (const auto& n : bundle.nodes) {
        w.string(n.name);
        w.pod(n.mesh);
        w.pod(n.world);
    }
    return out.good();
}

bool readBundle(const std::filesystem::path& path, SceneBundle& bundle)
{
    std::error_code ec;
    const std::uintmax_t fileSize = std::filesystem::file_size(path, ec);
    std::ifstream in(path, std::ios::binary);
    if (ec || !in) {
        logf(LogLevel::Error, "cannot open {}", path.string());
        return false;
    }
    Reader r(in, fileSize);
    std::array<char, 4> magic{};
    std::uint32_t version = 0;
    if (!r.pod(magic) || magic != kMagic || !r.pod(version) || version != kVersion) {
        logf(LogLevel::Error, "{} is not a version {} scene bundle", path.string(), kVersion);
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
        if (!r.string(c.name) || !r.pod(c.skeleton) || !r.pod(c.duration) || !r.pods(c.ozzBytes)) {
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

    if (!r.count(count, 72)) return false;
    bundle.nodes.resize(count);
    for (auto& n : bundle.nodes) {
        if (!r.string(n.name) || !r.pod(n.mesh) || !r.pod(n.world)) {
            return false;
        }
    }
    return validate(bundle, path.string());
}

}
