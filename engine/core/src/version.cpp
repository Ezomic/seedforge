#include "ichor/core/version.h"

#include <format>

namespace ichor {

std::string logHeader()
{
    return std::format("Ichor build {}", versionStamp());
}

std::string versionedTitle(std::string_view title)
{
    return std::format("{} (build {})", title, versionStamp());
}

}
