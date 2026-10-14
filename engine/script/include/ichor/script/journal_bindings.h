#pragma once

#include "ichor/data/journal.h"
#include "ichor/script/script_host.h"

#include <optional>

namespace ichor {

class Chronicle;
class Scene;

// ichor.journal(who, what, where, decided) writes one prose entry into the chronicle at the
// scene's current tick, under the "journal" category. Every argument is a string and may be
// left off or empty. It is a Lua error when an argument is not a string, when the chronicle is
// full, or when no templates are loaded (the optional is empty). The chronicle, the scene and
// the templates must outlive the host; the templates are read at each call, so a reload that
// replaces them takes effect at once.
void bindJournal(ScriptHost& host, Chronicle& chronicle, const Scene& scene, const std::optional<JournalTemplates>& templates);

}
