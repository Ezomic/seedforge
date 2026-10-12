#include "ichor/platform/null_platform.h"

namespace ichor {

bool NullPlatform::init(const WindowDesc&)
{
    return true;
}

void NullPlatform::pumpEvents()
{
    ++pumps_;
}

double NullPlatform::now() const
{
    return static_cast<double>(pumps_) * kTick;
}

}
