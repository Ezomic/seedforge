#pragma once

// Tracy zones for any module. The dependency policy keeps headless libraries off the runtime-only profile target, and
// a build flag on every target would cold-start CI's compiler cache, so no target is wired to Tracy for this: a runtime
// build installs Tracy's headers next to the other vcpkg headers every module already sees, and a zone is then Tracy's
// own, empty unless ENGINE_PROFILE defined TRACY_ENABLE. A headless build has no Tracy and a zone is nothing.
#if __has_include(<tracy/Tracy.hpp>)
#include <tracy/Tracy.hpp>
#define ICHOR_ZONE ZoneScoped
#define ICHOR_ZONE_N(name) ZoneScopedN(name)
#else
#define ICHOR_ZONE
#define ICHOR_ZONE_N(name)
#endif
