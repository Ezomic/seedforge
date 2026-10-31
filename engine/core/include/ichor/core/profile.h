#pragma once

// Tracy zones for any module. A runtime build (engine/CMakeLists.txt) defines ICHOR_TRACY for every target, and a zone
// is then Tracy's own, which is empty unless ENGINE_PROFILE defined TRACY_ENABLE. A headless build defines nothing and
// a zone is nothing, so the macros can sit in audio, world and UI code that links no profiler.
#if defined(ICHOR_TRACY)
#include <tracy/Tracy.hpp>
#define ICHOR_ZONE ZoneScoped
#define ICHOR_ZONE_N(name) ZoneScopedN(name)
#else
#define ICHOR_ZONE
#define ICHOR_ZONE_N(name)
#endif
