#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <vector>

namespace ichor {

// What the simulation hands the renderer each frame. World positions are
// doubles; the renderer subtracts the camera before anything touches a float
// (doc 17, architecture rule 8).

struct RenderInstance {
    std::uint32_t mesh = 0;
    glm::dvec3 position{0.0};
    glm::mat3 basis{1.0f}; // rotation and uniform scale; normals are transformed by it directly
    // Skinned instances point at a run of matrices in RenderView::jointPalette.
    std::uint32_t jointOffset = 0;
    std::uint32_t jointCount = 0; // zero draws the mesh rigid
    // Asks for the object's base to take on the ground's colour (RenderView::groundColor). Only
    // for objects that stand on the ground with their origin at their foot: height is measured
    // up from the origin, so a centred or floating object would be painted wrongly. The
    // height is flat, from the foot's level, not from the terrain's surface under each point.
    // Skinned instances ignore it.
    bool groundBlend = false;
};

// A resident terrain cell: the handle the renderer gave its meshes, where
// its origin sits in the world, and which level of detail to draw.
struct TerrainDraw {
    std::uint32_t cell = 0;
    glm::dvec3 origin{0.0};
    std::uint32_t lod = 0;
};

// One planted tree, relative to the origin of its cell.
struct PlantInstance {
    glm::vec3 position{0.0f};
    float yaw = 0.0f; // radians about the vertical axis
    float scale = 1.0f;
    std::uint32_t kind = 0; // which of the vegetation meshes
};

// A resident vegetation cell: the handle the renderer gave its instances and where the cell sits.
struct VegetationDraw {
    std::uint32_t cell = 0;
    glm::dvec3 origin{0.0};
};

// The grade laid over the picture as the scene shaders draw it: sky, water, ground and
// objects, but not rain, the debug lines or the UI. The defaults change nothing.
struct ColourGrade {
    float saturation = 1.0f; // 0 is grey, 1 as drawn
    float contrast = 1.0f;   // around middle grey
    float exposure = 1.0f;   // before tone mapping
    glm::vec3 shadows{1.0f};    // multiplies the dark end
    glm::vec3 highlights{1.0f}; // multiplies the bright end
};

// How far any surface may stray, whatever its source: the range roughness is held to and
// the range of albedo brightness for non-metals. The defaults change nothing.
struct MaterialResponse {
    float roughnessMin = 0.04f;
    float roughnessMax = 1.0f;
    float albedoMin = 0.0f;
    float albedoMax = 1000.0f; // effectively open
};

// A resident water cell: the handle the renderer gave its surface mesh and where the cell sits.
struct WaterDraw {
    std::uint32_t cell = 0;
    glm::dvec3 origin{0.0};
};

struct RenderCamera {
    glm::dvec3 position{0.0};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // looks down -Z, Y up
    float fovY = glm::radians(60.0f);
    float nearPlane = 0.1f;
    float farPlane = 500.0f;
};

struct DebugLine {
    glm::dvec3 a{0.0};
    glm::dvec3 b{0.0};
    glm::vec3 color{1.0f};
};

struct RenderView {
    RenderCamera camera;
    glm::vec3 sunDirection{0.4f, 0.8f, 0.3f}; // towards the sun
    glm::vec3 sunColor{1.0f, 0.96f, 0.9f};
    float sunIntensity = 3.0f;
    glm::vec3 ambient{0.18f, 0.2f, 0.26f};
    glm::vec3 clearColor{0.45f, 0.58f, 0.75f};
    // The sky is drawn instead of the flat clear colour when enabled, and fog
    // fades everything towards fogColor with distance. Both are linear colours.
    bool skyEnabled = false;
    glm::vec3 skyZenith{0.12f, 0.3f, 0.65f};
    glm::vec3 skyHorizon{0.55f, 0.7f, 0.85f};
    glm::vec3 fogColor{0.55f, 0.7f, 0.85f};
    float fogDensity = 0.0f; // per metre; zero is no fog
    float rainIntensity = 0.0f; // 0 dry to 1 downpour; streaks are drawn over the picture
    float wetness = 0.0f;       // 0 dry to 1 soaked; terrain, plants and props darken and gain a little gloss
    float time = 0.0f;          // seconds, for effects that move; the caller keeps it small
    glm::vec2 windDirection{1.0f, 0.0f}; // ground-plane unit vector; trees lean and sway along it
    float windStrength = 0.0f;           // 0 still to 1 gale
    float fogHeightFalloff = 0.0f;       // per metre; zero is fog of one density at every height
    ColourGrade grade;
    glm::vec3 groundColor{0.1f, 0.16f, 0.06f}; // linear colour of the ground, for blending objects into it
    float groundBlendHeight = 0.0f;            // metres up an object the blend reaches; zero is off
    MaterialResponse response;
    float cloudCover = 0.0f;             // 0 is a clear sky, 1 solid cloud; the sky draws a drifting layer of it
    float lightShafts = 0.0f;            // how strongly the sun scatters in the fog where the shadows let it through; zero is off
    // A flat sheet of water at one height, over the whole world. Seen only from above.
    bool waterEnabled = false;
    float waterLevel = 0.0f;                   // world y in metres
    glm::vec3 waterColor{0.02f, 0.10f, 0.13f}; // linear colour of deep water
    std::vector<RenderInstance> instances;
    std::vector<glm::mat4> jointPalette; // skinning matrices, model space times inverse bind
    std::vector<TerrainDraw> terrain;
    std::vector<VegetationDraw> vegetation;
    std::vector<WaterDraw> waterCells; // rivers and lakes, drawn with the sea's shading
    std::vector<DebugLine> lines;
};

}
