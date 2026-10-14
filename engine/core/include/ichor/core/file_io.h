#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace ichor {

// Writes to a sibling temporary file, flushes, closes, then renames over the
// target, so a crash or a full disk never truncates an existing good file
// (doc 14, corruption safety).
// The temporary file is named for the process and a counter, so writers racing for one target
// never share it.
bool writeFileAtomically(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes);

// The whole of a regular file, or false with `bytes` empty when there is none: missing, a
// directory, unreadable, or longer than maxBytes. Never throws. (On Linux a directory opens as
// a stream and then throws when read through an iterator, which is why callers use this.)
bool readFileBytes(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes, std::uintmax_t maxBytes = std::uintmax_t{1} << 32);

}
