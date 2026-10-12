#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace ichor {

// Writes to a sibling temporary file, flushes, closes, then renames over the
// target, so a crash or a full disk never truncates an existing good file
// (doc 14, corruption safety).
bool writeFileAtomically(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes);

}
