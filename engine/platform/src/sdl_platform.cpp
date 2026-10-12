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

    const bool* keys = SDL_GetKeyboardState(nullptr);
    const auto down = [&](SDL_Scancode a, SDL_Scancode b) { return keys[a] || keys[b]; };
    InputFrame frame;
    frame.moveX = (down(SDL_SCANCODE_D, SDL_SCANCODE_RIGHT) ? 1.0f : 0.0f) - (down(SDL_SCANCODE_A, SDL_SCANCODE_LEFT) ? 1.0f : 0.0f);
    frame.moveY = (down(SDL_SCANCODE_W, SDL_SCANCODE_UP) ? 1.0f : 0.0f) - (down(SDL_SCANCODE_S, SDL_SCANCODE_DOWN) ? 1.0f : 0.0f);
    if (keys[SDL_SCANCODE_SPACE]) frame.buttons |= InputFrame::Confirm;
    if (keys[SDL_SCANCODE_Q]) frame.buttons |= InputFrame::Cancel;
    if (down(SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT)) frame.buttons |= InputFrame::Sprint;
    input_ = frame;
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
