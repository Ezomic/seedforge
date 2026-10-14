#include "ichor/platform/sdl_platform.h"

#include "ichor/core/log.h"

#include <SDL3/SDL.h>

#include <cmath>

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
        if (escapeQuits_ && event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
            quit_ = true;
        }
    }

    const bool* keys = SDL_GetKeyboardState(nullptr);
    const auto down = [&](SDL_Scancode a, SDL_Scancode b) { return keys[a] || keys[b]; };
    InputFrame frame;
    frame.moveX = (down(SDL_SCANCODE_D, SDL_SCANCODE_RIGHT) ? 1.0f : 0.0f) - (down(SDL_SCANCODE_A, SDL_SCANCODE_LEFT) ? 1.0f : 0.0f);
    frame.moveY = (down(SDL_SCANCODE_W, SDL_SCANCODE_UP) ? 1.0f : 0.0f) - (down(SDL_SCANCODE_S, SDL_SCANCODE_DOWN) ? 1.0f : 0.0f);
    if (down(SDL_SCANCODE_RETURN, SDL_SCANCODE_KP_ENTER)) frame.buttons |= InputFrame::Confirm;
    if (keys[SDL_SCANCODE_Q]) frame.buttons |= InputFrame::Cancel;
    if (down(SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT)) frame.buttons |= InputFrame::Sprint;
    if (keys[SDL_SCANCODE_J]) frame.buttons |= InputFrame::Attack;
    if (keys[SDL_SCANCODE_K]) frame.buttons |= InputFrame::Heavy;
    if (keys[SDL_SCANCODE_L]) frame.buttons |= InputFrame::Block;
    if (keys[SDL_SCANCODE_SPACE]) frame.buttons |= InputFrame::Dodge;
    if (keys[SDL_SCANCODE_F5]) frame.buttons |= InputFrame::QuickSave;
    if (keys[SDL_SCANCODE_F9]) frame.buttons |= InputFrame::QuickLoad;
    if (keys[SDL_SCANCODE_TAB]) frame.buttons |= InputFrame::Journal;
    if (keys[SDL_SCANCODE_ESCAPE]) frame.buttons |= InputFrame::Menu;
    if (keys[SDL_SCANCODE_M]) frame.buttons |= InputFrame::Map;

    // The mouse is reported in window units; the UI works in drawable pixels, which differ on a scaled display.
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    const SDL_MouseButtonFlags mouseButtons = SDL_GetMouseState(&mouseX, &mouseY);
    int windowWidth = 0;
    int windowHeight = 0;
    SDL_GetWindowSize(window_, &windowWidth, &windowHeight);
    const Extent pixels = drawableSize();
    const float scaleX = windowWidth > 0 ? static_cast<float>(pixels.width) / static_cast<float>(windowWidth) : 1.0f;
    const float scaleY = windowHeight > 0 ? static_cast<float>(pixels.height) / static_cast<float>(windowHeight) : 1.0f;
    pointer_.x = static_cast<int>(std::lround(mouseX * scaleX));
    pointer_.y = static_cast<int>(std::lround(mouseY * scaleY));
    pointer_.primary = (mouseButtons & SDL_BUTTON_LMASK) != 0;
    pointer_.inside = (SDL_GetWindowFlags(window_) & SDL_WINDOW_MOUSE_FOCUS) != 0;
    navKeys_ = {keys[SDL_SCANCODE_LEFT], keys[SDL_SCANCODE_RIGHT], keys[SDL_SCANCODE_UP], keys[SDL_SCANCODE_DOWN], keys[SDL_SCANCODE_PAGEUP], keys[SDL_SCANCODE_PAGEDOWN],
        down(SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT)};
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
