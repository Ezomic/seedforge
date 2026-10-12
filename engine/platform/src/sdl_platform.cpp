#include "ichor/platform/sdl_platform.h"

#include "ichor/core/log.h"

#include <SDL3/SDL.h>

namespace ichor {

SdlPlatform::~SdlPlatform()
{
    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
    }
    SDL_Quit();
}

bool SdlPlatform::init(const WindowDesc& desc)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        logf(LogLevel::Error, "SDL_Init failed: {}", SDL_GetError());
        return false;
    }
    window_ = SDL_CreateWindow(desc.title.c_str(), desc.width, desc.height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (window_ == nullptr) {
        logf(LogLevel::Error, "SDL_CreateWindow failed: {}", SDL_GetError());
        return false;
    }
    return true;
}

void SdlPlatform::pumpEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (hook_) {
            hook_(event);
        }
        if (event.type == SDL_EVENT_QUIT) {
            quit_ = true;
        }
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
            quit_ = true;
        }
    }
}

double SdlPlatform::now() const
{
    return static_cast<double>(SDL_GetTicksNS()) * 1e-9;
}

Extent SdlPlatform::drawableSize() const
{
    Extent extent;
    SDL_GetWindowSizeInPixels(window_, &extent.width, &extent.height);
    return extent;
}

}
