#include "ichor/assets/fbx_import.h"

#include "ichor/assets/fbx.h"
#include "ichor/core/log.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/component_wise.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/io/archive.h>
#include <ozz/base/io/stream.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>

namespace ichor {

namespace {

constexpr double kTicksPerSecond = 46186158000.0;
constexpr double kSampleRate = 60.0;
constexpr const char* kRootJoint = "root";
// ozz holds at most 1023 joints; a file with more models than that is not a character.
constexpr std::size_t kMaxModels = 4096;
constexpr std::size_t kMaxJoints = 1023;
constexpr std::size_t kMaxCorners = 8u << 20;
constexpr std::size_t kMaxVertices = 4u << 20;

struct Trs {
    glm::dvec3 t{0.0};
    glm::dquat q{1.0, 0.0, 0.0, 0.0};
    glm::dvec3 s{1.0};
};

glm::dmat4 toMatrix(const Trs& trs)
{
    return glm::translate(glm::dmat4(1.0), trs.t) * glm::mat4_cast(trs.q) * glm::scale(glm::dmat4(1.0), trs.s);
}

Trs decomposeMatrix(const glm::dmat4& m)
{
    Trs trs;
    glm::dvec3 skew;
    glm::dvec4 perspective;
    glm::decompose(m, trs.s, trs.q, trs.t, skew, perspective);
    trs.q = glm::normalize(trs.q);
    return trs;
}

ozz::math::Transform toOzz(const Trs& trs)
{
    ozz::math::Transform t;
    t.translation = {static_cast<float>(trs.t.x), static_cast<float>(trs.t.y), static_cast<float>(trs.t.z)};
    t.rotation = {static_cast<float>(trs.q.x), static_cast<float>(trs.q.y), static_cast<float>(trs.q.z), static_cast<float>(trs.q.w)};
    t.scale = {static_cast<float>(trs.s.x), static_cast<float>(trs.s.y), static_cast<float>(trs.s.z)};
    return t;
}

template <class T>
bool serialize(const T& object, std::vector<std::uint8_t>& out)
{
    ozz::io::MemoryStream stream;
    {
        ozz::io::OArchive archive(&stream);
        archive << object;
    }
    const auto size = static_cast<std::size_t>(stream.Size());
    out.resize(size);
    stream.Seek(0, ozz::io::Stream::kSet);
    return stream.Read(out.data(), size) == size;
}

// FBX Euler angles in degrees, rotation order XYZ: the matrix is Rz * Ry * Rx.
glm::dquat eulerXyz(const glm::dvec3& degrees)
{
    const glm::dvec3 r = glm::radians(degrees);
    const glm::dquat x = glm::angleAxis(r.x, glm::dvec3(1, 0, 0));
    const glm::dquat y = glm::angleAxis(r.y, glm::dvec3(0, 1, 0));
    const glm::dquat z = glm::angleAxis(r.z, glm::dvec3(0, 0, 1));
    return z * y * x;
}

struct Curve {
    std::vector<double> times; // seconds
    std::vector<double> values;
};

enum Channel { Translation = 0, Rotation = 1, Scaling = 2 };

struct Model {
    std::int64_t id = 0;
    std::string name;
    std::string kind;
    std::int64_t parent = 0; // 0 is the scene root
    glm::dvec3 t{0.0};
    glm::dvec3 r{0.0};
    glm::dvec3 s{1.0};
    glm::dvec3 preRotation{0.0};
    glm::dvec3 postRotation{0.0};
    Curve curves[3][3];
    bool animated[3] = {false, false, false};
    bool isJoint() const { return kind == "LimbNode" || kind == "Root"; }
};

struct Cluster {
    std::int64_t id = 0;
    std::int64_t bone = 0;
    std::vector<std::int64_t> indices;
    std::vector<double> weights;
    glm::dmat4 transformLink{1.0};
};

struct Geometry {
    std::vector<double> positions; // xyz
    std::vector<std::int64_t> polygonVertexIndex;
    std::vector<double> normals;
    std::vector<std::int64_t> normalIndex;
    bool normalsByPolygonVertex = true;
    bool normalsIndexed = false;
    std::vector<double> uvs;
    std::vector<std::int64_t> uvIndex;
    bool uvsIndexed = false;
    std::vector<std::int64_t> polygonMaterial;
    bool materialAllSame = false;
};

struct FbxMaterial {
    std::string name;
    glm::vec3 diffuse{0.6f};
};

struct Scene {
    std::vector<Model> models;
    std::unordered_map<std::int64_t, std::size_t> modelIndex;
    double metresPerUnit = 0.01;
    std::int64_t meshModel = 0;
    Geometry geometry;
    bool hasGeometry = false;
    std::vector<Cluster> clusters;
    std::vector<FbxMaterial> materials; // in the order they connect to the mesh model

    const Model* find(std::int64_t id) const
    {
        const auto it = modelIndex.find(id);
        return it == modelIndex.end() ? nullptr : &models[it->second];
    }
    const Model* findByName(std::string_view name) const
    {
        for (const Model& m : models) {
            if (m.name == name) return &m;
        }
        return nullptr;
    }
};

std::string objectName(const fbx::Property& prop)
{
    const auto end = prop.text.find('\0');
    return end == std::string::npos ? prop.text : prop.text.substr(0, end);
}

const fbx::Node* property70(const fbx::Node& object, std::string_view name)
{
    const fbx::Node* props = object.child("Properties70");
    if (props == nullptr) return nullptr;
    for (const fbx::Node& p : props->children) {
        if (p.name == "P" && !p.props.empty() && p.props[0].text == name) return &p;
    }
    return nullptr;
}

bool vec3Property(const fbx::Node& object, std::string_view name, glm::dvec3& out)
{
    const fbx::Node* p = property70(object, name);
    if (p == nullptr || p->props.size() < 7) return false;
    out = {p->props[4].real, p->props[5].real, p->props[6].real};
    return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
}

bool readFile(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return false;
    const auto size = in.tellg();
    if (size < 0 || size > (128ll << 20)) return false;
    bytes.resize(static_cast<std::size_t>(size));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(bytes.data()), size);
    return static_cast<bool>(in);
}

// Finite and small enough that nothing built from it can overflow a float.
constexpr double kLimit = 1.0e6;

bool finite(const std::vector<double>& values)
{
    return std::all_of(values.begin(), values.end(), [](double v) { return std::isfinite(v) && std::abs(v) <= kLimit; });
}

const fbx::Property* firstProp(const fbx::Node& node, char type)
{
    return !node.props.empty() && node.props[0].type == type ? &node.props[0] : nullptr;
}

bool loadLayerElement(const fbx::Node& layer, const char* dataName, const char* indexName, std::vector<double>& data,
    std::vector<std::int64_t>& index, bool& indexed, bool* byPolygonVertex)
{
    const fbx::Node* d = layer.child(dataName);
    if (d == nullptr || d->props.empty() || !finite(d->props[0].reals)) return false;
    data = d->props[0].reals;
    const fbx::Node* ref = layer.child("ReferenceInformationType");
    indexed = ref != nullptr && !ref->props.empty() && ref->props[0].text == "IndexToDirect";
    if (indexed) {
        const fbx::Node* i = layer.child(indexName);
        if (i == nullptr || i->props.empty()) return false;
        index = i->props[0].ints;
    }
    if (byPolygonVertex != nullptr) {
        const fbx::Node* map = layer.child("MappingInformationType");
        *byPolygonVertex = map != nullptr && !map->props.empty() && map->props[0].text == "ByPolygonVertex";
    }
    return true;
}

bool loadScene(const std::filesystem::path& file, Scene& scene)
{
    std::vector<std::uint8_t> bytes;
    if (!readFile(file, bytes)) {
        logf(LogLevel::Error, "{}: cannot read", file.string());
        return false;
    }
    fbx::Document doc;
    std::string error;
    if (!fbx::parse(bytes, doc, error)) {
        logf(LogLevel::Error, "{}: {}", file.string(), error);
        return false;
    }

    // Axes and units. Only the layout Blender and most DCCs write is understood.
    if (const fbx::Node* settings = doc.root("GlobalSettings")) {
        const auto intProp = [&](std::string_view name, std::int64_t fallback) {
            const fbx::Node* p = property70(*settings, name);
            return p != nullptr && p->props.size() > 4 ? p->props[4].integer : fallback;
        };
        if (intProp("UpAxis", 1) != 1 || intProp("UpAxisSign", 1) != 1 || intProp("CoordAxis", 0) != 0) {
            logf(LogLevel::Error, "{}: only Y-up files with +X as the coordinate axis are supported", file.string());
            return false;
        }
        const fbx::Node* unit = property70(*settings, "UnitScaleFactor");
        if (unit != nullptr && unit->props.size() > 4 && std::isfinite(unit->props[4].real) && unit->props[4].real > 0.0) {
            scene.metresPerUnit = unit->props[4].real * 0.01;
        }
    }

    const fbx::Node* objects = doc.root("Objects");
    const fbx::Node* connections = doc.root("Connections");
    if (objects == nullptr || connections == nullptr) {
        logf(LogLevel::Error, "{}: no Objects or Connections", file.string());
        return false;
    }

    struct CurveNode {
        std::int64_t model = 0;
        int channel = -1;
    };
    std::unordered_map<std::int64_t, CurveNode> curveNodes;
    std::unordered_map<std::int64_t, const fbx::Node*> curveObjects;
    std::unordered_map<std::int64_t, const fbx::Node*> geometries;
    std::unordered_map<std::int64_t, const fbx::Node*> deformers;
    std::unordered_map<std::int64_t, const fbx::Node*> materialObjects;

    for (const fbx::Node& object : objects->children) {
        if (object.props.empty() || object.props[0].type != 'L') continue;
        const std::int64_t id = object.props[0].integer;
        if (object.name == "Model" && object.props.size() >= 3) {
            Model m;
            m.id = id;
            m.name = objectName(object.props[1]);
            m.kind = object.props[2].text;
            vec3Property(object, "Lcl Translation", m.t);
            vec3Property(object, "Lcl Rotation", m.r);
            vec3Property(object, "Lcl Scaling", m.s);
            vec3Property(object, "PreRotation", m.preRotation);
            vec3Property(object, "PostRotation", m.postRotation);
            const fbx::Node* order = property70(object, "RotationOrder");
            if (order != nullptr && order->props.size() > 4 && order->props[4].integer != 0) {
                logf(LogLevel::Error, "{}: model {} uses a rotation order other than XYZ", file.string(), m.name);
                return false;
            }
            if (scene.models.size() >= kMaxModels) {
                logf(LogLevel::Error, "{}: too many models", file.string());
                return false;
            }
            scene.modelIndex[id] = scene.models.size();
            scene.models.push_back(std::move(m));
        } else if (object.name == "AnimationCurveNode") {
            curveNodes[id] = {};
        } else if (object.name == "AnimationCurve") {
            curveObjects[id] = &object;
        } else if (object.name == "Geometry") {
            geometries[id] = &object;
        } else if (object.name == "Deformer") {
            deformers[id] = &object;
        } else if (object.name == "Material" && object.props.size() >= 2) {
            materialObjects[id] = &object;
        }
    }

    std::unordered_map<std::int64_t, std::int64_t> skinOfGeometry;
    std::unordered_map<std::int64_t, std::int64_t> skinOfCluster;
    std::unordered_map<std::int64_t, std::int64_t> boneOfCluster;
    std::int64_t geometryOfMesh = 0;
    std::vector<std::int64_t> materialIds;
    for (const fbx::Node& c : connections->children) {
        if (c.name != "C" || c.props.size() < 3) continue;
        const std::string& kind = c.props[0].text;
        const std::int64_t src = c.props[1].integer;
        const std::int64_t dst = c.props[2].integer;
        if (kind == "OO") {
            if (scene.modelIndex.contains(src) && scene.modelIndex.contains(dst)) {
                scene.models[scene.modelIndex[src]].parent = dst;
            } else if (scene.modelIndex.contains(src) && dst == 0) {
                scene.models[scene.modelIndex[src]].parent = 0;
            } else if (geometries.contains(src) && scene.modelIndex.contains(dst)) {
                geometryOfMesh = src;
                scene.meshModel = dst;
            } else if (materialObjects.contains(src) && scene.modelIndex.contains(dst)) {
                materialIds.push_back(src);
            } else if (deformers.contains(src) && geometries.contains(dst)) {
                skinOfGeometry[dst] = src;
            } else if (deformers.contains(src) && deformers.contains(dst)) {
                skinOfCluster[src] = dst;
            } else if (scene.modelIndex.contains(src) && deformers.contains(dst)) {
                boneOfCluster[dst] = src;
            }
        } else if (kind == "OP" && c.props.size() >= 4) {
            const std::string& property = c.props[3].text;
            if (curveNodes.contains(src) && scene.modelIndex.contains(dst)) {
                int channel = -1;
                if (property == "Lcl Translation") channel = Translation;
                if (property == "Lcl Rotation") channel = Rotation;
                if (property == "Lcl Scaling") channel = Scaling;
                curveNodes[src] = {dst, channel};
            } else if (curveObjects.contains(src) && curveNodes.contains(dst)) {
                const CurveNode& node = curveNodes[dst];
                const int axis = property == "d|X" ? 0 : property == "d|Y" ? 1 : property == "d|Z" ? 2 : -1;
                if (node.channel < 0 || axis < 0 || node.model == 0) continue;
                const fbx::Node& curve = *curveObjects[src];
                const fbx::Node* times = curve.child("KeyTime");
                const fbx::Node* values = curve.child("KeyValueFloat");
                if (times == nullptr || values == nullptr || times->props.empty() || values->props.empty()) continue;
                const auto& kt = times->props[0].ints;
                const auto& kv = values->props[0].reals;
                if (kt.size() != kv.size() || kt.empty() || !finite(kv)) {
                    logf(LogLevel::Error, "{}: an animation curve has mismatched or non-finite keys", file.string());
                    return false;
                }
                Model& m = scene.models[scene.modelIndex[node.model]];
                Curve& out = m.curves[node.channel][axis];
                out.times.resize(kt.size());
                for (std::size_t i = 0; i < kt.size(); ++i) {
                    out.times[i] = static_cast<double>(kt[i]) / kTicksPerSecond;
                    if (i > 0 && !(out.times[i] > out.times[i - 1])) {
                        logf(LogLevel::Error, "{}: animation key times must increase", file.string());
                        return false;
                    }
                }
                out.values = kv;
                m.animated[node.channel] = true;
            }
        }
    }

    // A cycle in the parent links would make every walk up the tree endless. Each model is
    // walked up once: 1 is on the walk in progress, 2 is known to reach the scene root.
    {
        std::vector<std::uint8_t> state(scene.models.size(), 0);
        for (std::size_t i = 0; i < scene.models.size(); ++i) {
            std::vector<std::size_t> walk;
            std::size_t at = i;
            bool reachesRoot = false;
            while (true) {
                if (state[at] == 2) {
                    reachesRoot = true;
                    break;
                }
                if (state[at] == 1) break;
                state[at] = 1;
                walk.push_back(at);
                const auto parent = scene.modelIndex.find(scene.models[at].parent);
                if (parent == scene.modelIndex.end()) {
                    reachesRoot = true;
                    break;
                }
                at = parent->second;
            }
            if (!reachesRoot) {
                logf(LogLevel::Error, "{}: the model hierarchy has a cycle", file.string());
                return false;
            }
            for (const std::size_t w : walk) state[w] = 2;
        }
    }

    for (const std::int64_t id : materialIds) {
        FbxMaterial material;
        material.name = objectName(materialObjects[id]->props[1]);
        glm::dvec3 diffuse(0.6);
        vec3Property(*materialObjects[id], "DiffuseColor", diffuse);
        material.diffuse = glm::vec3(diffuse);
        scene.materials.push_back(std::move(material));
    }

    if (geometryOfMesh != 0) {
        const fbx::Node& g = *geometries[geometryOfMesh];
        const fbx::Node* vertices = g.child("Vertices");
        const fbx::Node* indices = g.child("PolygonVertexIndex");
        if (vertices == nullptr || indices == nullptr || vertices->props.empty() || indices->props.empty() || !finite(vertices->props[0].reals)
            || vertices->props[0].reals.size() % 3 != 0) {
            logf(LogLevel::Error, "{}: the mesh has no usable vertices", file.string());
            return false;
        }
        Geometry& geo = scene.geometry;
        if (vertices->props[0].reals.size() > kMaxVertices * 3 || indices->props[0].ints.size() > kMaxCorners) {
            logf(LogLevel::Error, "{}: the mesh is larger than the importer accepts", file.string());
            return false;
        }
        geo.positions = vertices->props[0].reals;
        geo.polygonVertexIndex = indices->props[0].ints;
        if (const fbx::Node* n = g.child("LayerElementNormal")) {
            const fbx::Node* map = n->child("MappingInformationType");
            const std::string mapping = map != nullptr && !map->props.empty() ? map->props[0].text : std::string();
            if (mapping != "ByPolygonVertex" && mapping != "ByVertice" && mapping != "ByVertex" && mapping != "ByControlPoint") {
                logf(LogLevel::Error, "{}: normals mapped '{}' are not supported", file.string(), mapping);
                return false;
            }
            if (!loadLayerElement(*n, "Normals", "NormalsIndex", geo.normals, geo.normalIndex, geo.normalsIndexed, &geo.normalsByPolygonVertex)
                || geo.normals.size() % 3 != 0) {
                logf(LogLevel::Error, "{}: unreadable normals", file.string());
                return false;
            }
        }
        if (const fbx::Node* n = g.child("LayerElementUV")) {
            bool byPolygonVertex = false;
            if (!loadLayerElement(*n, "UV", "UVIndex", geo.uvs, geo.uvIndex, geo.uvsIndexed, &byPolygonVertex) || !byPolygonVertex || geo.uvs.size() % 2 != 0) {
                logf(LogLevel::Error, "{}: unreadable UVs", file.string());
                return false;
            }
        }
        if (const fbx::Node* n = g.child("LayerElementMaterial")) {
            const fbx::Node* map = n->child("MappingInformationType");
            const fbx::Node* values = n->child("Materials");
            geo.materialAllSame = map != nullptr && !map->props.empty() && map->props[0].text == "AllSame";
            if (values != nullptr && !values->props.empty()) geo.polygonMaterial = values->props[0].ints;
        }
        scene.hasGeometry = true;

        const auto skin = skinOfGeometry.find(geometryOfMesh);
        if (skin != skinOfGeometry.end()) {
            for (const auto& [clusterId, skinId] : skinOfCluster) {
                if (skinId != skin->second) continue;
                const auto bone = boneOfCluster.find(clusterId);
                if (bone == boneOfCluster.end()) continue;
                const fbx::Node& cluster = *deformers[clusterId];
                const fbx::Node* ci = cluster.child("Indexes");
                const fbx::Node* cw = cluster.child("Weights");
                const fbx::Node* link = cluster.child("TransformLink");
                if (link == nullptr || link->props.empty() || link->props[0].reals.size() != 16 || !finite(link->props[0].reals)) {
                    logf(LogLevel::Error, "{}: a skin cluster has no bind matrix", file.string());
                    return false;
                }
                Cluster out;
                out.id = clusterId;
                out.bone = bone->second;
                if (ci != nullptr && cw != nullptr && !ci->props.empty() && !cw->props.empty()) {
                    out.indices = ci->props[0].ints;
                    out.weights = cw->props[0].reals;
                    if (out.indices.size() != out.weights.size() || !finite(out.weights)) {
                        logf(LogLevel::Error, "{}: a skin cluster has mismatched weights", file.string());
                        return false;
                    }
                }
                out.transformLink = glm::make_mat4(link->props[0].reals.data());
                scene.clusters.push_back(std::move(out));
            }
            std::sort(scene.clusters.begin(), scene.clusters.end(), [](const Cluster& a, const Cluster& b) { return a.bone != b.bone ? a.bone < b.bone : a.id < b.id; });
        }
    }
    return true;
}

// A pose value that survives the cast to float and is not absurd: hostile files carry both kinds.
bool usable(const Trs& trs)
{
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(trs.t[i]) || std::abs(trs.t[i]) > kLimit || !std::isfinite(trs.s[i]) || std::abs(trs.s[i]) > kLimit) return false;
    }
    return std::isfinite(trs.q.x) && std::isfinite(trs.q.y) && std::isfinite(trs.q.z) && std::isfinite(trs.q.w);
}

glm::dmat4 localMatrix(const Model& m)
{
    Trs trs;
    trs.t = m.t;
    trs.q = glm::normalize(eulerXyz(m.preRotation) * eulerXyz(m.r) * glm::inverse(eulerXyz(m.postRotation)));
    trs.s = m.s;
    return toMatrix(trs);
}

// The world matrix of a model from its Lcl properties as the file stores them.
glm::dmat4 restWorld(const Scene& scene, const Model& m)
{
    glm::dmat4 world = localMatrix(m);
    const Model* at = scene.find(m.parent);
    while (at != nullptr) {
        world = localMatrix(*at) * world;
        at = scene.find(at->parent);
    }
    return world;
}

double sampleCurve(const Curve& c, double t, double fallback)
{
    if (c.times.empty()) return fallback;
    if (t <= c.times.front()) return c.values.front();
    if (t >= c.times.back()) return c.values.back();
    const auto upper = std::upper_bound(c.times.begin(), c.times.end(), t);
    const std::size_t i = static_cast<std::size_t>(upper - c.times.begin());
    const double u = (t - c.times[i - 1]) / (c.times[i] - c.times[i - 1]);
    return c.values[i - 1] + (c.values[i] - c.values[i - 1]) * u;
}

glm::dvec3 sampleVec(const Model& m, int channel, double t, const glm::dvec3& fallback)
{
    if (!m.animated[channel]) return fallback;
    return {sampleCurve(m.curves[channel][0], t, fallback.x), sampleCurve(m.curves[channel][1], t, fallback.y),
        sampleCurve(m.curves[channel][2], t, fallback.z)};
}

Trs animatedLocal(const Model& m, double t)
{
    Trs trs;
    trs.t = sampleVec(m, Translation, t, m.t);
    const glm::dvec3 euler = sampleVec(m, Rotation, t, m.r);
    trs.q = glm::normalize(eulerXyz(m.preRotation) * eulerXyz(euler) * glm::inverse(eulerXyz(m.postRotation)));
    trs.s = sampleVec(m, Scaling, t, m.s);
    return trs;
}

glm::dmat4 animatedWorldOfParent(const Scene& scene, const Model& m, double t)
{
    glm::dmat4 world(1.0);
    const Model* at = scene.find(m.parent);
    while (at != nullptr) {
        world = toMatrix(animatedLocal(*at, t)) * world;
        at = scene.find(at->parent);
    }
    return world;
}

glm::dmat4 uniformYaw(float degrees)
{
    return glm::rotate(glm::dmat4(1.0), glm::radians(static_cast<double>(degrees)), glm::dvec3(0, 1, 0));
}

glm::dmat4 sourceToModel(const Scene& scene, const FbxImportOptions& options)
{
    return uniformYaw(options.yawDegrees) * glm::scale(glm::dmat4(1.0), glm::dvec3(scene.metresPerUnit));
}

struct BuiltMesh {
    Mesh mesh;
    std::vector<std::size_t> sourceVertex; // the file vertex each output vertex came from
};

glm::vec4 tangentFor(const glm::vec3& n)
{
    const glm::vec3 axis = std::abs(n.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    return glm::vec4(glm::normalize(glm::cross(axis, n)), 1.0f);
}

// Triangulates the file's polygons into the bundle's vertex format. Positions and
// normals go through `transform` so the mesh is baked into model space.
bool buildMesh(const Scene& scene, const glm::dmat4& transform, std::uint32_t materialBase, std::uint32_t defaultMaterial,
    const std::string& name, BuiltMesh& out)
{
    const Geometry& geo = scene.geometry;
    const std::size_t vertexCount = geo.positions.size() / 3;
    const glm::dmat3 normalMatrix = glm::dmat3(transform);
    struct Corner {
        std::size_t vertex;
        glm::vec3 normal;
        glm::vec2 uv;
    };
    const auto cornerOf = [&](std::size_t polygonVertex, std::int64_t raw) -> std::optional<Corner> {
        const std::int64_t v = raw < 0 ? ~raw : raw;
        if (v < 0 || static_cast<std::size_t>(v) >= vertexCount) return std::nullopt;
        Corner c{static_cast<std::size_t>(v), glm::vec3(0.0f), glm::vec2(0.0f)};
        if (!geo.normals.empty()) {
            std::int64_t n = geo.normalsByPolygonVertex ? static_cast<std::int64_t>(polygonVertex) : v;
            if (geo.normalsIndexed) {
                if (n >= static_cast<std::int64_t>(geo.normalIndex.size())) return std::nullopt;
                n = geo.normalIndex[static_cast<std::size_t>(n)];
            }
            if (n < 0 || n >= static_cast<std::int64_t>(geo.normals.size() / 3)) return std::nullopt;
            c.normal = glm::vec3(geo.normals[n * 3], geo.normals[n * 3 + 1], geo.normals[n * 3 + 2]);
        }
        if (!geo.uvs.empty()) {
            std::int64_t u = static_cast<std::int64_t>(polygonVertex);
            if (geo.uvsIndexed) {
                if (u >= static_cast<std::int64_t>(geo.uvIndex.size())) return std::nullopt;
                u = geo.uvIndex[static_cast<std::size_t>(u)];
            }
            if (u < 0 || u >= static_cast<std::int64_t>(geo.uvs.size() / 2)) return std::nullopt;
            c.uv = glm::vec2(geo.uvs[u * 2], 1.0 - geo.uvs[u * 2 + 1]);
        }
        return c;
    };

    struct Triangle {
        std::array<Corner, 3> corners;
        std::size_t material;
    };
    std::vector<Triangle> triangles;
    std::size_t polygon = 0;
    std::vector<Corner> poly;
    for (std::size_t i = 0; i < geo.polygonVertexIndex.size(); ++i) {
        const std::int64_t raw = geo.polygonVertexIndex[i];
        const auto corner = cornerOf(i, raw);
        if (!corner) {
            logf(LogLevel::Error, "mesh {}: an index is out of range", name);
            return false;
        }
        poly.push_back(*corner);
        if (raw >= 0) continue;
        std::size_t material = 0;
        if (!geo.materialAllSame && polygon < geo.polygonMaterial.size() && geo.polygonMaterial[polygon] >= 0) {
            material = static_cast<std::size_t>(geo.polygonMaterial[polygon]);
        } else if (geo.materialAllSame && !geo.polygonMaterial.empty() && geo.polygonMaterial[0] >= 0) {
            material = static_cast<std::size_t>(geo.polygonMaterial[0]);
        }
        for (std::size_t k = 1; k + 1 < poly.size(); ++k) {
            triangles.push_back({{poly[0], poly[k], poly[k + 1]}, material});
        }
        poly.clear();
        ++polygon;
    }
    if (!poly.empty() || triangles.empty()) {
        logf(LogLevel::Error, "mesh {}: no complete polygons", name);
        return false;
    }

    // Triangles with the same material form one submesh, in material order.
    std::stable_sort(triangles.begin(), triangles.end(), [](const Triangle& a, const Triangle& b) { return a.material < b.material; });

    Mesh& mesh = out.mesh;
    mesh.name = name;
    std::map<std::pair<std::size_t, std::array<float, 5>>, std::uint32_t> welded;
    const bool flatNormals = geo.normals.empty();
    for (const Triangle& tri : triangles) {
        Submesh* sub = mesh.submeshes.empty() ? nullptr : &mesh.submeshes.back();
        const std::uint32_t material = tri.material < scene.materials.size() ? materialBase + static_cast<std::uint32_t>(tri.material) : defaultMaterial;
        if (sub == nullptr || sub->material != material) {
            mesh.submeshes.push_back({static_cast<std::uint32_t>(mesh.indices.size()), 0, material});
            sub = &mesh.submeshes.back();
        }
        glm::vec3 faceNormal(0.0f);
        if (flatNormals) {
            const auto p = [&](const Corner& c) { return glm::vec3(transform * glm::dvec4(geo.positions[c.vertex * 3], geo.positions[c.vertex * 3 + 1], geo.positions[c.vertex * 3 + 2], 1.0)); };
            const glm::vec3 n = glm::cross(p(tri.corners[1]) - p(tri.corners[0]), p(tri.corners[2]) - p(tri.corners[0]));
            faceNormal = glm::dot(n, n) > 0.0f ? glm::normalize(n) : glm::vec3(0, 1, 0);
        }
        for (const Corner& c : tri.corners) {
            const glm::dvec3 local(geo.positions[c.vertex * 3], geo.positions[c.vertex * 3 + 1], geo.positions[c.vertex * 3 + 2]);
            const glm::vec3 position = glm::vec3(transform * glm::dvec4(local, 1.0));
            if (!std::isfinite(position.x + position.y + position.z) || glm::compMax(glm::abs(position)) > 1.0e6f) {
                logf(LogLevel::Error, "mesh {}: a vertex is out of range", name);
                return false;
            }
            glm::vec3 normal = faceNormal;
            if (!flatNormals) {
                const glm::dvec3 n = normalMatrix * glm::dvec3(c.normal);
                normal = glm::dot(n, n) > 0.0 ? glm::vec3(glm::normalize(n)) : glm::vec3(0, 1, 0);
            }
            // Weld on the source vertex, normal and uv: coincident vertices that skin differently stay apart.
            const std::pair<std::size_t, std::array<float, 5>> key{c.vertex, {normal.x, normal.y, normal.z, c.uv.x, c.uv.y}};
            const auto [it, fresh] = welded.try_emplace(key, static_cast<std::uint32_t>(mesh.vertices.size()));
            if (fresh) {
                Vertex v{};
                v.position = position;
                v.normal = normal;
                v.tangent = tangentFor(normal);
                v.uv = c.uv;
                v.weights[0] = 65535;
                mesh.vertices.push_back(v);
                out.sourceVertex.push_back(c.vertex);
            }
            mesh.indices.push_back(it->second);
            ++sub->indexCount;
        }
    }
    mesh.boundsMin = mesh.boundsMax = mesh.vertices.front().position;
    for (const Vertex& v : mesh.vertices) {
        mesh.boundsMin = glm::min(mesh.boundsMin, v.position);
        mesh.boundsMax = glm::max(mesh.boundsMax, v.position);
    }
    return true;
}

std::uint32_t addMaterials(const Scene& scene, SceneBundle& bundle, std::uint32_t& defaultMaterial)
{
    const auto find = [&](const std::string& name) -> std::uint32_t {
        for (std::size_t i = 0; i < bundle.materials.size(); ++i) {
            if (bundle.materials[i].name == name) return static_cast<std::uint32_t>(i);
        }
        return kNoTexture;
    };
    if (find("exile_placeholder") == kNoTexture) {
        Material grey;
        grey.name = "exile_placeholder";
        grey.baseColorFactor = glm::vec4(0.55f, 0.52f, 0.48f, 1.0f);
        grey.metallicFactor = 0.0f;
        grey.roughnessFactor = 0.8f;
        bundle.materials.push_back(grey);
    }
    defaultMaterial = find("exile_placeholder");
    // The file's materials land consecutively from the returned base, which is what a
    // submesh's local material index is added to.
    const auto base = static_cast<std::uint32_t>(bundle.materials.size());
    for (const FbxMaterial& m : scene.materials) {
        Material material;
        material.name = m.name;
        material.baseColorFactor = glm::vec4(m.diffuse, 1.0f);
        material.metallicFactor = 0.0f;
        material.roughnessFactor = 0.75f;
        bundle.materials.push_back(material);
    }
    return base;
}

}

bool importFbxCharacter(const std::filesystem::path& file, const FbxImportOptions& options, SceneBundle& bundle, FbxRig& rig)
{
    Scene scene;
    if (!loadScene(file, scene)) return false;
    if (!scene.hasGeometry || scene.clusters.empty()) {
        logf(LogLevel::Error, "{}: no skinned mesh", file.string());
        return false;
    }
    const Model* meshModel = scene.find(scene.meshModel);
    if (meshModel == nullptr) {
        logf(LogLevel::Error, "{}: the mesh has no model", file.string());
        return false;
    }

    rig = FbxRig{};
    glm::dmat4 modelFromFile = sourceToModel(scene, options);
    const glm::dmat4 meshWorld = restWorld(scene, *meshModel);

    std::uint32_t defaultMaterial = 0;
    const std::uint32_t materialBase = addMaterials(scene, bundle, defaultMaterial);
    BuiltMesh built;
    if (!buildMesh(scene, modelFromFile * meshWorld, materialBase, defaultMaterial, meshModel->name, built)) return false;
    if (options.groundFeet) {
        const double lift = -static_cast<double>(built.mesh.boundsMin.y);
        modelFromFile = glm::translate(glm::dmat4(1.0), glm::dvec3(0.0, lift, 0.0)) * modelFromFile;
        for (Vertex& v : built.mesh.vertices) v.position.y += static_cast<float>(lift);
        built.mesh.boundsMin.y += static_cast<float>(lift);
        built.mesh.boundsMax.y += static_cast<float>(lift);
    }
    rig.modelFromFile = modelFromFile;

    // The joints: LimbNode models, bound by the clusters that name them.
    std::vector<const Model*> joints;
    for (const Model& m : scene.models) {
        if (m.isJoint()) joints.push_back(&m);
    }
    if (joints.size() >= kMaxJoints) {
        logf(LogLevel::Error, "{}: {} bones is more than a skeleton can hold", file.string(), joints.size());
        return false;
    }
    std::unordered_map<std::int64_t, const Cluster*> clusterOf;
    for (const Cluster& c : scene.clusters) clusterOf[c.bone] = &c;
    std::unordered_map<std::int64_t, glm::dmat4> bindOf;
    for (const Model* j : joints) {
        const auto it = clusterOf.find(j->id);
        bindOf[j->id] = modelFromFile * (it != clusterOf.end() ? it->second->transformLink : restWorld(scene, *j));
    }
    std::vector<const Model*> rootJoints;
    for (const Model* j : joints) {
        const Model* parent = scene.find(j->parent);
        if (parent == nullptr || !parent->isJoint()) rootJoints.push_back(j);
    }
    if (rootJoints.size() != 1) {
        logf(LogLevel::Error, "{}: expected one root bone, found {}", file.string(), rootJoints.size());
        return false;
    }
    for (const Model* j : joints) {
        if (j->name == kRootJoint) {
            logf(LogLevel::Error, "{}: a bone is already named root", file.string());
            return false;
        }
    }

    ozz::animation::offline::RawSkeleton raw;
    raw.roots.emplace_back();
    raw.roots.back().name = kRootJoint;
    raw.roots.back().transform = toOzz(Trs{});
    std::unordered_map<std::int64_t, std::vector<const Model*>> childrenOf;
    for (const Model* j : joints) childrenOf[j->parent].push_back(j);
    bool usableBinds = true;
    std::function<void(const Model*, const glm::dmat4&, ozz::animation::offline::RawSkeleton::Joint&)> build;
    build = [&](const Model* j, const glm::dmat4& parentWorld, ozz::animation::offline::RawSkeleton::Joint& out) {
        out.name = j->name;
        const Trs local = decomposeMatrix(glm::inverse(parentWorld) * bindOf[j->id]);
        // A bind pose with a collapsed or enormous scale cannot be inverted into a skinning matrix.
        usableBinds = usableBinds && usable(local) && local.s.x > 1e-4 && local.s.y > 1e-4 && local.s.z > 1e-4;
        out.transform = toOzz(local);
        for (const Model* child : childrenOf[j->id]) {
            out.children.emplace_back();
            build(child, bindOf[j->id], out.children.back());
        }
    };
    raw.roots.back().children.emplace_back();
    build(rootJoints[0], glm::dmat4(1.0), raw.roots.back().children.back());
    if (!usableBinds) {
        logf(LogLevel::Error, "{}: the bind pose holds values no skeleton could use", file.string());
        return false;
    }
    if (!raw.Validate()) {
        logf(LogLevel::Error, "{}: invalid skeleton", file.string());
        return false;
    }
    ozz::animation::offline::SkeletonBuilder builder;
    ozz::unique_ptr<ozz::animation::Skeleton> skeleton = builder(raw);
    if (!skeleton) {
        logf(LogLevel::Error, "{}: skeleton build failed", file.string());
        return false;
    }

    Skeleton cooked;
    cooked.name = std::filesystem::path(file).stem().string();
    std::unordered_map<std::string, std::uint16_t> indexOfName;
    const auto names = skeleton->joint_names();
    for (int i = 0; i < skeleton->num_joints(); ++i) {
        cooked.jointNames.emplace_back(names[i]);
        indexOfName[names[i]] = static_cast<std::uint16_t>(i);
    }
    if (indexOfName.size() != static_cast<std::size_t>(skeleton->num_joints())) {
        logf(LogLevel::Error, "{}: bone names must be unique", file.string());
        return false;
    }
    cooked.inverseBind.assign(cooked.jointNames.size(), glm::mat4(1.0f));
    rig.bindWorld.assign(cooked.jointNames.size(), glm::mat4(1.0f));
    for (const Model* j : joints) {
        const std::uint16_t index = indexOfName[j->name];
        rig.bindWorld[index] = glm::mat4(bindOf[j->id]);
        cooked.inverseBind[index] = glm::mat4(glm::inverse(bindOf[j->id]));
    }
    rig.bindRoot = glm::dvec3(bindOf[rootJoints[0]->id][3]);
    rig.jointNames = cooked.jointNames;
    rig.jointParent.assign(cooked.jointNames.size(), -1);
    for (const Model* j : joints) {
        const Model* parent = scene.find(j->parent);
        rig.jointParent[indexOfName[j->name]] = parent != nullptr && parent->isJoint() ? indexOfName[parent->name] : 0;
    }
    if (!serialize(*skeleton, cooked.ozzBytes)) return false;

    // Skin weights: the four strongest bones per vertex, renormalised.
    std::vector<std::vector<std::pair<std::uint16_t, float>>> influences(scene.geometry.positions.size() / 3);
    for (const Cluster& c : scene.clusters) {
        const Model* bone = scene.find(c.bone);
        if (bone == nullptr || !indexOfName.contains(bone->name)) continue;
        const std::uint16_t joint = indexOfName[bone->name];
        for (std::size_t i = 0; i < c.indices.size(); ++i) {
            if (c.indices[i] < 0 || static_cast<std::size_t>(c.indices[i]) >= influences.size() || !(c.weights[i] > 0.0)) continue;
            influences[static_cast<std::size_t>(c.indices[i])].emplace_back(joint, static_cast<float>(c.weights[i]));
        }
    }
    for (auto& list : influences) {
        std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        if (list.size() > 4) list.resize(4);
    }
    Mesh& mesh = built.mesh;
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const auto& list = influences[built.sourceVertex[i]];
        Vertex& v = mesh.vertices[i];
        for (int k = 0; k < 4; ++k) {
            v.joints[k] = 0;
            v.weights[k] = 0;
        }
        float sum = 0.0f;
        for (const auto& [joint, weight] : list) sum += weight;
        if (!(sum > 0.0f)) {
            v.joints[0] = indexOfName[rootJoints[0]->name];
            v.weights[0] = 65535;
            ++rig.unweightedVertices;
            continue;
        }
        std::uint32_t total = 0;
        for (std::size_t k = 0; k < list.size(); ++k) {
            v.joints[k] = list[k].first;
            v.weights[k] = static_cast<std::uint16_t>(std::lround(list[k].second / sum * 65535.0f));
            total += v.weights[k];
        }
        // The strongest bone absorbs the rounding so the weights sum to exactly 65535.
        v.weights[0] = static_cast<std::uint16_t>(v.weights[0] + (65535 - static_cast<std::int32_t>(total)));
    }

    rig.skeleton = static_cast<std::uint32_t>(bundle.skeletons.size());
    mesh.skeleton = rig.skeleton;
    bundle.skeletons.push_back(std::move(cooked));
    rig.characterMesh = static_cast<std::uint32_t>(bundle.meshes.size());
    bundle.meshes.push_back(std::move(built.mesh));
    return true;
}

bool importFbxClip(const std::filesystem::path& file, std::string_view clipName, const FbxImportOptions& options, const FbxRig& rig,
    SceneBundle& bundle)
{
    if (rig.skeleton == kNoSkeleton || rig.skeleton >= bundle.skeletons.size()) {
        logf(LogLevel::Error, "{}: no skeleton to animate", file.string());
        return false;
    }
    Scene scene;
    if (!loadScene(file, scene)) return false;
    const Skeleton& skeleton = bundle.skeletons[rig.skeleton];

    // Which model drives which joint, by name. The synthetic root has none.
    std::vector<const Model*> driver(skeleton.jointNames.size(), nullptr);
    const Model* rootBone = nullptr;
    for (std::size_t j = 0; j < skeleton.jointNames.size(); ++j) {
        driver[j] = scene.findByName(skeleton.jointNames[j]);
        if (driver[j] != nullptr && j == 1) rootBone = driver[j];
    }
    if (rootBone == nullptr) {
        logf(LogLevel::Error, "{}: the clip does not animate the root bone {}", file.string(), skeleton.jointNames.size() > 1 ? skeleton.jointNames[1] : "?");
        return false;
    }
    std::size_t missing = 0;
    for (std::size_t j = 1; j < driver.size(); ++j) {
        if (driver[j] == nullptr) ++missing;
    }
    if (missing != 0) {
        logf(LogLevel::Warn, "{}: {} bones of the skeleton are not in the clip and keep their bind pose", file.string(), missing);
    }

    double start = 0.0;
    double end = 0.0;
    bool any = false;
    for (const Model& m : scene.models) {
        for (int channel = 0; channel < 3; ++channel) {
            for (const Curve& c : m.curves[channel]) {
                if (c.times.empty()) continue;
                start = any ? std::min(start, c.times.front()) : c.times.front();
                end = any ? std::max(end, c.times.back()) : c.times.back();
                any = true;
            }
        }
    }
    const double duration = end - start;
    if (!any || !(duration > 1.0 / kSampleRate) || duration > 120.0) {
        logf(LogLevel::Error, "{}: the clip has no usable animation", file.string());
        return false;
    }

    // The rig carries the whole conversion, the lift that grounds the feet included.
    const glm::dmat4& modelFromFile = rig.modelFromFile;
    ozz::animation::offline::RawAnimation anim;
    anim.name = std::string(clipName);
    anim.duration = static_cast<float>(duration);
    anim.tracks.resize(skeleton.jointNames.size());
    // Key times are ticks over a large constant, so a whole number of frames can land a hair high:
    // without the slack the last two keys would round to the same float.
    const auto frames = static_cast<int>(std::ceil(duration * kSampleRate - 1.0e-6)) + 1;

    // Joint rest locals come from the cooked bind world matrices and the skeleton's own parent links.
    std::vector<Trs> rest(skeleton.jointNames.size());
    for (std::size_t j = 1; j < rest.size(); ++j) {
        const std::int32_t parent = j < rig.jointParent.size() ? rig.jointParent[j] : -1;
        const glm::dmat4 parentWorld = parent <= 0 ? glm::dmat4(1.0) : glm::dmat4(rig.bindWorld[static_cast<std::size_t>(parent)]);
        rest[j] = decomposeMatrix(glm::inverse(parentWorld) * glm::dmat4(rig.bindWorld[j]));
    }

    glm::dvec3 first(0.0);
    for (int f = 0; f < frames; ++f) {
        const double local = std::min(duration, static_cast<double>(f) / kSampleRate);
        const double t = start + local;
        for (std::size_t j = 1; j < driver.size(); ++j) {
            Trs trs = rest[j];
            if (driver[j] != nullptr) trs = animatedLocal(*driver[j], t);
            if (j == 1) {
                // The root bone's place in model space, then split: the horizontal travel goes to
                // the synthetic root joint, the height and the bone's own turning stay on the bone.
                const glm::dmat4 world = modelFromFile * animatedWorldOfParent(scene, *driver[j], t) * toMatrix(trs);
                const Trs placed = decomposeMatrix(world);
                if (!usable(placed)) {
                    logf(LogLevel::Error, "{}: the clip holds values no skeleton could use", file.string());
                    return false;
                }
                if (f == 0) first = placed.t;
                glm::dvec3 travel(placed.t.x - first.x, 0.0, placed.t.z - first.z);
                if (options.reverseRootTravel) travel = -travel;
                anim.tracks[0].translations.push_back({static_cast<float>(local), {static_cast<float>(travel.x), 0.0f, static_cast<float>(travel.z)}});
                anim.tracks[0].rotations.push_back({static_cast<float>(local), {0.0f, 0.0f, 0.0f, 1.0f}});
                anim.tracks[0].scales.push_back({static_cast<float>(local), {1.0f, 1.0f, 1.0f}});
                trs = placed;
                trs.t = glm::dvec3(rig.bindRoot.x, placed.t.y, rig.bindRoot.z);
            }
            if (!usable(trs)) {
                logf(LogLevel::Error, "{}: the clip holds values no skeleton could use", file.string());
                return false;
            }
            auto& track = anim.tracks[j];
            const ozz::math::Transform o = toOzz(trs);
            track.translations.push_back({static_cast<float>(local), o.translation});
            track.rotations.push_back({static_cast<float>(local), o.rotation});
            track.scales.push_back({static_cast<float>(local), o.scale});
        }
    }
    if (!anim.Validate()) {
        logf(LogLevel::Error, "{}: invalid animation", file.string());
        return false;
    }
    ozz::animation::offline::AnimationBuilder animBuilder;
    ozz::unique_ptr<ozz::animation::Animation> built = animBuilder(anim);
    if (!built) {
        logf(LogLevel::Error, "{}: animation build failed", file.string());
        return false;
    }
    AnimationClip clip;
    clip.name = std::string(clipName);
    clip.skeleton = rig.skeleton;
    clip.duration = static_cast<float>(duration);
    if (!serialize(*built, clip.ozzBytes)) return false;
    bundle.clips.push_back(std::move(clip));
    return true;
}

bool importFbxClipFirstPose(const std::filesystem::path& file, const FbxRig& rig, const Skeleton& skeleton, std::vector<glm::mat4>& worlds)
{
    Scene scene;
    if (!loadScene(file, scene)) return false;
    double start = 0.0;
    bool any = false;
    for (const Model& m : scene.models) {
        for (int channel = 0; channel < 3; ++channel) {
            for (const Curve& c : m.curves[channel]) {
                if (c.times.empty()) continue;
                start = any ? std::min(start, c.times.front()) : c.times.front();
                any = true;
            }
        }
    }
    if (!any || skeleton.jointNames.size() < 2) return false;
    std::unordered_map<std::string, std::size_t> indexOfName;
    for (std::size_t j = 0; j < skeleton.jointNames.size(); ++j) indexOfName[skeleton.jointNames[j]] = j;
    worlds.assign(skeleton.jointNames.size(), glm::mat4(1.0f));
    std::vector<glm::dmat4> placed(skeleton.jointNames.size(), glm::dmat4(1.0));
    for (std::size_t j = 1; j < skeleton.jointNames.size(); ++j) {
        const Model* model = scene.findByName(skeleton.jointNames[j]);
        if (model == nullptr) {
            placed[j] = glm::dmat4(rig.bindWorld[j]);
        } else if (j == 1) {
            placed[j] = rig.modelFromFile * animatedWorldOfParent(scene, *model, start) * toMatrix(animatedLocal(*model, start));
        } else {
            const Model* parent = scene.find(model->parent);
            const auto at = parent != nullptr ? indexOfName.find(parent->name) : indexOfName.end();
            if (at == indexOfName.end() || at->second >= j) return false;
            placed[j] = placed[at->second] * toMatrix(animatedLocal(*model, start));
        }
        worlds[j] = glm::mat4(placed[j]);
    }
    return true;
}

bool importFbxProp(const std::filesystem::path& file, const FbxRig& rig, SceneBundle& bundle, std::uint32_t& meshIndex)
{
    Scene scene;
    if (!loadScene(file, scene)) return false;
    const Model* model = scene.find(scene.meshModel);
    if (!scene.hasGeometry || model == nullptr) {
        logf(LogLevel::Error, "{}: no mesh", file.string());
        return false;
    }
    std::uint32_t defaultMaterial = 0;
    const std::uint32_t materialBase = addMaterials(scene, bundle, defaultMaterial);
    BuiltMesh built;
    if (!buildMesh(scene, rig.modelFromFile * restWorld(scene, *model), materialBase, defaultMaterial, model->name, built)) return false;
    meshIndex = static_cast<std::uint32_t>(bundle.meshes.size());
    bundle.meshes.push_back(std::move(built.mesh));
    return true;
}

std::uint32_t nearestJoint(const FbxRig& rig, const std::vector<glm::mat4>& jointWorlds, const Mesh& mesh,
    std::initializer_list<std::string_view> jointNames)
{
    const glm::vec3 centre = (mesh.boundsMin + mesh.boundsMax) * 0.5f;
    std::uint32_t best = kNoJoint;
    float bestDistance = 0.0f;
    for (std::size_t j = 0; j < rig.jointNames.size() && j < jointWorlds.size(); ++j) {
        if (std::find(jointNames.begin(), jointNames.end(), rig.jointNames[j]) == jointNames.end()) continue;
        const float d = glm::length(glm::vec3(jointWorlds[j][3]) - centre);
        if (best == kNoJoint || d < bestDistance) {
            best = static_cast<std::uint32_t>(j);
            bestDistance = d;
        }
    }
    return best;
}

}
