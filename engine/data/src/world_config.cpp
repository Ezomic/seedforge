#include "ichor/data/world_config.h"

namespace ichor {

bool axiomsMayCoexist(const Axiom&, const Axiom&, const ConflictList&)
{
    return true;
}

WorldConfigResult rollWorldConfig(std::uint64_t, std::span<const Axiom>, const ConflictList&)
{
    return {};
}

WorldConfigResult pinWorldConfig(std::string_view, std::span<const Axiom>, const ConflictList&)
{
    return {};
}

WorldConfigResult loadRolledWorldConfig(std::uint64_t, const std::filesystem::path&)
{
    return {};
}

WorldConfigResult loadPinnedWorldConfig(std::string_view, const std::filesystem::path&)
{
    return {};
}

std::string worldConfigCanonicalText(const WorldConfig&)
{
    return {};
}

std::uint64_t worldConfigDigest(const WorldConfig&)
{
    return 0;
}

std::string worldConfigIds(const WorldConfig&)
{
    return {};
}

}
