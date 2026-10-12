#pragma once

#include "ichor/script/script_host.h"

namespace ichor {

class Scene;

// What a script may do to the world. Every write is a scene command, so the
// command log, replay and the state hash keep holding (doc 15, rule 2):
//
//   ichor.spawn(name, x, y, z [, mesh])   create an entity named `name`
//   ichor.destroy(name)                   remove it
//   ichor.move(name, dx, dy, dz)          teleport it by a delta
//   ichor.exists(name) -> bool            whether it is in the scene now
//
// Commands apply at the start of the next tick, so exists() does not see a
// spawn until then. The scene must outlive the host.
void bindScene(ScriptHost& host, Scene& scene);

}
