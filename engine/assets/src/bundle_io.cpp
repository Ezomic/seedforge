#include "ichor/assets/bundle_io.h"

#include "ichor/core/log.h"

#include <array>
#include <cstring>
#include <fstream>

namespace ichor {

namespace {

constexpr std::array<char, 4> kMagic{'I', 'S', 'C', 'N'};
constexpr std::uint32_t kVersion = 1;

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

private:
    std::ofstream& out_;
};

class Reader {
public:
    explicit Reader(std::ifstream& in)
        : in_(in)
    {
    }

    template <class T>
    bool pod(T& value)
    {
        in_.read(reinterpret_cast<char*>(&value), sizeof(T));
        return in_.good();
    }

    bool string(std::string& value)
    {
        std::uint32_t size = 0;
        if (!pod(size) || size > kMaxCount) return false;
        value.resize(size);
        in_.read(value.data(), size);
        return in_.good();
    }

    template <class T>
    bool pods(std::vector<T>& values)
    {
        std::uint32_t count = 0;
        if (!pod(count) || count > kMaxCount) return false;
        values.resize(count);
        in_.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(count * sizeof(T)));
        return in_.good() || (count == 0);
    }

private:
    static constexpr std::uint32_t kMaxCount = 1u << 28;
    std::ifstream& in_;
};

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

    w.pod(static_cast<std::uint32_t>(bundle.meshes.size()));
    for (const auto& m : bundle.meshes) {
        w.string(m.name);
        w.pods(m.vertices);
        w.pods(m.indices);
        w.pods(m.submeshes);
        w.pod(m.boundsMin);
        w.pod(m.boundsMax);
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
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        logf(LogLevel::Error, "cannot open {}", path.string());
        return false;
    }
    Reader r(in);
    std::array<char, 4> magic{};
    std::uint32_t version = 0;
    if (!r.pod(magic) || magic != kMagic || !r.pod(version) || version != kVersion) {
        logf(LogLevel::Error, "{} is not a version {} scene bundle", path.string(), kVersion);
        return false;
    }

    std::uint32_t count = 0;
    if (!r.pod(count)) return false;
    bundle.textures.resize(count);
    for (auto& t : bundle.textures) {
        if (!r.string(t.name) || !r.pod(t.width) || !r.pod(t.height) || !r.pod(t.mipCount) || !r.pod(t.format) || !r.pods(t.pixels)) {
            return false;
        }
    }

    if (!r.pod(count)) return false;
    bundle.materials.resize(count);
    for (auto& m : bundle.materials) {
        if (!r.string(m.name) || !r.pod(m.baseColorFactor) || !r.pod(m.metallicFactor) || !r.pod(m.roughnessFactor)
            || !r.pod(m.baseColorTexture) || !r.pod(m.metallicRoughnessTexture) || !r.pod(m.normalTexture)) {
            return false;
        }
    }

    if (!r.pod(count)) return false;
    bundle.meshes.resize(count);
    for (auto& m : bundle.meshes) {
        if (!r.string(m.name) || !r.pods(m.vertices) || !r.pods(m.indices) || !r.pods(m.submeshes) || !r.pod(m.boundsMin) || !r.pod(m.boundsMax)) {
            return false;
        }
    }

    if (!r.pod(count)) return false;
    bundle.nodes.resize(count);
    for (auto& n : bundle.nodes) {
        if (!r.string(n.name) || !r.pod(n.mesh) || !r.pod(n.world)) {
            return false;
        }
    }
    return true;
}

}
