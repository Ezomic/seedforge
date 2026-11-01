#include "ichor/platform/sdl_platform.h"

#include "ichor/core/log.h"
#include "ichor/core/version.h"

#include <SDL3/SDL.h>

#include <algorithm>
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
    window_ = SDL_CreateWindow(versionedTitle(desc.title).c_str(), desc.width, desc.height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | (desc.hidden ? SDL_WINDOW_HIDDEN : 0));
    if (window_ == nullptr) {
        logf(LogLevel::Error, "SDL_CreateWindow failed: {}", SDL_GetError());
        return false;
    }
    // A window opened behind another one never hears that it lost focus, so it starts from what it has.
    focused_ = (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) != 0;
    return true;
}

void SdlPlatform::enableLook(bool enabled)
{
    lookEnabled_ = enabled;
    mouseLook_.setHeld(false);
    if (window_ != nullptr) {
        SDL_SetWindowRelativeMouseMode(window_, false);
    }
}

void SdlPlatform::pumpEvents()
{
    const double pumpedAt = now();
    const double elapsed = pumpedAt - lastPump_;
    lastPump_ = pumpedAt;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (hook_) {
            hook_(event);
        }
        if (lookEnabled_ && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_RIGHT) {
            mouseLook_.setHeld(true);
            SDL_SetWindowRelativeMouseMode(window_, true);
        }
        if (lookEnabled_ && event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_RIGHT) {
            mouseLook_.setHeld(false);
            SDL_SetWindowRelativeMouseMode(window_, false);
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
            mouseAttack_.click((SDL_GetModState() & SDL_KMOD_CTRL) != 0);
        }
        if (lookEnabled_ && event.type == SDL_EVENT_MOUSE_MOTION) {
            mouseLook_.addMotion(event.motion.xrel);
        }
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            pressedKey_ = static_cast<KeyCode>(event.key.scancode);
        }
        if (event.type == SDL_EVENT_QUIT) {
            quit_ = true;
        }
        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
            focused_ = event.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
        }
        if (typing_.typing()) {
            if (event.type == SDL_EVENT_TEXT_INPUT) {
                typed_ += event.text.text;
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.key == SDLK_BACKSPACE) typed_ += '\b';
                if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) typed_ += '\r';
                if (event.key.key == SDLK_ESCAPE) typed_ += '\x1b';
            }
        } else if (escapeQuits_ && event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
            quit_ = true;
        }
    }

    const bool* keys = SDL_GetKeyboardState(nullptr);
    const auto down = [&](SDL_Scancode a, SDL_Scancode b) { return keys[a] || keys[b]; };
    InputFrame frame;
    const bool arrowsWalk = !arrowsReserved_ && !lookEnabled_;
    const auto bound = [&](BindAction action) { return keys[bindings_.key(action)]; };
    const auto walk = [&](BindAction action, SDL_Scancode arrow) { return arrowsWalk ? bound(action) || keys[arrow] : bound(action); };
    frame.moveX = (walk(BindAction::MoveRight, SDL_SCANCODE_RIGHT) ? 1.0f : 0.0f) - (walk(BindAction::MoveLeft, SDL_SCANCODE_LEFT) ? 1.0f : 0.0f);
    frame.moveY = (walk(BindAction::MoveForward, SDL_SCANCODE_UP) ? 1.0f : 0.0f) - (walk(BindAction::MoveBack, SDL_SCANCODE_DOWN) ? 1.0f : 0.0f);
    if (bound(BindAction::Confirm) || keys[SDL_SCANCODE_KP_ENTER]) frame.buttons |= InputFrame::Confirm;
    if (bound(BindAction::Cancel)) frame.buttons |= InputFrame::Cancel;
    if (bound(BindAction::Sprint) || keys[SDL_SCANCODE_RSHIFT]) frame.buttons |= InputFrame::Sprint;
    if (bound(BindAction::Attack)) frame.buttons |= InputFrame::Attack;
    if (bound(BindAction::Heavy)) frame.buttons |= InputFrame::Heavy;
    if (bound(BindAction::Block)) frame.buttons |= InputFrame::Block;
    if (bound(BindAction::Dodge)) frame.buttons |= InputFrame::Dodge;
    if (bound(BindAction::QuickSave)) frame.buttons |= InputFrame::QuickSave;
    if (bound(BindAction::QuickLoad)) frame.buttons |= InputFrame::QuickLoad;
    if (bound(BindAction::Journal)) frame.buttons |= InputFrame::Journal;
    if (keys[SDL_SCANCODE_ESCAPE]) frame.buttons |= InputFrame::Menu;
    if (bound(BindAction::Map)) frame.buttons |= InputFrame::Map;
    if (bound(BindAction::Notes)) frame.buttons |= InputFrame::Notes;
    if (bound(BindAction::Character)) frame.buttons |= InputFrame::Character;
    if (bound(BindAction::Interact)) frame.buttons |= InputFrame::Interact;
    if (bound(BindAction::Inventory)) frame.buttons |= InputFrame::Inventory;
    if (lookEnabled_) {
        if (bound(BindAction::LockOn)) frame.buttons |= InputFrame::LockOn;
        const float mouse = mouseLook_.step(elapsed);
        const float arrows = arrowsReserved_ ? 0.0f : (keys[SDL_SCANCODE_RIGHT] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_LEFT] ? 1.0f : 0.0f);
        frame.lookX = std::clamp(mouse + arrows, -1.0f, 1.0f);
    }

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
    mouseAttack_.setBlockHeld((mouseButtons & SDL_BUTTON_MMASK) != 0);
    frame.buttons |= mouseAttack_.step(static_cast<float>(elapsed));
    pointer_.primary = (mouseButtons & SDL_BUTTON_LMASK) != 0;
    pointer_.inside = (SDL_GetWindowFlags(window_) & SDL_WINDOW_MOUSE_FOCUS) != 0;
    navKeys_ = {keys[SDL_SCANCODE_LEFT], keys[SDL_SCANCODE_RIGHT], keys[SDL_SCANCODE_UP], keys[SDL_SCANCODE_DOWN], keys[SDL_SCANCODE_PAGEUP], keys[SDL_SCANCODE_PAGEDOWN],
        down(SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT)};
    overlayKey_ = keys[SDL_SCANCODE_F3];
    // While the note box has the keyboard, the card keys stay still too.
    const bool cardKeys = !typing_.typing();
    helpKey_ = cardKeys && keys[SDL_SCANCODE_F1];
    soundLegendKey_ = cardKeys && keys[SDL_SCANCODE_F2];
    captionKey_ = cardKeys && keys[SDL_SCANCODE_F4];
    noteKey_ = keys[SDL_SCANCODE_F8];
    rawInput_ = frame;
    input_ = typing_.filter(frame);
}

void SdlPlatform::setTextEntry(bool on)
{
    if (on == typing_.typing()) {
        return;
    }
    typing_.setTyping(on, rawInput_);
    typed_.clear();
    // Whatever the game read from this pump is taken back now, not at the next one.
    input_ = typing_.filter(rawInput_);
    if (window_ != nullptr) {
        if (on) {
            SDL_StartTextInput(window_);
        } else {
            SDL_StopTextInput(window_);
        }
    }
}

std::vector<Extent> SdlPlatform::displayModes() const
{
    std::vector<Extent> sizes;
    if (window_ == nullptr) {
        return sizes;
    }
    int count = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(SDL_GetDisplayForWindow(window_), &count);
    if (modes == nullptr) {
        return sizes;
    }
    // SDL lists them largest first, then by refresh rate, so each size's first mode is the one kept.
    for (int i = 0; i < count; ++i) {
        const Extent size{modes[i]->w, modes[i]->h};
        if (size.width > 0 && size.height > 0 && std::find(sizes.begin(), sizes.end(), size) == sizes.end()) {
            sizes.push_back(size);
        }
    }
    SDL_free(modes);
    return sizes;
}

bool SdlPlatform::setDisplay(const DisplayRequest& request)
{
    if (window_ == nullptr) {
        return false;
    }
    const bool sized = request.size.width > 0 && request.size.height > 0;
    bool ok = true;
    if (request.fullscreen) {
        SDL_DisplayMode closest{};
        const bool exclusive = sized && SDL_GetClosestFullscreenDisplayMode(SDL_GetDisplayForWindow(window_), request.size.width, request.size.height, 0.0f, false, &closest);
        // Without a size, or with one no mode has, the display keeps its own mode and the window fills it.
        ok = SDL_SetWindowFullscreenMode(window_, exclusive ? &closest : nullptr) && ok;
        ok = SDL_SetWindowFullscreen(window_, true) && ok;
    } else {
        ok = SDL_SetWindowFullscreen(window_, false) && ok;
        if (sized) {
            // A mode larger than the desktop is a fullscreen size; the window stays inside the usable screen.
            Extent size = request.size;
            SDL_Rect usable{};
            if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window_), &usable)) {
                size.width = std::min(size.width, usable.w);
                size.height = std::min(size.height, usable.h);
            }
            ok = SDL_SetWindowSize(window_, size.width, size.height) && ok;
            ok = SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED) && ok;
        }
    }
    // Blocks until the window system has done it, so the next drawableSize() is the new one.
    SDL_SyncWindow(window_);
    if (!ok) {
        logf(LogLevel::Warn, "display: the window could not be changed: {}", SDL_GetError());
    }
    return ok;
}

bool SdlPlatform::fullscreen() const
{
    return window_ != nullptr && (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
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
