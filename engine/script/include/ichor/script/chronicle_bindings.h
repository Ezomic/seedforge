#pragma once

#include "ichor/script/script_host.h"

namespace ichor {

class Chronicle;
class Scene;

// ichor.chronicle(category, text) records a notable event at the scene's
// current tick. The category is lowercase letters, digits, dash or
// underscore; anything else is a Lua error. The chronicle and the scene must
// outlive the host.
void bindChronicle(ScriptHost& host, Chronicle& chronicle, const Scene& scene);

}
