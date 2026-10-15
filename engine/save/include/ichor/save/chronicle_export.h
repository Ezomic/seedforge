#pragma once

#include "ichor/save/chronicle.h"

#include <string>
#include <string_view>

namespace ichor {

// A chronicle as plain text to read or share: a title, an entry count and one
// line per entry. Pure and deterministic. Entry text is made safe to print:
// invalid UTF-8 becomes U+FFFD and control characters, line separators and bidi
// controls become spaces, so no entry can add lines or steer a terminal. The
// chronicle and its saved form are not touched.
std::string exportChronicleText(const Chronicle& chronicle, double secondsPerTick);

// True when the bytes are well-formed UTF-8 (no overlongs, surrogates or code points past U+10FFFF).
bool isValidUtf8(std::string_view bytes);

}
