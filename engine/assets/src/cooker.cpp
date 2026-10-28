#include "ichor/assets/cooker.h"

#include "ichor/assets/image.h"
#include "ichor/assets/piece_conventions.h"
#include "ichor/assets/skeleton_cook.h"
#include "ichor/core/log.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <meshoptimizer.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <unordered_map>

namespace ichor {

namespace {

std::uint32_t textureIndex(const cgltf_data& data, const cgltf_texture* texture)
{
    if (texture == nullptr || texture->image == nullptr) {
        return kNoTexture;
    }
    return static_cast<std::uint32_t>(texture->image - data.images);
}

bool readAccessor(const cgltf_accessor* accessor, cgltf_size components, std::vector<float>& out)
{
    if (accessor == nullptr) {
        return false;
    }
    out.resize(accessor->count * components);
    for (cgltf_size i = 0; i < accessor->count; ++i) {
        if (!cgltf_accessor_read_float(accessor, i, out.data() + i * components, components)) {
            return false;
        }
    }
    return true;
}

bool readJoints(const cgltf_accessor* accessor, std::vector<std::uint32_t>& out)
{
    if (accessor == nullptr) {
        return false;
    }
    out.resize(accessor->count * 4);
    for (cgltf_size i = 0; i < accessor->count; ++i) {
        if (!cgltf_accessor_read_uint(accessor, i, out.data() + i * 4, 4)) {
            return false;
        }
    }
    return true;
}

// Per-triangle tangents accumulated per vertex, for meshes exported without
// them. Good enough for flat kits; characters get theirs from the exporter.
void generateTangents(std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices)
{
    std::vector<glm::vec3> tan(vertices.size(), glm::vec3(0.0f));
    std::vector<glm::vec3> bitan(vertices.size(), glm::vec3(0.0f));
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
        const Vertex& a = vertices[indices[i]];
        const Vertex& b = vertices[indices[i + 1]];
        const Vertex& c = vertices[indices[i + 2]];
        const glm::vec3 e1 = b.position - a.position;
        const glm::vec3 e2 = c.position - a.position;
        const glm::vec2 d1 = b.uv - a.uv;
        const glm::vec2 d2 = c.uv - a.uv;
        const float det = d1.x * d2.y - d2.x * d1.y;
        if (std::abs(det) < 1e-12f) {
            continue;
        }
        const float r = 1.0f / det;
        const glm::vec3 t = (e1 * d2.y - e2 * d1.y) * r;
        const glm::vec3 bt = (e2 * d1.x - e1 * d2.x) * r;
        for (int k = 0; k < 3; ++k) {
            tan[indices[i + k]] += t;
            bitan[indices[i + k]] += bt;
        }
    }
    for (std::size_t v = 0; v < vertices.size(); ++v) {
        const glm::vec3 n = vertices[v].normal;
        glm::vec3 t = tan[v] - n * glm::dot(n, tan[v]);
        if (glm::dot(t, t) < 1e-12f) {
            t = std::abs(n.x) < 0.9f ? glm::cross(n, glm::vec3(1.0f, 0.0f, 0.0f)) : glm::cross(n, glm::vec3(0.0f, 1.0f, 0.0f));
        }
        t = glm::normalize(t);
        const float w = glm::dot(glm::cross(n, t), bitan[v]) < 0.0f ? -1.0f : 1.0f;
        vertices[v].tangent = glm::vec4(t, w);
    }
}

void optimizeMesh(Mesh& mesh, CookStats& stats)
{
    stats.verticesIn += static_cast<std::uint32_t>(mesh.vertices.size());

    std::vector<unsigned int> remap(mesh.indices.size());
    const std::size_t uniqueVertices = meshopt_generateVertexRemap(remap.data(), mesh.indices.data(), mesh.indices.size(),
        mesh.vertices.data(), mesh.vertices.size(), sizeof(Vertex));

    std::vector<std::uint32_t> indices(mesh.indices.size());
    std::vector<Vertex> vertices(uniqueVertices);
    meshopt_remapIndexBuffer(indices.data(), mesh.indices.data(), mesh.indices.size(), remap.data());
    meshopt_remapVertexBuffer(vertices.data(), mesh.vertices.data(), mesh.vertices.size(), sizeof(Vertex), remap.data());

    for (const Submesh& sub : mesh.submeshes) {
        meshopt_optimizeVertexCache(indices.data() + sub.firstIndex, indices.data() + sub.firstIndex, sub.indexCount, vertices.size());
        meshopt_optimizeOverdraw(indices.data() + sub.firstIndex, indices.data() + sub.firstIndex, sub.indexCount,
            &vertices[0].position.x, vertices.size(), sizeof(Vertex), 1.05f);
    }
    const std::size_t fetched = meshopt_optimizeVertexFetch(vertices.data(), indices.data(), indices.size(), vertices.data(), vertices.size(), sizeof(Vertex));
    vertices.resize(fetched);

    mesh.vertices = std::move(vertices);
    mesh.indices = std::move(indices);
    stats.verticesOut += static_cast<std::uint32_t>(mesh.vertices.size());
    stats.triangles += static_cast<std::uint32_t>(mesh.indices.size() / 3);
}

// `jointRemap` maps glTF skin joint slots (what JOINTS_0 indexes) to ozz joint indices.
bool cookMesh(const cgltf_data& data, const cgltf_mesh& source, const std::vector<std::uint16_t>* jointRemap, Mesh& mesh, CookStats& stats)
{
    mesh.name = source.name != nullptr ? source.name : "";
    mesh.boundsMin = glm::vec3(std::numeric_limits<float>::max());
    mesh.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());

    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> tangents;
    std::vector<float> uvs;
    std::vector<float> weights;
    std::vector<std::uint32_t> joints;
    bool needTangents = false;

    for (cgltf_size p = 0; p < source.primitives_count; ++p) {
        const cgltf_primitive& prim = source.primitives[p];
        joints.clear();
        weights.clear();
        if (prim.type != cgltf_primitive_type_triangles || prim.indices == nullptr) {
            logf(LogLevel::Warn, "mesh {}: skipping non-indexed or non-triangle primitive", mesh.name);
            continue;
        }
        const cgltf_accessor* posAcc = nullptr;
        const cgltf_accessor* normAcc = nullptr;
        const cgltf_accessor* tanAcc = nullptr;
        const cgltf_accessor* uvAcc = nullptr;
        const cgltf_accessor* jointAcc = nullptr;
        const cgltf_accessor* weightAcc = nullptr;
        for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
            const cgltf_attribute& attr = prim.attributes[a];
            if (attr.type == cgltf_attribute_type_position) posAcc = attr.data;
            if (attr.type == cgltf_attribute_type_normal) normAcc = attr.data;
            if (attr.type == cgltf_attribute_type_tangent) tanAcc = attr.data;
            if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) uvAcc = attr.data;
            if (attr.type == cgltf_attribute_type_joints && attr.index == 0) jointAcc = attr.data;
            if (attr.type == cgltf_attribute_type_weights && attr.index == 0) weightAcc = attr.data;
        }
        if (!readAccessor(posAcc, 3, positions) || !readAccessor(normAcc, 3, normals)) {
            logf(LogLevel::Error, "mesh {}: primitive without positions or normals", mesh.name);
            return false;
        }
        const bool hasUv = readAccessor(uvAcc, 2, uvs);
        const bool hasTangents = readAccessor(tanAcc, 4, tangents);
        const bool hasSkin = jointRemap != nullptr && readJoints(jointAcc, joints) && readAccessor(weightAcc, 4, weights);
        if (normAcc->count != posAcc->count || (hasUv && uvAcc->count != posAcc->count) || (hasTangents && tanAcc->count != posAcc->count)
            || (hasSkin && (jointAcc->count != posAcc->count || weightAcc->count != posAcc->count))) {
            logf(LogLevel::Error, "mesh {}: attribute counts disagree", mesh.name);
            return false;
        }
        needTangents = needTangents || !hasTangents;

        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (cgltf_size v = 0; v < posAcc->count; ++v) {
            Vertex vertex{};
            vertex.position = glm::make_vec3(positions.data() + v * 3);
            vertex.normal = glm::make_vec3(normals.data() + v * 3);
            vertex.tangent = hasTangents ? glm::make_vec4(tangents.data() + v * 4) : glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
            vertex.uv = hasUv ? glm::make_vec2(uvs.data() + v * 2) : glm::vec2(0.0f);
            vertex.weights[0] = 65535;
            float sum = 0.0f;
            if (hasSkin) {
                for (int k = 0; k < 4; ++k) {
                    sum += weights[v * 4 + k];
                }
            }
            // An unweighted vertex rides the first joint rigidly rather than collapsing to the origin.
            if (hasSkin && sum > 0.0f) {
                const float scale = 65535.0f / sum;
                std::uint32_t total = 0;
                int largest = 0;
                for (int k = 0; k < 4; ++k) {
                    const std::uint32_t slot = joints[v * 4 + k];
                    if (slot >= jointRemap->size()) {
                        logf(LogLevel::Error, "mesh {}: joint index {} outside the skin", mesh.name, slot);
                        return false;
                    }
                    vertex.joints[k] = (*jointRemap)[slot];
                    vertex.weights[k] = static_cast<std::uint16_t>(std::lround(weights[v * 4 + k] * scale));
                    total += vertex.weights[k];
                    if (vertex.weights[k] > vertex.weights[largest]) largest = k;
                }
                // Rounding leaves the four a unit or two short of one; the largest absorbs it.
                vertex.weights[largest] = static_cast<std::uint16_t>(vertex.weights[largest] + (65535 - static_cast<int>(total)));
            }
            mesh.boundsMin = glm::min(mesh.boundsMin, vertex.position);
            mesh.boundsMax = glm::max(mesh.boundsMax, vertex.position);
            mesh.vertices.push_back(vertex);
        }

        Submesh sub;
        sub.firstIndex = static_cast<std::uint32_t>(mesh.indices.size());
        sub.indexCount = static_cast<std::uint32_t>(prim.indices->count);
        sub.material = prim.material != nullptr ? static_cast<std::uint32_t>(prim.material - data.materials) : 0;
        for (cgltf_size i = 0; i < prim.indices->count; ++i) {
            mesh.indices.push_back(base + static_cast<std::uint32_t>(cgltf_accessor_read_index(prim.indices, i)));
        }
        mesh.submeshes.push_back(sub);
    }

    if (mesh.vertices.empty()) {
        logf(LogLevel::Error, "mesh {}: no triangle primitives", mesh.name);
        return false;
    }
    if (needTangents) {
        generateTangents(mesh.vertices, mesh.indices);
        stats.tangentsGenerated += static_cast<std::uint32_t>(mesh.vertices.size());
    }
    optimizeMesh(mesh, stats);
    return true;
}

bool cookTexture(const cgltf_image& image, TextureUse use, std::uint32_t maxSize, Texture& texture)
{
    const std::string name = image.name != nullptr ? image.name : "";
    if (image.buffer_view == nullptr) {
        logf(LogLevel::Error, "texture {}: external images are not supported, embed them in the GLB", name);
        return false;
    }
    const auto* bytes = static_cast<const std::uint8_t*>(image.buffer_view->buffer->data) + image.buffer_view->offset;
    DecodedImage decoded;
    if (!decodeImage(bytes, image.buffer_view->size, decoded)) {
        logf(LogLevel::Error, "texture {}: cannot be decoded", name);
        return false;
    }
    const TextureFormat format = chooseTextureFormat(use, decoded.sourceChannels == 1);
    if (!cookCompressedTexture(std::move(decoded), format, maxSize, texture)) {
        logf(LogLevel::Error, "texture {}: cannot be compressed", name);
        return false;
    }
    texture.name = name;
    return true;
}

// The size table to cook `source` with: the one in the options, else art/texture_budget.json from the art folder above the file.
TextureBudget textureBudgetFor(const CookOptions& options, const std::filesystem::path& source)
{
    if (options.textureBudget) {
        return *options.textureBudget;
    }
    static std::mutex mutex;
    static std::map<std::filesystem::path, TextureBudget> cache;
    std::error_code ec;
    for (std::filesystem::path dir = std::filesystem::absolute(source, ec).parent_path(); !dir.empty() && dir != dir.parent_path(); dir = dir.parent_path()) {
        if (dir.filename() != "art") {
            continue;
        }
        const std::lock_guard lock(mutex);
        const auto found = cache.find(dir);
        if (found != cache.end()) {
            return found->second;
        }
        TextureBudget budget;
        std::vector<std::string> problems;
        if (std::filesystem::exists(dir / "texture_budget.json", ec)) {
            const bool loaded = loadTextureBudget(dir / "texture_budget.json", budget, problems);
            for (const std::string& problem : problems) {
                logf(LogLevel::Warn, "texture_budget.json: {}", problem);
            }
            if (!loaded) {
                logf(LogLevel::Warn, "{} could not be read, cooking with the default texture size cap", (dir / "texture_budget.json").string());
            }
            return cache.emplace(dir, loaded ? std::move(budget) : TextureBudget{}).first->second;
        }
    }
    return {};
}

// A parsed, buffer-loaded and validated glTF, or null with the reason logged.
cgltf_data* loadValidated(const std::filesystem::path& source)
{
    const std::string file = source.string();
    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, file.c_str(), &data) != cgltf_result_success) {
        logf(LogLevel::Error, "cannot parse {}", file);
        return nullptr;
    }
    if (cgltf_load_buffers(&options, data, file.c_str()) != cgltf_result_success) {
        logf(LogLevel::Error, "cannot load buffers for {}", file);
        cgltf_free(data);
        return nullptr;
    }
    if (cgltf_validate(data) != cgltf_result_success) {
        logf(LogLevel::Error, "{} fails glTF validation", file);
        cgltf_free(data);
        return nullptr;
    }
    return data;
}

}

bool inspectGltf(const std::filesystem::path& source, ConventionScan& scan)
{
    cgltf_data* data = loadValidated(source);
    if (data == nullptr) {
        return false;
    }
    scanConventions(*data, scan);
    cgltf_free(data);
    return true;
}

bool cookGltf(const std::filesystem::path& source, SceneBundle& bundle, CookStats& stats, const CookOptions& cookOptions)
{
    PieceData discarded;
    return cookGltf(source, bundle, discarded, stats, cookOptions);
}

bool cookGltf(const std::filesystem::path& source, SceneBundle& bundle, PieceData& pieces, CookStats& stats, const CookOptions& cookOptions)
{
    cgltf_data* data = loadValidated(source);
    if (data == nullptr) {
        return false;
    }

    // A texture's format depends on how a material uses it: colour is sRGB, a normal map is
    // two channels, everything else is data. An image used two ways keeps the colour or data reading.
    std::vector<bool> asColor(data->images_count, false);
    std::vector<bool> asData(data->images_count, false);
    std::vector<bool> asNormal(data->images_count, false);
    for (cgltf_size m = 0; m < data->materials_count; ++m) {
        const cgltf_material& material = data->materials[m];
        const std::uint32_t color = textureIndex(*data, material.pbr_metallic_roughness.base_color_texture.texture);
        const std::uint32_t metalRough = textureIndex(*data, material.pbr_metallic_roughness.metallic_roughness_texture.texture);
        const std::uint32_t normal = textureIndex(*data, material.normal_texture.texture);
        if (color != kNoTexture) asColor[color] = true;
        if (metalRough != kNoTexture) asData[metalRough] = true;
        if (normal != kNoTexture) asNormal[normal] = true;
    }

    std::error_code absoluteEc;
    const std::filesystem::path absoluteSource = std::filesystem::absolute(source, absoluteEc).lexically_normal();
    const TextureBudget budget = textureBudgetFor(cookOptions, absoluteSource);
    const std::uint32_t maxSize = maxTextureSize(budget, absoluteSource);
    bool ok = true;
    bundle.textures.resize(data->images_count);
    for (cgltf_size i = 0; i < data->images_count && ok; ++i) {
        const TextureUse use = asColor[i] ? TextureUse::Color : (asNormal[i] && !asData[i] ? TextureUse::Normal : TextureUse::Data);
        ok = cookTexture(data->images[i], use, maxSize, bundle.textures[i]);
    }

    bundle.materials.resize(data->materials_count);
    for (cgltf_size m = 0; m < data->materials_count; ++m) {
        const cgltf_material& src = data->materials[m];
        Material& dst = bundle.materials[m];
        dst.name = src.name != nullptr ? src.name : "";
        const auto& pbr = src.pbr_metallic_roughness;
        dst.baseColorFactor = glm::make_vec4(pbr.base_color_factor);
        dst.metallicFactor = pbr.metallic_factor;
        dst.roughnessFactor = pbr.roughness_factor;
        dst.baseColorTexture = textureIndex(*data, pbr.base_color_texture.texture);
        dst.metallicRoughnessTexture = textureIndex(*data, pbr.metallic_roughness_texture.texture);
        dst.normalTexture = textureIndex(*data, src.normal_texture.texture);
    }
    if (bundle.materials.empty()) {
        bundle.materials.push_back(Material{.name = "default"});
    }

    // Skins become skeletons; each remembers how its glTF joint slots map to ozz joints.
    std::vector<std::vector<std::uint16_t>> jointRemaps(data->skins_count);
    for (cgltf_size s = 0; s < data->skins_count && ok; ++s) {
        Skeleton skeleton;
        ok = cookSkeleton(*data, data->skins[s], skeleton, jointRemaps[s], cookOptions.rootYawDegrees);
        if (ok) {
            bundle.skeletons.push_back(std::move(skeleton));
        }
    }
    for (cgltf_size a = 0; a < data->animations_count && ok; ++a) {
        for (cgltf_size s = 0; s < data->skins_count; ++s) {
            AnimationClip clip;
            if (cookClip(*data, data->animations[a], data->skins[s], bundle.skeletons[s], static_cast<std::uint32_t>(s), clip, cookOptions.rootYawDegrees)) {
                bundle.clips.push_back(std::move(clip));
                stats.clips += 1;
            }
        }
    }

    ConventionScan conventions;
    scanConventions(*data, conventions);
    for (const std::string& issue : conventions.issues) {
        logf(LogLevel::Warn, "{}: {}", source.filename().string(), issue);
    }

    // Meshes are cooked once per glTF mesh; skinning data comes from the first
    // node's skin, which is how exporters pair them.
    std::unordered_map<const cgltf_mesh*, std::uint32_t> meshIndex;
    for (cgltf_size n = 0; n < data->nodes_count && ok; ++n) {
        const cgltf_node& src = data->nodes[n];
        if (src.mesh == nullptr || meshIndex.contains(src.mesh) || conventions.roles[n] == NodeRole::Collision) {
            continue;
        }
        Mesh mesh;
        const std::vector<std::uint16_t>* remap = src.skin != nullptr ? &jointRemaps[src.skin - data->skins] : nullptr;
        if (!cookMesh(*data, *src.mesh, remap, mesh, stats)) {
            ok = false;
            break;
        }
        mesh.skeleton = src.skin != nullptr ? static_cast<std::uint32_t>(src.skin - data->skins) : kNoSkeleton;
        meshIndex[src.mesh] = static_cast<std::uint32_t>(bundle.meshes.size());
        bundle.meshes.push_back(std::move(mesh));
    }
    std::vector<bool> collisionMesh(data->meshes_count, false);
    for (cgltf_size n = 0; n < data->nodes_count; ++n) {
        if (conventions.roles[n] == NodeRole::Collision) {
            collisionMesh[static_cast<std::size_t>(data->nodes[n].mesh - data->meshes)] = true;
        }
    }
    for (cgltf_size m = 0; m < data->meshes_count; ++m) {
        if (!meshIndex.contains(&data->meshes[m]) && !collisionMesh[m]) {
            logf(LogLevel::Warn, "mesh {}: no node references it, dropped", data->meshes[m].name != nullptr ? data->meshes[m].name : "");
        }
    }

    for (cgltf_size n = 0; n < data->nodes_count && ok; ++n) {
        const cgltf_node& src = data->nodes[n];
        const auto found = src.mesh != nullptr ? meshIndex.find(src.mesh) : meshIndex.end();
        if (found == meshIndex.end() || conventions.roles[n] == NodeRole::Collision || conventions.roles[n] == NodeRole::LodExtra) {
            continue;
        }
        Node node;
        node.name = src.name != nullptr ? src.name : "";
        node.mesh = found->second;
        float world[16];
        cgltf_node_transform_world(&src, world);
        node.world = glm::make_mat4(world);
        bundle.nodes.push_back(node);
    }

    if (ok) {
        pieces.sockets = std::move(conventions.sockets);
        pieces.collision = std::move(conventions.collision);
        for (const ConventionScan::Lod& lod : conventions.lods) {
            LodGroup group;
            group.piece = lod.piece;
            for (std::size_t level = 0; level < lod.nodes.size(); ++level) {
                group.levels.push_back(LodLevel{.mesh = meshIndex.at(data->nodes[lod.nodes[level]].mesh), .distance = lod.distances[level]});
            }
            pieces.lodGroups.push_back(std::move(group));
        }
    }

    cgltf_free(data);
    return ok;
}

}
