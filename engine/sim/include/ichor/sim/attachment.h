#pragma once

#include "ichor/sim/components.h"

#include <glm/mat4x4.hpp>

#include <cstdint>

namespace ichor {

class Scene;

// A prop carried by a joint of another entity's skinned pose. The prop's mesh is baked at
// its bind placement, so the joint's skinning matrix is all it needs; `local` is applied
// first, for a grip that differs from the bind.
struct JointAttachment {
    StableId holder;
    StableId prop;
    std::uint32_t joint = 0;
    glm::mat4 local{1.0f};
};

// Puts the prop where the holder's joint carries it. Does nothing while the holder has
// no pose yet or either entity is missing. Run it as a scene system after animation.
void applyAttachment(Scene& scene, const JointAttachment& attachment);

}
