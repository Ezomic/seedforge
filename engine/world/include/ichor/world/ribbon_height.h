#pragma once

#include "ichor/assets/bundle.h"
#include "ichor/world/heightfield.h"

namespace ichor {

// The height of the terrain mesh at a point: the ground as the streamer triangulates it at full detail (each sample cell is two
// triangles split along the diagonal from its +x corner to its +z corner), which differs from Heightfield::heightAt's bilinear
// surface between samples.
float terrainMeshHeightAt(const Heightfield& heightfield, double x, double z);

// Lifts a ground-hugging strip (the road ribbon, a settlement street) clear of the terrain under it. The strip is cut into flat
// pieces and the terrain is triangulated in its own way, so a flat piece can dip under a bump between its vertices and fight it for
// the pixel. Every triangle of `mesh` is sampled over its whole area and each of its three vertices is raised by the deepest dip
// it finds (the same for vertices that share a position, so pieces still meet); a raised vertex only raises the plane, so one pass
// is enough. Vertex positions are relative to the cell origin (originX, originZ); vertices are expected to stand at the ground's
// height already plus the strip's lift, and a dip is measured against the ground plus `clearM`. A triangle that would need lifting
// more than `maxRaiseM` lies on a cliff face: it is dropped, so nothing floats over the drop and nothing is left to fight the ground.
void raiseOverGround(const Heightfield& heightfield, Mesh& mesh, double originX, double originZ, float clearM, float maxRaiseM);

}
