#include "ichor/gfx/null_renderer.h"

namespace ichor {

void NullRenderer::beginFrame()
{
    ++begun_;
}

void NullRenderer::endFrame()
{
    ++ended_;
}

void NullRenderer::debugText(std::string)
{
    ++lines_;
}

}
