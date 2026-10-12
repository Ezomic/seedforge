#include "ichor/platform/null_platform.h"

#include <chrono>

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
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

}
