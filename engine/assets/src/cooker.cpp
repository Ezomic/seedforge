#include "ichor/assets/cooker.h"

#include "ichor/assets/image.h"
#include "ichor/core/log.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <meshoptimizer.h>

#include <algorithm>
#include <cmath>
#include <limits>
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

bool cookMesh(const cgltf_data& data, const cgltf_mesh& source, Mesh& mesh, CookStats& stats)
{
    mesh.name = source.name != nullptr ? source.name : "";
    mesh.boundsMin = glm::vec3(std::numeric_limits<float>::max());
    mesh.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());

    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> tangents;
    std::vector<float> uvs;
    bool needTangents = false;

    for (cgltf_size p = 0; p < source.primitives_count; ++p) {
        const cgltf_primitive& prim = source.primitives[p];
        if (prim.type != cgltf_primitive_type_triangles || prim.indices == nullptr) {
            logf(LogLevel::Warn, "mesh {}: skipping non-indexed or non-triangle primitive", mesh.name);
            continue;
        }
        const cgltf_accessor* posAcc = nullptr;
        const cgltf_accessor* normAcc = nullptr;
        const cgltf_accessor* tanAcc = nullptr;
        const cgltf_accessor* uvAcc = nullptr;
        for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
            const cgltf_attribute& attr = prim.attributes[a];
            if (attr.type == cgltf_attribute_type_position) posAcc = attr.data;
            if (attr.type == cgltf_attribute_type_normal) normAcc = attr.data;
            if (attr.type == cgltf_attribute_type_tangent) tanAcc = attr.data;
            if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) uvAcc = attr.data;
        }
        if (!readAccessor(posAcc, 3, positions) || !readAccessor(normAcc, 3, normals)) {
            logf(LogLevel::Error, "mesh {}: primitive without positions or normals", mesh.name);
            return false;
        }
        const bool hasUv = readAccessor(uvAcc, 2, uvs);
        const bool hasTangents = readAccessor(tanAcc, 4, tangents);
        needTangents = needTangents || !hasTangents;

        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (cgltf_size v = 0; v < posAcc->count; ++v) {
            Vertex vertex{};
            vertex.position = glm::make_vec3(positions.data() + v * 3);
            vertex.normal = glm::make_vec3(normals.data() + v * 3);
            vertex.tangent = hasTangents ? glm::make_vec4(tangents.data() + v * 4) : glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
            vertex.uv = hasUv ? glm::make_vec2(uvs.data() + v * 2) : glm::vec2(0.0f);
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
        return false;
    }
    if (needTangents) {
        generateTangents(mesh.vertices, mesh.indices);
        stats.tangentsGenerated += static_cast<std::uint32_t>(mesh.vertices.size());
    }
    optimizeMesh(mesh, stats);
    return true;
}

bool cookTexture(const cgltf_image& image, TextureFormat format, Texture& texture)
{
    texture.name = image.name != nullptr ? image.name : "";
    if (image.buffer_view == nullptr) {
        logf(LogLevel::Error, "texture {}: external images are not supported, embed them in the GLB", texture.name);
        return false;
    }
    const auto* bytes = static_cast<const std::uint8_t*>(image.buffer_view->buffer->data) + image.buffer_view->offset;
    return decodeTexture(bytes, image.buffer_view->size, format, texture);
}

}

bool cookGltf(const std::filesystem::path& source, SceneBundle& bundle, CookStats& stats)
{
    const std::string file = source.string();
    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, file.c_str(), &data) != cgltf_result_success) {
        logf(LogLevel::Error, "cannot parse {}", file);
        return false;
    }
    if (cgltf_load_buffers(&options, data, file.c_str()) != cgltf_result_success) {
        logf(LogLevel::Error, "cannot load buffers for {}", file);
        cgltf_free(data);
        return false;
    }

    // A texture's format depends on how a material uses it: colour is sRGB,
    // everything else is data.
    std::vector<TextureFormat> formats(data->images_count, TextureFormat::Rgba8Unorm);
    for (cgltf_size m = 0; m < data->materials_count; ++m) {
        const std::uint32_t color = textureIndex(*data, data->materials[m].pbr_metallic_roughness.base_color_texture.texture);
        if (color != kNoTexture) {
            formats[color] = TextureFormat::Rgba8Srgb;
        }
    }

    bool ok = true;
    bundle.textures.resize(data->images_count);
    for (cgltf_size i = 0; i < data->images_count && ok; ++i) {
        ok = cookTexture(data->images[i], formats[i], bundle.textures[i]);
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

    std::unordered_map<const cgltf_mesh*, std::uint32_t> meshIndex;
    for (cgltf_size m = 0; m < data->meshes_count && ok; ++m) {
        Mesh mesh;
        if (cookMesh(*data, data->meshes[m], mesh, stats)) {
            meshIndex[&data->meshes[m]] = static_cast<std::uint32_t>(bundle.meshes.size());
            bundle.meshes.push_back(std::move(mesh));
        }
    }

    for (cgltf_size n = 0; n < data->nodes_count && ok; ++n) {
        const cgltf_node& src = data->nodes[n];
        const auto found = src.mesh != nullptr ? meshIndex.find(src.mesh) : meshIndex.end();
        if (found == meshIndex.end()) {
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

    cgltf_free(data);
    return ok;
}

}
