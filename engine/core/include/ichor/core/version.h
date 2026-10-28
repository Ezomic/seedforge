#pragma once

#include <string>
#include <string_view>

namespace ichor {

// The short commit hash the build was configured from, "-dirty" when the tree had changes, or "unknown" without git.
// It lives in one generated file, so a new stamp recompiles that file alone.
std::string_view versionStamp();

// The first line the app logs, so a tester's report names the build.
std::string logHeader();

std::string versionedTitle(std::string_view title);

}
