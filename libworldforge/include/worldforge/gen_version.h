#pragma once

#include <cstdint>

// The generator version (doc 08, Determinism, note 5). Bump it whenever any phase changes its
// output for existing inputs: it is mixed into the forge world hash and stored in the manifest, so
// an old seed is never silently regenerated into a different world.
namespace worldforge {

constexpr std::uint32_t kGenVersion = 1;

}
